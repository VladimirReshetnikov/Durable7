// Tests for the rope builder.

using System.Text;
using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Validates the fluent <see cref="RopeBuilder"/>: chained appends, line and rune appends, the two rope
/// outputs, and reset.</summary>
public sealed class RopeBuilderTests
{
    /// <summary>Chained appends (string, char, span, line, rune) build the expected text and both rope flavors.</summary>
    [Fact]
    public void FluentAppends_BuildExpectedText()
    {
        var builder = new RopeBuilder()
            .Append("hello")
            .Append(' ')
            .Append("world".AsSpan())
            .AppendLine()
            .Append(new Rune(0x1F600));   // an emoji, encoded as a surrogate pair

        const string expected = "hello world\n\U0001F600";
        Assert.Equal(expected, builder.ToString());
        Assert.Equal(expected.Length, builder.Length);
        Assert.Equal(expected, builder.ToRope().AsString());
        Assert.Equal(expected, builder.ToTextRope().AsString());
    }

    /// <summary>The text-rope output tracks line structure from AppendLine.</summary>
    [Fact]
    public void ToTextRope_TracksLineStructure()
    {
        var rope = new RopeBuilder().AppendLine("one").AppendLine("two").Append("three").ToTextRope();
        Assert.Equal(3, rope.LineCount());   // "one\n" + "two\n" + "three" => three lines
        Assert.Equal("two", rope.GetLine(1));
    }

    /// <summary>Appending a non-BMP rune emits a surrogate pair (two chars, one code point).</summary>
    [Fact]
    public void Append_Rune_EncodesSurrogatePair()
    {
        var rope = new RopeBuilder().Append(new Rune(0x1F600)).ToRope();
        Assert.Equal(2, rope.Count);
        Assert.Equal(1, rope.CodePointCount());
    }

    /// <summary>Clear resets the accumulated text.</summary>
    [Fact]
    public void Clear_ResetsBuilder()
    {
        var builder = new RopeBuilder().Append("discard me");
        builder.Clear();
        Assert.Equal(0, builder.Length);
        Assert.Equal(string.Empty, builder.ToString());
        Assert.Empty(builder.ToRope());
    }
}
