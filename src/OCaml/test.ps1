# SPDX-License-Identifier: MIT-0

[CmdletBinding()]
param(
    [ValidateSet('All', 'Common', 'Numerics', 'Hamt', 'FingerTree', 'Ordered', 'Tungsten')]
    [string] $Workspace = 'All',

    [string[]] $DuneArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\..\eng\Enable-HeadlessTestMode.ps1')
$null = Enable-HeadlessTestMode

$opam = Get-Command opam -ErrorAction Stop
$target = switch ($Workspace) {
    'All' { $null }
    'Common' { 'tests/common' }
    'Numerics' { 'tests/numerics' }
    'Hamt' { 'tests/hamt' }
    'FingerTree' { 'tests/finger_tree' }
    'Ordered' { 'tests/ordered' }
    'Tungsten' { 'tests/tungsten' }
}

$arguments = @('exec', '--', 'dune', 'runtest')
if ($null -ne $target) {
    $arguments += $target
}
$arguments += $DuneArguments
$arguments += @('-j', '1', '--display=short')

$previousOpamJobs = $env:OPAMJOBS
$previousDuneJobs = $env:DUNEJOBS
$env:OPAMJOBS = '1'
$env:DUNEJOBS = '1'

Push-Location -LiteralPath $PSScriptRoot
try {
    & $opam.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "OCaml $Workspace tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
    if ($null -eq $previousOpamJobs) {
        Remove-Item Env:OPAMJOBS -ErrorAction SilentlyContinue
    }
    else {
        $env:OPAMJOBS = $previousOpamJobs
    }
    if ($null -eq $previousDuneJobs) {
        Remove-Item Env:DUNEJOBS -ErrorAction SilentlyContinue
    }
    else {
        $env:DUNEJOBS = $previousDuneJobs
    }
}
