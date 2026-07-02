using System.Text;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// A fluent builder for character ropes. Appends accumulate into a <see cref="StringBuilder"/> (which is highly
/// optimized for append-only growth), and <see cref="ToRope"/> / <see cref="ToTextRope"/> materialize the result
/// into a properly chunked rope in one O(n) pass. Use this to <em>construct</em> a large character sequence; use
/// the rope's own edit operations once you need to mutate or share it.
/// </summary>
/// <example>
/// <code>
/// var rope = new RopeBuilder()
///     .Append("hello")
///     .Append(' ')
///     .Append(new Rune(0x1F600))   // an emoji, encoded as a surrogate pair
///     .AppendLine()
///     .ToTextRope();
/// </code>
/// </example>
public sealed class RopeBuilder
{
    private readonly StringBuilder _builder = new();

    /// <summary>The number of UTF-16 characters accumulated so far.</summary>
    public int Length => _builder.Length;

    /// <summary>Appends a string (a <see langword="null"/> string appends nothing). Returns this builder.</summary>
    /// <param name="text">The text to append.</param>
    public RopeBuilder Append(string? text)
    {
        _builder.Append(text);
        return this;
    }

    /// <summary>Appends a single character. Returns this builder.</summary>
    /// <param name="value">The character to append.</param>
    public RopeBuilder Append(char value)
    {
        _builder.Append(value);
        return this;
    }

    /// <summary>Appends a span of characters. Returns this builder.</summary>
    /// <param name="value">The characters to append.</param>
    public RopeBuilder Append(ReadOnlySpan<char> value)
    {
        _builder.Append(value);
        return this;
    }

    /// <summary>Appends a Unicode scalar value (code point), encoding it as a surrogate pair when it lies outside
    /// the basic multilingual plane. Returns this builder.</summary>
    /// <param name="value">The Unicode scalar value to append.</param>
    public RopeBuilder Append(Rune value)
    {
        Span<char> utf16 = stackalloc char[2];
        var written = value.EncodeToUtf16(utf16);
        _builder.Append(utf16[..written]);
        return this;
    }

    /// <summary>Appends optional text followed by a line feed (<c>'\n'</c>). Returns this builder.</summary>
    /// <param name="text">Text to append before the line feed; omitted appends only the line feed.</param>
    public RopeBuilder AppendLine(string? text = null)
    {
        _builder.Append(text).Append('\n');
        return this;
    }

    /// <summary>Removes all accumulated characters. Returns this builder.</summary>
    public RopeBuilder Clear()
    {
        _builder.Clear();
        return this;
    }

    /// <summary>Builds a positional character rope from the accumulated text. O(n).</summary>
    public Rope<char> ToRope() => Rope<char>.Create(BuildArray());

    /// <summary>Builds a line-aware text rope (measured by newline count) from the accumulated text. O(n).</summary>
    public MeasuredRope<char, int, NewlineMeasure> ToTextRope() =>
        MeasuredRope<char, int, NewlineMeasure>.Create(BuildArray());

    /// <summary>Returns the accumulated text as a string.</summary>
    public override string ToString() => _builder.ToString();

    private char[] BuildArray()
    {
        var characters = new char[_builder.Length];
        _builder.CopyTo(0, characters, 0, _builder.Length);
        return characters;
    }
}
