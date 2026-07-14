# SPDX-License-Identifier: MIT-0

[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Project = (Join-Path $PSScriptRoot 'DataStructures.sln'),
    [string]$Configuration,
    [string]$Filter,
    [switch]$NoRestore,
    [switch]$NoBuild,
    [switch]$Blame,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AdditionalArguments
)

$ErrorActionPreference = 'Stop'

$headlessModeScript = Join-Path $PSScriptRoot '..\..\eng\Enable-HeadlessTestMode.ps1'
. $headlessModeScript
$null = Enable-HeadlessTestMode

$dotnetArguments = @(
    'test'
    $Project
    '--settings'
    (Join-Path $PSScriptRoot 'test.runsettings')
    '--nologo'
)

if (-not [string]::IsNullOrWhiteSpace($Configuration)) {
    $dotnetArguments += @('--configuration', $Configuration)
}
if (-not [string]::IsNullOrWhiteSpace($Filter)) {
    $dotnetArguments += @('--filter', $Filter)
}
if ($NoRestore) {
    $dotnetArguments += '--no-restore'
}
if ($NoBuild) {
    $dotnetArguments += '--no-build'
}
if ($Blame) {
    $dotnetArguments += '--blame'
}
[string[]]$forwardedArguments = @()
if ($null -ne $AdditionalArguments) {
    $forwardedArguments = @($AdditionalArguments)
}
$separatorIndex = if ($forwardedArguments.Length -eq 0) {
    -1
}
else {
    [Array]::IndexOf($forwardedArguments, '--')
}
if ($separatorIndex -ge 0) {
    $dotnetOptions = if ($separatorIndex -gt 0) {
        @($forwardedArguments[0..($separatorIndex - 1)])
    }
    else {
        @()
    }
    $testHostOptions = if ($separatorIndex + 1 -lt $forwardedArguments.Count) {
        @($forwardedArguments[($separatorIndex + 1)..($forwardedArguments.Count - 1)])
    }
    else {
        @()
    }
}
else {
    $dotnetOptions = $forwardedArguments
    $testHostOptions = @()
}
$dotnetOptions = @($dotnetOptions)
$testHostOptions = @($testHostOptions)
for ($index = 0; $index -lt $dotnetOptions.Count; $index++) {
    $argument = $dotnetOptions[$index]
    if ($argument -match '^(?i:(?:-s|--settings|/settings)(?:[:=].*)?)$') {
        throw 'Forwarding a custom runsettings file is not supported because it could disable the repository single-worker test policy.'
    }

    $propertyPayloads = [System.Collections.Generic.List[string]]::new()
    if ($argument -match '^(?i:(?:-p|--p|--property|-property|/p|/property)[:=])(?<Properties>.*)$') {
        $propertyPayloads.Add($Matches.Properties)
    }
    elseif ($argument -match '^(?i:-p|--p|--property|-property|/p|/property)$') {
        for ($propertyIndex = $index + 1; $propertyIndex -lt $dotnetOptions.Count; $propertyIndex++) {
            $candidate = $dotnetOptions[$propertyIndex]
            if ($candidate -match '^[-/@]' -or $candidate -notmatch '=') {
                break
            }
            $propertyPayloads.Add($candidate)
        }
    }
    foreach ($propertyPayload in $propertyPayloads) {
        if ($propertyPayload -match '(?i)(?:^|[;,])\s*(?:VSTestSetting|RunSettingsFilePath)=') {
            throw 'Forwarding a custom runsettings MSBuild property is not supported because it could disable the repository single-worker test policy.'
        }
    }
    if ($argument.StartsWith('@', [System.StringComparison]::Ordinal)) {
        throw 'Forwarding response files is not supported because their contents could override the repository single-worker policy.'
    }
}
$dotnetArguments += $dotnetOptions
# Append enforced build controls after caller options so they cannot fan out MSBuild or restore.
$dotnetArguments += @(
    '--disable-build-servers'
    '--maxcpucount:1'
    '-nr:false'
    '-p:BuildInParallel=false'
    '-p:RestoreDisableParallel=true'
    '-p:UseSharedCompilation=false'
)
if ($separatorIndex -ge 0) {
    $dotnetArguments += '--'
    $dotnetArguments += $testHostOptions
    $dotnetArguments += @(
        'RunConfiguration.MaxCpuCount=1'
        'xUnit.ParallelizeAssembly=false'
        'xUnit.ParallelizeTestCollections=false'
        'xUnit.MaxParallelThreads=1'
    )
}

Push-Location $PSScriptRoot
try {
    & dotnet @dotnetArguments
    $testExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

exit $testExitCode
