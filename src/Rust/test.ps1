# SPDX-License-Identifier: MIT-0

[CmdletBinding()]
param(
    [ValidateSet('All', 'Hamt', 'FingerTree', 'Tungsten')]
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
    'Hamt' { @('-p', 'tools-data-structures-hamt') }
    'FingerTree' { @('-p', 'tools-data-structures-fingertree') }
    'Tungsten' { @('-p', 'tools-data-structures-tungsten') }
}

$arguments = @('test') + $selection
if ($Release) {
    $arguments += '--release'
}
$arguments += $CargoArguments

Push-Location -LiteralPath $PSScriptRoot
try {
    & $cargoPath @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Rust $Workspace tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
