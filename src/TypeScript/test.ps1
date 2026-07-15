# SPDX-License-Identifier: MIT-0

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $MyInvocation.MyCommand.Path
$headless = Join-Path $workspace '..\..\eng\Enable-HeadlessTestMode.ps1'
. $headless
$null = Enable-HeadlessTestMode
Push-Location $workspace
try {
    npm ci
    if ($LASTEXITCODE -ne 0) { throw "npm ci failed with exit code $LASTEXITCODE." }
    npm run validate
    if ($LASTEXITCODE -ne 0) { throw "TypeScript validation failed with exit code $LASTEXITCODE." }
}
finally {
    Pop-Location
}
