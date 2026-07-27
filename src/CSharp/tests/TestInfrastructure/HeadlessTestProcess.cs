// Shared support for the headless test process tests.

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Durable7.Tests.Infrastructure;

/// <summary>
/// Runs a test executable with the console and any dialogs suppressed, so a failing assertion
/// cannot block an unattended run.
/// </summary>
internal static class HeadlessTestProcess
{
    /// <summary>Gets the suppressed error mode mask.</summary>
    internal const uint SuppressedErrorModeMask = 0x0000_8003;
    /// <summary>Gets the wer always show ui.</summary>
    internal const uint WerAlwaysShowUi = 0x0000_0010;
    /// <summary>Gets the wer no ui.</summary>
    internal const uint WerNoUi = 0x0000_0020;

/// <summary>Prepares the state this run needs.</summary>
#pragma warning disable CA2255 // Test assemblies deliberately initialize process-wide failure handling before test code runs.
    [ModuleInitializer]
#pragma warning restore CA2255
    internal static void Initialize()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        uint previousErrorMode = GetErrorMode();
        SetErrorMode(previousErrorMode | SuppressedErrorModeMask);

        uint effectiveErrorMode = GetErrorMode();
        if ((effectiveErrorMode & SuppressedErrorModeMask) != SuppressedErrorModeMask)
        {
            throw new InvalidOperationException(
                $"Could not enable headless Windows process error handling. Observed error mode 0x{effectiveErrorMode:X8}.");
        }

        uint previousWerFlags = GetWerFlags();
        uint requestedWerFlags = (previousWerFlags & ~WerAlwaysShowUi) | WerNoUi;
        ThrowIfFailed(WerSetFlags(requestedWerFlags), nameof(WerSetFlags));

        uint effectiveWerFlags = GetWerFlags();
        if ((effectiveWerFlags & WerNoUi) == 0 || (effectiveWerFlags & WerAlwaysShowUi) != 0)
        {
            throw new InvalidOperationException(
                $"Could not disable Windows Error Reporting UI. Observed WER flags 0x{effectiveWerFlags:X8}.");
        }
    }

    /// <summary>Returns the current error mode.</summary>
    internal static uint GetCurrentErrorMode() => GetErrorMode();

    /// <summary>Returns the current wer flags.</summary>
    internal static uint GetCurrentWerFlags() => GetWerFlags();

    private static uint GetWerFlags()
    {
        ThrowIfFailed(WerGetFlags(GetCurrentProcess(), out uint flags), nameof(WerGetFlags));
        return flags;
    }

    private static void ThrowIfFailed(int hresult, string operation)
    {
        if (hresult != 0)
        {
            throw new InvalidOperationException(
                $"{operation} failed with HRESULT 0x{unchecked((uint)hresult):X8}.");
        }
    }

    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern uint GetErrorMode();

    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern uint SetErrorMode(uint mode);

    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern nint GetCurrentProcess();

    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern int WerGetFlags(nint process, out uint flags);

    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern int WerSetFlags(uint flags);
}
