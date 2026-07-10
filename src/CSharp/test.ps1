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
if ($null -ne $AdditionalArguments) {
    $dotnetArguments += $AdditionalArguments
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
