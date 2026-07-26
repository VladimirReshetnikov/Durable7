// Tests for the rope text.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies the <see cref="RopeText"/> text conveniences: string interop, the <see cref="NewlineMeasure"/> line
/// rope, line/column navigation against a <c>string.Split('\n')</c> model, the <see cref="System.IO.TextReader"/>
/// adapter, and editing-through-the-rope round trips.
/// </summary>
public sealed class RopeTextTests
{
    /// <summary>Representative texts: empty, no newlines, interior/leading/trailing newlines, and a blank line.</summary>
    public static TheoryData<string> Texts =>
    [
        "",
        "no newlines here",
        "a\nb\nc",
        "a\nbb\nccc",
        "trailing\n",
        "\nleading",
        "\n\n\n",
        "line0\nline1\n\nline3\n",
    ];

    /// <summary>Verifies string round-trips and the newline measure.</summary>
    [Theory]
    [MemberData(nameof(Texts))]
    public void StringInterop_RoundTrips(string text)
    {
        Assert.Equal(text, text.ToCharRope().AsString());
        var rope = text.ToTextRope();
        Assert.Equal(text, rope.AsString());
        Assert.Equal(text.Count(c => c == '\n'), rope.Measure);
        Assert.Equal(text.Split('\n').Length, rope.LineCount());
    }

    /// <summary>Verifies line/column navigation matches a brute-force string model at every offset and line.</summary>
    [Theory]
    [MemberData(nameof(Texts))]
    public void LineNavigation_MatchesStringModel(string text)
    {
        var rope = text.ToTextRope();
        var lines = text.Split('\n');

        // Line start offsets from the model: cumulative line lengths plus the newlines before each line.
        var starts = new int[lines.Length];
        var running = 0;
        for (var i = 0; i < lines.Length; i++)
        {
            starts[i] = running;
            running += lines[i].Length + 1;   // +1 for the newline that follows (past the end for the last line)
        }

        Assert.Equal(lines.Length, rope.LineCount());

        for (var line = 0; line < lines.Length; line++)
        {
            Assert.Equal(starts[line], rope.LineStartOffset(line));
            Assert.Equal(lines[line], rope.GetLine(line));
        }

        for (var offset = 0; offset <= text.Length; offset++)
        {
            var expectedLine = text[..offset].Count(c => c == '\n');
            Assert.Equal(expectedLine, rope.LineOfOffset(offset));

            var (line, column) = rope.LineColumnOf(offset);
            Assert.Equal(expectedLine, line);
            Assert.Equal(offset - starts[expectedLine], column);
            Assert.Equal(offset, rope.OffsetOf(line, column));   // round-trip position <-> offset
        }

        Assert.Equal(lines, rope.Lines().ToArray());
    }

    /// <summary>
    /// Verifies Lines and LinesText validate their rope argument at the call site rather than deferring the
    /// ArgumentNullException to the first enumeration of the returned sequence.
    /// </summary>
    [Fact]
    public void Lines_ValidatesEagerly()
    {
        MeasuredRope<char, int, NewlineMeasure> rope = null!;
        Assert.Throws<ArgumentNullException>(() => rope.Lines());
        Assert.Throws<ArgumentNullException>(() => rope.LinesText());
    }

    /// <summary>
    /// Verifies OffsetOf accepts columns up to the line length (the newline / end-of-rope position)
    /// and rejects columns past the line's end rather than silently addressing a later line.
    /// </summary>
    [Fact]
    public void OffsetOf_ValidatesColumnAgainstTheLineEnd()
    {
        var rope = "alpha\nbeta\n".ToTextRope();

        Assert.Equal(0, rope.OffsetOf(0, 0));
        Assert.Equal(5, rope.OffsetOf(0, 5));      // the newline terminating line 0
        Assert.Equal(8, rope.OffsetOf(1, 2));
        Assert.Equal(10, rope.OffsetOf(1, 4));     // the newline terminating line 1
        Assert.Equal(11, rope.OffsetOf(2, 0));     // trailing empty line, end of rope

        // Offsets 6..7 are on line 1; a column past line 0's end must not reach them.
        Assert.Throws<ArgumentOutOfRangeException>("column", () => rope.OffsetOf(0, 6));
        Assert.Throws<ArgumentOutOfRangeException>("column", () => rope.OffsetOf(0, 7));
        Assert.Throws<ArgumentOutOfRangeException>("column", () => rope.OffsetOf(2, 1));
        Assert.Throws<ArgumentOutOfRangeException>("column", () => rope.OffsetOf(0, -1));
    }

    /// <summary>Verifies the TextReader adapter reads exactly the rope's characters, with working Peek.</summary>
    [Theory]
    [MemberData(nameof(Texts))]
    public void TextReader_ReadsAllCharacters(string text)
    {
        using var reader = text.ToCharRope().AsTextReader();
        Assert.Equal(text, reader.ReadToEnd());

        using var peeking = text.ToTextRope().AsTextReader();
        var built = new System.Text.StringBuilder();
        int peeked;
        while ((peeked = peeking.Peek()) != -1)
        {
            Assert.Equal(peeked, peeking.Read());   // Peek then Read return the same character
            built.Append((char)peeked);
        }

        Assert.Equal(text, built.ToString());
    }

    /// <summary>Verifies editing through the text rope (insert/remove) round-trips to the expected string.</summary>
    [Fact]
    public void Editing_RoundTripsToString()
    {
        var rope = "hello world".ToTextRope();

        var inserted = rope.InsertRange(5, ",\nthere".AsSpan());
        Assert.Equal("hello,\nthere world", inserted.AsString());
        Assert.Equal(2, inserted.LineCount());
        Assert.Equal("hello,", inserted.GetLine(0));
        Assert.Equal("there world", inserted.GetLine(1));

        var removed = inserted.RemoveRange(5, 7);   // remove ",\nthere"
        Assert.Equal("hello world", removed.AsString());
        Assert.Equal(1, removed.LineCount());
    }

    /// <summary>Verifies a large, heavily-lined document supports O(log n) line lookups correctly.</summary>
    [Fact]
    public void LargeDocument_LineLookups()
    {
        var builder = new System.Text.StringBuilder();
        for (var i = 0; i < 50_000; i++)
            builder.Append("line ").Append(i).Append('\n');
        var text = builder.ToString();
        var rope = text.ToTextRope();

        Assert.Equal(50_001, rope.LineCount());   // 50,000 newlines + 1 trailing empty line
        Assert.Equal("line 0", rope.GetLine(0));
        Assert.Equal("line 31415", rope.GetLine(31415));
        Assert.Equal("line 49999", rope.GetLine(49999));
        Assert.Equal(string.Empty, rope.GetLine(50000));

        var offset = rope.OffsetOf(31415, 2);   // the '2' would be inside "line 31415"... column 2 is 'n'
        Assert.Equal('n', rope[offset]);
        Assert.Equal((31415, 2), rope.LineColumnOf(offset));
    }
}
