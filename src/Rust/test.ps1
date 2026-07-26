# SPDX-License-Identifier: MIT-0

<#
.SYNOPSIS
Runs the Rust test suites.

.PARAMETER Workspace
Which workspace to test, or All for every one.

.PARAMETER Release
Test the optimized build rather than the debug build.

.PARAMETER CargoArguments
Further arguments passed through to cargo.
#>


[CmdletBinding()]
param(
    [ValidateSet('All', 'Hamt', 'FingerTree', 'Ordered', 'RangeUpdate')]
    [string] $Workspace = 'All',

    [switch] $Release,

    [string[]] $CargoArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\..\eng\Enable-HeadlessTestMode.ps1')
$null = Enable-HeadlessTestMode

$cargo = Get-Command cargo -ErrorAction SilentlyContinue
if ($null -eq $cargo) {
    $rustupCargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
    if (-not (Test-Path -LiteralPath $rustupCargo)) {
        throw 'Cargo was not found on PATH or under the default rustup profile.'
    }

    $cargoPath = $rustupCargo
}
else {
    $cargoPath = $cargo.Source
}

$selection = switch ($Workspace) {
    'All' { @('--workspace') }
    'Hamt' { @('-p', 'durable7-hamt') }
    'FingerTree' { @('-p', 'durable7-fingertree') }
    'Ordered' { @('-p', 'durable7-ordered') }
    'RangeUpdate' { @('-p', 'durable7-range-update') }
}

$separatorIndex = [Array]::IndexOf([string[]] $CargoArguments, '--')
if ($separatorIndex -ge 0) {
    $cargoOptions = if ($separatorIndex -gt 0) {
        @($CargoArguments[0..($separatorIndex - 1)])
    }
    else {
        @()
    }
    $testOptions = if ($separatorIndex + 1 -lt $CargoArguments.Count) {
        @($CargoArguments[($separatorIndex + 1)..($CargoArguments.Count - 1)])
    }
    else {
        @()
    }
}
else {
    $cargoOptions = @($CargoArguments)
    $testOptions = @()
}
$cargoOptions = @($cargoOptions)
$testOptions = @($testOptions)

$normalizedCargoOptions = [System.Collections.Generic.List[string]]::new()
for ($index = 0; $index -lt $cargoOptions.Count; $index++) {
    $argument = $cargoOptions[$index]
    if ($argument -eq '--jobs' -or $argument -eq '-j') {
        if ($index + 1 -ge $cargoOptions.Count -or
            $cargoOptions[$index + 1] -notmatch '^(default|[+-]?\d+)$') {
            throw "$argument requires a job count or 'default'."
        }
        $index++
        continue
    }
    if ($argument -match '^--jobs=(?<Jobs>.*)$') {
        if ($Matches.Jobs -notmatch '^(default|[+-]?\d+)$') {
            throw "Invalid Cargo job option: $argument"
        }
        continue
    }
    if ($argument -match '^-j=?(?<Jobs>.+)$') {
        if ($Matches.Jobs -notmatch '^(default|[+-]?\d+)$') {
            throw "Invalid Cargo job option: $argument"
        }
        continue
    }
    $normalizedCargoOptions.Add($argument)
}
$cargoOptions = @($normalizedCargoOptions)

$normalizedTestOptions = [System.Collections.Generic.List[string]]::new()
for ($index = 0; $index -lt $testOptions.Count; $index++) {
    $argument = $testOptions[$index]
    if ($argument -eq '--test-threads') {
        if ($index + 1 -ge $testOptions.Count -or
            $testOptions[$index + 1] -notmatch '^\d+$') {
            throw "$argument requires a non-negative integer."
        }
        $index++
        continue
    }
    if ($argument -match '^--test-threads=(?<Threads>.*)$') {
        if ($Matches.Threads -notmatch '^\d+$') {
            throw "Invalid rusttest thread option: $argument"
        }
        continue
    }
    $normalizedTestOptions.Add($argument)
}
$testOptions = @($normalizedTestOptions)

$arguments = @('test') + $selection
if ($Release) {
    $arguments += '--release'
}
$arguments += $cargoOptions
# Append the enforced values after caller options so a profile or forwarded flag cannot fan out.
$arguments += @('--jobs', '1', '--')
$arguments += $testOptions
$arguments += '--test-threads=1'

$previousCargoBuildJobs = $env:CARGO_BUILD_JOBS
$previousRustTestThreads = $env:RUST_TEST_THREADS
$env:CARGO_BUILD_JOBS = '1'
$env:RUST_TEST_THREADS = '1'

Push-Location -LiteralPath $PSScriptRoot
try {
    & $cargoPath @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Rust $Workspace tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
    if ($null -eq $previousCargoBuildJobs) {
        Remove-Item Env:CARGO_BUILD_JOBS -ErrorAction SilentlyContinue
    }
    else {
        $env:CARGO_BUILD_JOBS = $previousCargoBuildJobs
    }
    if ($null -eq $previousRustTestThreads) {
        Remove-Item Env:RUST_TEST_THREADS -ErrorAction SilentlyContinue
    }
    else {
        $env:RUST_TEST_THREADS = $previousRustTestThreads
    }
}
