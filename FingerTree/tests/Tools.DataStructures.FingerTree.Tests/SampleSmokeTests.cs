using Tools.DataStructures.FingerTree.Showcase;
using Tools.DataStructures.FingerTree.Tour;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Smoke tests for the runnable samples: drive each sample's <c>Run(TextWriter)</c> entry point with a captured
/// writer and assert it completes and produces the expected, deterministic transcript markers. This guards the
/// samples against silent runtime breakage (compile breakage is already caught by the solution build), without
/// depending on the timing-sensitive parts of their output.
/// </summary>
public sealed class SampleSmokeTests
{
    /// <summary>The text-buffer tour runs to completion with the expected undo/redo and line-navigation results.</summary>
    [Fact]
    public void Tour_RunsAndProducesExpectedTranscript()
    {
        var writer = new StringWriter();

        TourProgram.Run(writer);

        var transcript = writer.ToString();
        Assert.Contains("Act 1 - Undo/redo", transcript);
        Assert.Contains("hello,\\nbrave world", transcript);          // after the three edits
        Assert.Contains("redo             : \"hello world\"", transcript);
        Assert.Contains("offset 14        : line 1, column 3", transcript);
        Assert.Contains("GetLine(1)       : \"second line\"", transcript);
        Assert.Contains("OffsetOf(2, 0)   : 23", transcript);
        Assert.Contains("[writer] published 500 lines", transcript);  // the concurrency act completed
        Assert.Contains("Done.", transcript);
    }

    /// <summary>The measures showcase runs to completion with the expected priority, sampling, and interval results.</summary>
    [Fact]
    public void Showcase_RunsAndProducesExpectedTranscript()
    {
        var writer = new StringWriter();

        ShowcaseProgram.Run(writer);

        var transcript = writer.ToString();
        Assert.Contains("hotfix(0), alert(1), build(2), deploy(3), report(4), backup(5)", transcript);
        Assert.Contains("Act 2 - Weighted random sampling", transcript);
        Assert.Contains("overlapping [2,4]", transcript);
        Assert.Contains("[1,5], [3,8]", transcript);
        Assert.Contains("Done.", transcript);
    }
}
