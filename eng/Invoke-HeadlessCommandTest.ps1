# SPDX-License-Identifier: MIT-0

<#
.SYNOPSIS
Runs a test command read from a file, with Windows error dialogs suppressed.

.DESCRIPTION
Same contract as Invoke-HeadlessTest.ps1, but the executable and its arguments come from a file --
one per line -- so a build system can hand over a command too long or too quoted to pass on a
command line.

.PARAMETER ArgumentFile
A file whose first line is the executable and whose remaining lines are its arguments.
#>


[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $ArgumentFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'Enable-HeadlessTestMode.ps1')
$null = Enable-HeadlessTestMode

[string[]] $command = [System.IO.File]::ReadAllLines($ArgumentFile)
if ($command.Count -eq 0 -or [string]::IsNullOrWhiteSpace($command[0])) {
    throw "The headless command argument file '$ArgumentFile' did not name an executable."
}

$executablePath = $command[0]
[string[]] $argumentList = if ($command.Count -gt 1) { $command[1..($command.Count - 1)] } else { @() }
& $executablePath @argumentList
$processExitCode = $LASTEXITCODE
if ($null -eq $processExitCode) {
    throw "The test process '$executablePath' did not report an exit code."
}

exit $processExitCode
