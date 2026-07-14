# SPDX-License-Identifier: MIT-0

[CmdletBinding()]
param(
    [ValidateSet('All', 'Hamt', 'FingerTree', 'Tungsten')]
    [string] $Workspace = 'All',

    [string[]] $CabalArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\..\eng\Enable-HeadlessTestMode.ps1')
$null = Enable-HeadlessTestMode

$target = switch ($Workspace) {
    'All' { 'all' }
    'Hamt' { 'hamt-test' }
    'FingerTree' { 'ft-test' }
    'Tungsten' { 'tools-data-structures-tungsten' }
}

$cabal = Get-Command cabal -ErrorAction Stop
Push-Location -LiteralPath $PSScriptRoot
try {
    # Force a single Cabal build job even when a caller profile or argument requests more.
    & $cabal.Source test $target @CabalArguments '--jobs=1'
    if ($LASTEXITCODE -ne 0) {
        throw "Haskell $Workspace tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
