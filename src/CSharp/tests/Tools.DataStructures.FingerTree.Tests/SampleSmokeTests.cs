using Tools.DataStructures.FingerTree.Editor;
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
        Assert.Contains("history position : 2 of 3", transcript);
        Assert.Contains("snapshot cadence : 16 cursor edits (16 before the first display)", transcript);
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
        Assert.Contains("hotfix(0), alert(1), build(2), deploy(3), report(4), backup(5)", transcript);   // Act 1
        // Act 2 (weighted sampling): expected % is deterministic from the weights; sampled % is deterministic
        // from the seeded RNG, so these also guard the cumulative-weight selection logic.
        Assert.Contains("apple  weight 5 ( 50.0% expected) -> sampled  50.1%", transcript);
        Assert.Contains("fig    weight 1 ( 10.0% expected) -> sampled   9.8%", transcript);
        // Act 3 (order statistics): deterministic integer results from the seeded set.
        Assert.Contains("min 15, median 56, max 97", transcript);
        Assert.Contains("rank of median 56  : index 7", transcript);
        Assert.Contains("|A union B| = 27,  |A intersect B| = 2", transcript);
        Assert.Contains("overlapping [2,4]", transcript);                                                  // Act 4
        Assert.Contains("[1,5], [3,8]", transcript);
        Assert.Contains("reversed : 8 7 6 5 4 3 2 1", transcript);                                         // Act 5 (O(1) reverse)
        Assert.Contains("40 -> forty", transcript);                                                       // Act 6 floor(50)
        Assert.Contains("55 -> fifty-five", transcript);                                                  // Act 6 ceiling(50)
        Assert.Contains("Done.", transcript);
    }

    /// <summary>The editor-extras tour runs to completion with the expected character/code-point/grapheme counts
    /// and offset-addressing results.</summary>
    [Fact]
    public void Editor_RunsAndProducesExpectedTranscript()
    {
        var writer = new StringWriter();

        EditorProgram.Run(writer);

        var transcript = writer.ToString();
        Assert.Contains("UTF-16 chars     : 29", transcript);                 // chars, code points, and graphemes differ
        Assert.Contains("code points      : 28", transcript);
        Assert.Contains("graphemes        : 25", transcript);
        Assert.Contains("newline style    : CrLf", transcript);
        Assert.Contains("char offset 3    : code point #3, grapheme #3", transcript);
        Assert.Contains("grapheme #8 at     : char offset 9", transcript);
        Assert.Contains("main after 16 edits: \"alpha\\nlocal beta 🙂 edited!\\n✓\\ngamma\"", transcript);
        Assert.Contains("cursor gap         : line 2, column 1", transcript);
        Assert.Contains("alternate branch   : \"alpha\\nalternate beta\\ngamma\"", transcript);
        Assert.Contains("retained original  : \"alpha\\nbeta\\ngamma\"", transcript);
        Assert.Contains("Done.", transcript);
    }
}
