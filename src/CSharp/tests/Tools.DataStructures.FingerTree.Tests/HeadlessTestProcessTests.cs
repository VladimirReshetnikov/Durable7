using DataStructures.Tests.Infrastructure;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Verifies the managed test host cannot display Windows failure-reporting dialogs.</summary>
public sealed class HeadlessTestProcessTests
{
    /// <summary>Verifies both inherited process error handling and per-process WER UI suppression.</summary>
    [Fact]
    public void TestHostDisablesWindowsFailureDialogs()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        uint errorMode = HeadlessTestProcess.GetCurrentErrorMode();
        Assert.Equal(
            HeadlessTestProcess.SuppressedErrorModeMask,
            errorMode & HeadlessTestProcess.SuppressedErrorModeMask);

        uint werFlags = HeadlessTestProcess.GetCurrentWerFlags();
        Assert.Equal(HeadlessTestProcess.WerNoUi, werFlags & HeadlessTestProcess.WerNoUi);
        Assert.Equal(0u, werFlags & HeadlessTestProcess.WerAlwaysShowUi);
    }
}
