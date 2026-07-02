using System.Text;
using CsCheck;
using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Validates the editor-grade text extras: Unicode scalar (code point) and grapheme-cluster addressing, and
/// carriage-return-aware line handling. Code-point and rune results are cross-checked against the BCL's
/// <c>string.EnumerateRunes()</c> (including ill-formed surrogate sequences); grapheme and newline results are
/// checked against explicit expectations.
/// </summary>
public sealed class RopeTextExtrasTests
{
    /// <summary>Code-point count pairs surrogates and matches the BCL rune count.</summary>
    [Theory]
    [InlineData("", 0)]
    [InlineData("abc", 3)]
    [InlineData("ábc", 4)]        // a + combining acute + b + c: 4 code points, 4 chars
    [InlineData("x\U0001F600y", 3)]     // x + emoji (surrogate pair) + y: 3 code points, 4 chars
    public void CodePointCount_PairsSurrogates(string text, int expected)
    {
        var rope = Rope<char>.Create(text.AsSpan());
        Assert.Equal(expected, rope.CodePointCount());
        Assert.Equal(text.EnumerateRunes().Count(), rope.CodePointCount());
    }

    /// <summary>Rune enumeration matches <c>string.EnumerateRunes()</c>, including unpaired-surrogate substitution.</summary>
    [Theory]
    [InlineData("")]
    [InlineData("hello world")]
    [InlineData("áb\U0001F600c𝄞")]   // combining, BMP, emoji, musical symbol
    [InlineData("lone high \uD834 then text")]         // unpaired high surrogate (mid-string)
    [InlineData("\uDD1E lone low then text")]          // unpaired low surrogate
    [InlineData("ends with high \uD834")]              // trailing unpaired high surrogate
    public void EnumerateRunes_MatchesBcl(string text)
    {
        var rope = Rope<char>.Create(text.AsSpan());
        Assert.Equal(text.EnumerateRunes().ToArray(), rope.EnumerateRunes().ToArray());
    }

    /// <summary>Over randomly generated UTF-16 sequences (including ill-formed surrogates), rune enumeration and
    /// code-point counting agree with the BCL.</summary>
    [Fact]
    public void EnumerateRunes_MatchesBcl_OverRandomSequences()
    {
        Gen.Char.Array[0, 40].Select(chars => new string(chars)).Sample(text =>
        {
            var rope = Rope<char>.Create(text.AsSpan());
            Assert.Equal(text.EnumerateRunes().ToArray(), rope.EnumerateRunes().ToArray());
            Assert.Equal(text.EnumerateRunes().Count(), rope.CodePointCount());
        }, iter: 1000);
    }

    /// <summary>Grapheme count groups combining marks and surrogate-pair emoji into single clusters.</summary>
    [Theory]
    [InlineData("", 0)]
    [InlineData("abc", 3)]
    [InlineData("ábc", 3)]        // "a + combining acute" is one cluster
    [InlineData("x\U0001F600y", 3)]     // the emoji is one cluster
    public void GraphemeCount_GroupsClusters(string text, int expected)
    {
        var rope = Rope<char>.Create(text.AsSpan());
        Assert.Equal(expected, rope.GraphemeCount());
        Assert.Equal(rope.GraphemeCount(), rope.EnumerateGraphemes().Count());
    }

    /// <summary>Grapheme enumeration keeps a base character and its combining mark together.</summary>
    [Fact]
    public void EnumerateGraphemes_KeepsCombiningMarkWithBase()
    {
        var rope = "ábc".ToCharRope();
        Assert.Equal(["á", "b", "c"], rope.EnumerateGraphemes().ToArray());
    }

    /// <summary>Newline-style detection classifies LF, CRLF, CR, mixed, and none.</summary>
    [Theory]
    [InlineData("", NewlineStyle.None)]
    [InlineData("no breaks here", NewlineStyle.None)]
    [InlineData("a\nb\nc", NewlineStyle.Lf)]
    [InlineData("a\r\nb\r\nc", NewlineStyle.CrLf)]
    [InlineData("a\rb\rc", NewlineStyle.Cr)]
    [InlineData("a\r\nb\nc", NewlineStyle.Mixed)]
    [InlineData("a\nb\rc", NewlineStyle.Mixed)]
    [InlineData("ends with cr\r", NewlineStyle.Cr)]
    public void DetectNewlineStyle_Classifies(string text, NewlineStyle expected)
    {
        Assert.Equal(expected, text.ToCharRope().DetectNewlineStyle());
    }

    /// <summary>GetLineText strips a trailing carriage return; GetLine keeps it.</summary>
    [Fact]
    public void GetLineText_StripsTrailingCarriageReturn()
    {
        var rope = "first\r\nsecond\r\nthird".ToTextRope();
        Assert.Equal("first\r", rope.GetLine(0));   // GetLine keeps the carriage return
        Assert.Equal("first", rope.GetLineText(0));
        Assert.Equal("second", rope.GetLineText(1));
        Assert.Equal("third", rope.GetLineText(2));
    }

    /// <summary>LinesText strips trailing carriage returns from every line, for both CRLF and LF inputs.</summary>
    [Fact]
    public void LinesText_StripsCarriageReturns()
    {
        Assert.Equal(["a", "b", "c"], "a\r\nb\r\nc".ToTextRope().LinesText().ToArray());
        Assert.Equal(["a", "b", "c"], "a\nb\nc".ToTextRope().LinesText().ToArray());
    }

    /// <summary>The text-rope overloads agree with the character-rope overloads.</summary>
    [Fact]
    public void TextRopeOverloads_AgreeWithCharRopeOverloads()
    {
        const string text = "héllo \U0001F600\nworld\r\n!";
        var charRope = text.ToCharRope();
        var textRope = text.ToTextRope();
        Assert.Equal(charRope.CodePointCount(), textRope.CodePointCount());
        Assert.Equal(charRope.GraphemeCount(), textRope.GraphemeCount());
        Assert.Equal(charRope.EnumerateRunes().ToArray(), textRope.EnumerateRunes().ToArray());
        Assert.Equal(charRope.DetectNewlineStyle(), textRope.DetectNewlineStyle());
    }

    /// <summary>Code-point offset conversions round-trip on every boundary and map the endpoints.</summary>
    [Theory]
    [InlineData("")]
    [InlineData("plain ascii text")]
    [InlineData("á b \U0001F600 c")]
    [InlineData("\U0001D11E\U0001F600\U0001F601")]   // back-to-back surrogate pairs
    public void CodePointOffsetConversions_RoundTrip(string text)
    {
        var rope = text.ToCharRope();
        var count = rope.CodePointCount();
        Assert.Equal(0, rope.CodePointIndexToCharOffset(0));
        Assert.Equal(rope.Count, rope.CodePointIndexToCharOffset(count));
        for (var k = 0; k <= count; k++)
            Assert.Equal(k, rope.CharOffsetToCodePointIndex(rope.CodePointIndexToCharOffset(k)));
    }

    /// <summary>CodePointIndexToCharOffset returns the char offset of each code point's first UTF-16 unit.</summary>
    [Fact]
    public void CodePointIndexToCharOffset_LocatesSurrogatePairs()
    {
        var rope = "A\U0001D11EB".ToCharRope();   // A(0), pair(1..2), B(3)
        Assert.Equal(0, rope.CodePointIndexToCharOffset(0));
        Assert.Equal(1, rope.CodePointIndexToCharOffset(1));
        Assert.Equal(3, rope.CodePointIndexToCharOffset(2));
        Assert.Equal(4, rope.CodePointIndexToCharOffset(3));   // end
    }

    /// <summary>Grapheme offset conversions round-trip on every cluster boundary and map the endpoints.</summary>
    [Theory]
    [InlineData("")]
    [InlineData("abc")]
    [InlineData("áb̂c")]
    [InlineData("x\U0001F600y\U0001F1FA\U0001F1F8z")]   // emoji and a flag (two regional indicators = one cluster)
    public void GraphemeOffsetConversions_RoundTrip(string text)
    {
        var rope = text.ToCharRope();
        var count = rope.GraphemeCount();
        Assert.Equal(rope.Count, rope.GraphemeIndexToCharOffset(count));
        for (var k = 0; k <= count; k++)
            Assert.Equal(k, rope.CharOffsetToGraphemeIndex(rope.GraphemeIndexToCharOffset(k)));
    }

    /// <summary>Code-point offset conversions round-trip over randomly generated UTF-16 sequences.</summary>
    [Fact]
    public void CodePointOffsetConversions_RoundTrip_OverRandomSequences()
    {
        Gen.Char.Array[0, 40].Select(chars => new string(chars)).Sample(text =>
        {
            var rope = Rope<char>.Create(text.AsSpan());
            var count = rope.CodePointCount();
            Assert.Equal(rope.Count, rope.CodePointIndexToCharOffset(count));
            for (var k = 0; k <= count; k++)
                Assert.Equal(k, rope.CharOffsetToCodePointIndex(rope.CodePointIndexToCharOffset(k)));
        }, iter: 500);
    }

    /// <summary>Out-of-range indices and offsets throw.</summary>
    [Fact]
    public void OffsetConversions_ThrowOutOfRange()
    {
        var rope = "abc".ToCharRope();
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.CodePointIndexToCharOffset(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.CodePointIndexToCharOffset(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.CharOffsetToCodePointIndex(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.GraphemeIndexToCharOffset(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.CharOffsetToGraphemeIndex(4));
    }
}
