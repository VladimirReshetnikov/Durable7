using System.Text;
using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Editor;

/// <summary>
/// A runnable tour of the editor-grade text extras: build a document with <see cref="RopeBuilder"/>, then show how
/// UTF-16 characters, Unicode code points, and grapheme clusters differ, detect the newline style, read
/// carriage-return-stripped lines, convert between character offsets and code-point / grapheme indices, and
/// perform a sixteen-edit local measured-cursor burst with movement, deletion, coordinates, and branching.
/// Exposed as <see cref="Run"/> so a test can drive it with a captured writer.
/// </summary>
public static class EditorProgram
{
    /// <summary>Runs the five-act tour, writing a narrated transcript to <paramref name="output"/>.</summary>
    /// <param name="output">Destination for the transcript.</param>
    public static void Run(TextWriter output)
    {
        output.WriteLine("FingerTree editor extras: characters, code points, and graphemes");
        output.WriteLine("================================================================\n");

        // A document mixing an emoji (a surrogate pair), a decomposed accented letter (a base + a combining mark),
        // and Windows CRLF line endings -- so chars, code points, and graphemes all differ.
        var document = new RopeBuilder()
            .Append("Hi ")
            .Append(new Rune(0x1F600))   // an emoji: one code point, two UTF-16 chars
            .Append(" café")       // 'e' + a combining acute accent: one grapheme, two code points
            .Append("\r\n")
            .Append("second line")
            .Append("\r\n")
            .Append("end")
            .ToTextRope();

        output.WriteLine("Act 1 - Built with RopeBuilder");
        output.WriteLine($"  first line       : {document.GetLineText(0)}\n");

        output.WriteLine("Act 2 - Three different lengths");
        output.WriteLine($"  UTF-16 chars     : {document.Count}");
        output.WriteLine($"  code points      : {document.CodePointCount()}   (the emoji is one code point over two chars)");
        output.WriteLine($"  graphemes        : {document.GraphemeCount()}   (e + accent is one cluster; CRLF is one)\n");

        output.WriteLine("Act 3 - Newline style and CR-stripped lines");
        output.WriteLine($"  newline style    : {document.DetectNewlineStyle()}");
        var lineIndex = 0;
        foreach (var line in document.LinesText())
            output.WriteLine($"    line {lineIndex++}        : {line}");
        output.WriteLine();

        output.WriteLine("Act 4 - Offset addressing (char <-> code point <-> grapheme)");
        const int emojiCharOffset = 3;   // "Hi " then the emoji
        output.WriteLine(
            $"  char offset {emojiCharOffset}    : code point #{document.CharOffsetToCodePointIndex(emojiCharOffset)}, grapheme #{document.CharOffsetToGraphemeIndex(emojiCharOffset)}");
        const int accentGrapheme = 8;    // H i _ emoji _ c a f (e+accent): the accented-e cluster
        output.WriteLine(
            $"  grapheme #{accentGrapheme} at     : char offset {document.GraphemeIndexToCharOffset(accentGrapheme)} (the accented e)\n");

        output.WriteLine("Act 5 - Local measured-cursor editing and branching");
        var original = "alpha\nbeta\ngamma".ToTextRope();
        var branchPoint = original.GetCursor(original.OffsetOf(1, 0));
        var cursor = branchPoint.InsertRange("local ");                        // edit 1: localized insertion
        cursor = cursor.MoveNext().MoveNext().MoveNext().MoveNext();           // movement across "beta"
        cursor = cursor.DeletePrevious().Insert('a');                          // edits 2-3: delete and restore

        foreach (var character in " 🙂 edited!\n✓")
            cursor = cursor.Insert(character);                                // edits 4-16, including Unicode

        var (cursorLine, cursorColumn) = cursor.LineColumnOf();
        var main = cursor.Snapshot();                                          // measured cadence: one snapshot after 16 edits
        var alternate = branchPoint.InsertRange("alternate ").Snapshot();      // explicit branch-display boundary

        output.WriteLine($"  main after 16 edits: {Quote(main.AsString())}");
        output.WriteLine($"  cursor gap         : line {cursorLine}, column {cursorColumn}");
        output.WriteLine($"  alternate branch   : {Quote(alternate.AsString())}");
        output.WriteLine($"  retained original  : {Quote(original.AsString())}");
        output.WriteLine("  snapshot cadence   : 16 local edits; branch snapshot only at its display boundary\n");

        output.WriteLine("Done. Character, code-point, and grapheme addressing all agree on boundaries.");
    }

    private static string Quote(string text) => "\"" + text.Replace("\n", "\\n") + "\"";
}
