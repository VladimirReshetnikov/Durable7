# SPDX-License-Identifier: MIT-0

<#
.SYNOPSIS
Runs a test executable with Windows error dialogs suppressed.

.DESCRIPTION
Enables headless test mode, runs the executable, and propagates its exit code. A process that ends
without reporting an exit code is treated as a failure rather than silently passing.

.PARAMETER ExecutablePath
The test executable to run.

.PARAMETER ArgumentList
Arguments to pass to it. Remaining command-line arguments are collected here.
#>


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
