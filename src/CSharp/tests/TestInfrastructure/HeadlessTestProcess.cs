using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DataStructures.Tests.Infrastructure;

internal static class HeadlessTestProcess
{
    internal const uint SuppressedErrorModeMask = 0x0000_8003;
    internal const uint WerAlwaysShowUi = 0x0000_0010;
    internal const uint WerNoUi = 0x0000_0020;

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

    internal static uint GetCurrentErrorMode() => GetErrorMode();

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
