# SPDX-License-Identifier: MIT-0

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $MyInvocation.MyCommand.Path
$headless = Join-Path $workspace '..\..\eng\Enable-HeadlessTestMode.ps1'
. $headless
$null = Enable-HeadlessTestMode
$previousNpmMaxSockets = $env:npm_config_maxsockets
$previousCmakeParallelLevel = $env:CMAKE_BUILD_PARALLEL_LEVEL
$previousMakeFlags = $env:MAKEFLAGS
$env:npm_config_maxsockets = '1'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '1'
$env:MAKEFLAGS = '-j1'
Push-Location $workspace
try {
    npm ci
    if ($LASTEXITCODE -ne 0) { throw "npm ci failed with exit code $LASTEXITCODE." }
    npm run validate
    if ($LASTEXITCODE -ne 0) { throw "TypeScript validation failed with exit code $LASTEXITCODE." }
}
finally {
    Pop-Location
    if ($null -eq $previousNpmMaxSockets) {
        Remove-Item Env:npm_config_maxsockets -ErrorAction SilentlyContinue
    }
    else {
        $env:npm_config_maxsockets = $previousNpmMaxSockets
    }
    if ($null -eq $previousCmakeParallelLevel) {
        Remove-Item Env:CMAKE_BUILD_PARALLEL_LEVEL -ErrorAction SilentlyContinue
    }
    else {
        $env:CMAKE_BUILD_PARALLEL_LEVEL = $previousCmakeParallelLevel
    }
    if ($null -eq $previousMakeFlags) {
        Remove-Item Env:MAKEFLAGS -ErrorAction SilentlyContinue
    }
    else {
        $env:MAKEFLAGS = $previousMakeFlags
    }
}
