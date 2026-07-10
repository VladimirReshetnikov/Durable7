# SPDX-License-Identifier: MIT-0

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string] $ExecutablePath,

    [Parameter(Position = 1, ValueFromRemainingArguments)]
    [string[]] $ArgumentList = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'Enable-HeadlessTestMode.ps1')
$null = Enable-HeadlessTestMode

& $ExecutablePath @ArgumentList
$processExitCode = $LASTEXITCODE
if ($null -eq $processExitCode) {
    throw "The test process '$ExecutablePath' did not report an exit code."
}

exit $processExitCode
