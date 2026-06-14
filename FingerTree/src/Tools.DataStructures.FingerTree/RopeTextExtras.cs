using System.Globalization;
using System.Text;

namespace Tools.DataStructures.FingerTree;

/// <summary>The newline convention(s) found in a character sequence, as reported by the
/// <c>DetectNewlineStyle</c> extensions of <see cref="RopeTextExtras"/>.</summary>
public enum NewlineStyle
{
    /// <summary>No line-break characters were found.</summary>
    None,

    /// <summary>Only line feeds (<c>\n</c>) — the Unix convention.</summary>
    Lf,

    /// <summary>Only carriage-return + line-feed pairs (<c>\r\n</c>) — the Windows convention.</summary>
    CrLf,

    /// <summary>Only lone carriage returns (<c>\r</c>) — the classic-Mac convention.</summary>
    Cr,

    /// <summary>A mixture of more than one convention.</summary>
    Mixed,
}

/// <summary>
/// Editor-grade text conveniences over the character ropes: Unicode scalar-value (code point) and
/// grapheme-cluster addressing, and carriage-return-aware line handling. These complement <see cref="RopeText"/>'s
/// line-feed-based navigation while keeping the rope cores element-agnostic — they live entirely in extension
/// methods over <c>Rope&lt;char&gt;</c> and the line-aware <c>MeasuredRope&lt;char, int, NewlineMeasure&gt;</c>.
/// </summary>
/// <remarks>
/// A rope stores UTF-16 code units (<see cref="char"/>). Code points combine surrogate pairs; grapheme clusters
/// (the "user-perceived characters" of Unicode UAX #29) combine code points such as a base letter and a combining
/// mark, or the regional-indicator pairs behind flag emoji. The grapheme operations materialize the rope to a
/// string because cluster boundaries are a global property; the code-point operations stream without materializing.
/// </remarks>
public static class RopeTextExtras
{
    // ---- Unicode scalar (code point) addressing -----------------------------------------------------

    /// <summary>Counts the Unicode scalar values (code points) in the rope, pairing surrogates. O(n).</summary>
    /// <param name="rope">The character rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static int CodePointCount(this Rope<char> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return CountCodePoints(rope);
    }

    /// <summary>Counts the Unicode scalar values (code points) in the text rope, pairing surrogates. O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static int CodePointCount(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return CountCodePoints(rope);
    }

    /// <summary>Enumerates the rope's Unicode scalar values (code points); an unpaired surrogate yields
    /// <see cref="Rune.ReplacementChar"/>. O(n), streaming.</summary>
    /// <param name="rope">The character rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static IEnumerable<Rune> EnumerateRunes(this Rope<char> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return Runes(rope);
    }

    /// <summary>Enumerates the text rope's Unicode scalar values (code points). O(n), streaming.</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static IEnumerable<Rune> EnumerateRunes(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return Runes(rope);
    }

    // ---- Grapheme-cluster addressing (materializes the string) --------------------------------------

    /// <summary>Counts the grapheme clusters (user-perceived characters) in the rope. Materializes the text. O(n).</summary>
    /// <param name="rope">The character rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static int GraphemeCount(this Rope<char> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return CountGraphemes(rope.AsString());
    }

    /// <summary>Counts the grapheme clusters in the text rope. Materializes the text. O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static int GraphemeCount(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return CountGraphemes(rope.AsString());
    }

    /// <summary>Enumerates the rope's grapheme clusters, each as a string. Materializes the text. O(n).</summary>
    /// <param name="rope">The character rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static IEnumerable<string> EnumerateGraphemes(this Rope<char> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return Graphemes(rope.AsString());
    }

    /// <summary>Enumerates the text rope's grapheme clusters, each as a string. Materializes the text. O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static IEnumerable<string> EnumerateGraphemes(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return Graphemes(rope.AsString());
    }

    // ---- Carriage-return-aware line handling --------------------------------------------------------

    /// <summary>Detects the newline convention(s) present in the rope. O(n).</summary>
    /// <param name="rope">The character rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static NewlineStyle DetectNewlineStyle(this Rope<char> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return Detect(rope);
    }

    /// <summary>Detects the newline convention(s) present in the text rope. O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static NewlineStyle DetectNewlineStyle(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return Detect(rope);
    }

    /// <summary>Returns the text of line <paramref name="line"/> with any trailing carriage return removed, so a
    /// CRLF-terminated line comes back without its <c>\r</c>. O(log n + line length).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="line">A zero-based line index in <c>0 .. LineCount - 1</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="line"/> is outside the valid range.</exception>
    public static string GetLineText(this MeasuredRope<char, int, NewlineMeasure> rope, int line)
    {
        var text = rope.GetLine(line);   // validates `line` and strips the trailing line feed
        return TrimCarriageReturn(text);
    }

    /// <summary>Enumerates the rope's lines, each with any trailing carriage return removed (CRLF-aware). O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static IEnumerable<string> LinesText(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        foreach (var line in rope.Lines())
            yield return TrimCarriageReturn(line);
    }

    // ---- shared cores -------------------------------------------------------------------------------

    private static int CountCodePoints(IEnumerable<char> characters)
    {
        var count = 0;
        var pendingHigh = false;
        foreach (var character in characters)
        {
            if (pendingHigh)
            {
                count++;                                     // the pending high surrogate is one code point
                pendingHigh = false;
                if (char.IsLowSurrogate(character))
                    continue;                                // ... consumed together with its low half
            }

            if (char.IsHighSurrogate(character))
            {
                pendingHigh = true;
                continue;
            }

            count++;
        }

        if (pendingHigh)
            count++;
        return count;
    }

    private static IEnumerable<Rune> Runes(IEnumerable<char> characters)
    {
        var high = '\0';
        var hasHigh = false;
        foreach (var character in characters)
        {
            if (hasHigh)
            {
                hasHigh = false;
                if (char.IsLowSurrogate(character))
                {
                    yield return new Rune(high, character);
                    continue;
                }

                yield return Rune.ReplacementChar;           // unpaired high surrogate
            }

            if (char.IsHighSurrogate(character))
            {
                high = character;
                hasHigh = true;
                continue;
            }

            yield return char.IsLowSurrogate(character) ? Rune.ReplacementChar : new Rune(character);
        }

        if (hasHigh)
            yield return Rune.ReplacementChar;               // trailing unpaired high surrogate
    }

    private static int CountGraphemes(string text)
    {
        var enumerator = StringInfo.GetTextElementEnumerator(text);
        var count = 0;
        while (enumerator.MoveNext())
            count++;
        return count;
    }

    private static IEnumerable<string> Graphemes(string text)
    {
        var enumerator = StringInfo.GetTextElementEnumerator(text);
        while (enumerator.MoveNext())
            yield return (string)enumerator.Current;
    }

    private static NewlineStyle Detect(IEnumerable<char> characters)
    {
        var seen = NewlineStyle.None;
        var pendingCr = false;
        foreach (var character in characters)
        {
            if (pendingCr)
            {
                pendingCr = false;
                if (character == '\n')
                {
                    seen = Combine(seen, NewlineStyle.CrLf);
                    continue;
                }

                seen = Combine(seen, NewlineStyle.Cr);       // the previous '\r' stood alone
            }

            if (character == '\r')
            {
                pendingCr = true;
                continue;
            }

            if (character == '\n')
                seen = Combine(seen, NewlineStyle.Lf);
        }

        if (pendingCr)
            seen = Combine(seen, NewlineStyle.Cr);
        return seen;
    }

    private static NewlineStyle Combine(NewlineStyle existing, NewlineStyle found) =>
        existing == NewlineStyle.None ? found : existing == found ? existing : NewlineStyle.Mixed;

    private static string TrimCarriageReturn(string line) =>
        line.Length > 0 && line[^1] == '\r' ? line[..^1] : line;
}
