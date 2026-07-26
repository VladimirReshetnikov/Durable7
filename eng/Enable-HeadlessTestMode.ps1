# SPDX-License-Identifier: MIT-0

<#
.SYNOPSIS
Suppresses the Windows error dialogs a crashing test process would otherwise raise.

.DESCRIPTION
An unattended test run must fail by exit code, not by waiting on a modal dialog nobody will
dismiss. This dot-sourced helper defines Enable-HeadlessTestMode, which clears the Windows error
mode bits responsible for those dialogs and returns the previous state so a caller can restore it.
On a non-Windows host it reports that it did nothing and succeeds, so the same test scripts run
unchanged everywhere.

.OUTPUTS
A PSCustomObject with IsWindows, PreviousMode, EffectiveMode and SuppressedModeMask.
#>


function Enable-HeadlessTestMode {
    [CmdletBinding()]
    param()

    $isWindowsHost = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
        [System.Runtime.InteropServices.OSPlatform]::Windows)
    if (-not $isWindowsHost) {
        return [pscustomobject]@{
            IsWindows = $false
            PreviousMode = [uint32]0
            EffectiveMode = [uint32]0
            SuppressedModeMask = [uint32]0
        }
    }

    $nativeMethods = 'Durable7.Testing.WindowsErrorMode' -as [type]
    if ($null -eq $nativeMethods) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Durable7.Testing
{
    public static class WindowsErrorMode
    {
        [DllImport("kernel32.dll", ExactSpelling = true)]
        public static extern uint GetErrorMode();

        [DllImport("kernel32.dll", ExactSpelling = true)]
        public static extern uint SetErrorMode(uint mode);
    }
}
'@
        $nativeMethods = 'Durable7.Testing.WindowsErrorMode' -as [type]
    }

    # These process flags are inherited by child processes. In particular,
    # SEM_NOGPFAULTERRORBOX keeps Windows Error Reporting and loader failures
    # from displaying modal UI before a language runtime can enter its main.
    [uint32]$suppressedModeMask = 0x00000001 -bor # SEM_FAILCRITICALERRORS
        0x00000002 -bor                          # SEM_NOGPFAULTERRORBOX
        0x00008000                               # SEM_NOOPENFILEERRORBOX
    [uint32]$previousMode = $nativeMethods::GetErrorMode()
    [uint32]$requestedMode = $previousMode -bor $suppressedModeMask

    if ($requestedMode -ne $previousMode) {
        $null = $nativeMethods::SetErrorMode($requestedMode)
    }

    [uint32]$effectiveMode = $nativeMethods::GetErrorMode()
    if (($effectiveMode -band $suppressedModeMask) -ne $suppressedModeMask) {
        throw ("Failed to enable non-interactive Windows error handling. " +
            "Requested error-mode mask 0x{0:X8}, observed 0x{1:X8}." -f
            $suppressedModeMask, $effectiveMode)
    }

    return [pscustomobject]@{
        IsWindows = $true
        PreviousMode = $previousMode
        EffectiveMode = $effectiveMode
        SuppressedModeMask = $suppressedModeMask
    }
}
