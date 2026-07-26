// Editor-grade text conveniences over the character ropes: Unicode scalar-value (code point) and
// grapheme-cluster addressing, and carriage-return-aware line handling.

using System.Globalization;
using System.Text;

namespace Durable7.FingerTree;

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
        // Validate eagerly: iterator bodies defer their guards to the first MoveNext.
        ArgumentNullException.ThrowIfNull(rope);
        return LinesTextIterator(rope);
    }

    private static IEnumerable<string> LinesTextIterator(MeasuredRope<char, int, NewlineMeasure> rope)
    {
        foreach (var line in rope.Lines())
            yield return TrimCarriageReturn(line);
    }

    // ---- Offset conversions (char offset <-> code-point / grapheme index) ---------------------------

    /// <summary>Returns the character offset at which the code point with the given zero-based index begins. An
    /// index equal to the code-point count returns <c>Count</c> (the end). O(n).</summary>
    /// <param name="rope">The character rope.</param>
    /// <param name="codePointIndex">A zero-based code-point index in <c>0 .. CodePointCount</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="codePointIndex"/> is out of range.</exception>
    public static int CodePointIndexToCharOffset(this Rope<char> rope, int codePointIndex)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return CodePointStartOffset(rope, codePointIndex);
    }

    /// <summary>Returns the character offset at which the code point with the given zero-based index begins. O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="codePointIndex">A zero-based code-point index in <c>0 .. CodePointCount</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="codePointIndex"/> is out of range.</exception>
    public static int CodePointIndexToCharOffset(this MeasuredRope<char, int, NewlineMeasure> rope, int codePointIndex)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return CodePointStartOffset(rope, codePointIndex);
    }

    /// <summary>Returns the code-point index for a character offset: the number of code points that begin before
    /// it. A code-point-boundary offset round-trips with
    /// <see cref="CodePointIndexToCharOffset(Rope{char}, int)"/>; an offset inside a surrogate pair counts that
    /// pair. O(n).</summary>
    /// <param name="rope">The character rope.</param>
    /// <param name="charOffset">A character offset in <c>0 .. Count</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="charOffset"/> is outside <c>0 .. Count</c>.</exception>
    public static int CharOffsetToCodePointIndex(this Rope<char> rope, int charOffset)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return CodePointIndexAtOffset(rope, charOffset, rope.Count);
    }

    /// <summary>Returns the code-point index for a character offset (the number of code points beginning before
    /// it). O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="charOffset">A character offset in <c>0 .. Count</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="charOffset"/> is outside <c>0 .. Count</c>.</exception>
    public static int CharOffsetToCodePointIndex(this MeasuredRope<char, int, NewlineMeasure> rope, int charOffset)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return CodePointIndexAtOffset(rope, charOffset, rope.Count);
    }

    /// <summary>Returns the character offset at which the grapheme cluster with the given zero-based index begins.
    /// An index equal to the grapheme count returns the length. Materializes the text. O(n).</summary>
    /// <param name="rope">The character rope.</param>
    /// <param name="graphemeIndex">A zero-based grapheme index in <c>0 .. GraphemeCount</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="graphemeIndex"/> is out of range.</exception>
    public static int GraphemeIndexToCharOffset(this Rope<char> rope, int graphemeIndex)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return GraphemeStartOffset(rope.AsString(), graphemeIndex);
    }

    /// <summary>Returns the character offset at which the grapheme cluster with the given zero-based index begins.
    /// Materializes the text. O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="graphemeIndex">A zero-based grapheme index in <c>0 .. GraphemeCount</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="graphemeIndex"/> is out of range.</exception>
    public static int GraphemeIndexToCharOffset(this MeasuredRope<char, int, NewlineMeasure> rope, int graphemeIndex)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return GraphemeStartOffset(rope.AsString(), graphemeIndex);
    }

    /// <summary>Returns the grapheme-cluster index for a character offset (the number of clusters beginning before
    /// it). Materializes the text. O(n).</summary>
    /// <param name="rope">The character rope.</param>
    /// <param name="charOffset">A character offset in <c>0 .. Count</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="charOffset"/> is outside <c>0 .. Count</c>.</exception>
    public static int CharOffsetToGraphemeIndex(this Rope<char> rope, int charOffset)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return GraphemeIndexAtOffset(rope.AsString(), charOffset);
    }

    /// <summary>Returns the grapheme-cluster index for a character offset. Materializes the text. O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="charOffset">A character offset in <c>0 .. Count</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="charOffset"/> is outside <c>0 .. Count</c>.</exception>
    public static int CharOffsetToGraphemeIndex(this MeasuredRope<char, int, NewlineMeasure> rope, int charOffset)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return GraphemeIndexAtOffset(rope.AsString(), charOffset);
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

    private static int CodePointStartOffset(IEnumerable<char> characters, int codePointIndex)
    {
        if (codePointIndex < 0)
            throw new ArgumentOutOfRangeException(nameof(codePointIndex), codePointIndex, "Code-point index is negative.");

        var position = 0;
        var index = 0;
        var prevHigh = false;
        foreach (var character in characters)
        {
            if (!(prevHigh && char.IsLowSurrogate(character)))   // not the low half of a pair: a code point starts here
            {
                if (index == codePointIndex)
                    return position;
                index++;
            }

            position++;
            prevHigh = char.IsHighSurrogate(character);
        }

        if (index == codePointIndex)
            return position;   // the end boundary (codePointIndex == CodePointCount)
        throw new ArgumentOutOfRangeException(nameof(codePointIndex), codePointIndex, "Code-point index is past the end of the rope.");
    }

    private static int CodePointIndexAtOffset(IEnumerable<char> characters, int charOffset, int charCount)
    {
        if ((uint)charOffset > (uint)charCount)
            throw new ArgumentOutOfRangeException(nameof(charOffset), charOffset, "Character offset is outside 0 .. Count.");

        var position = 0;
        var index = 0;
        var prevHigh = false;
        foreach (var character in characters)
        {
            if (position >= charOffset)
                return index;
            if (!(prevHigh && char.IsLowSurrogate(character)))
                index++;
            position++;
            prevHigh = char.IsHighSurrogate(character);
        }

        return index;   // charOffset == charCount
    }

    // The character offset at which each grapheme cluster begins, using the SAME segmentation as GraphemeCount /
    // EnumerateGraphemes (TextElementEnumerator), so counts and offsets are always consistent.
    private static int[] GraphemeStarts(string text)
    {
        var enumerator = StringInfo.GetTextElementEnumerator(text);
        var starts = new List<int>();
        while (enumerator.MoveNext())
            starts.Add(enumerator.ElementIndex);
        return [.. starts];
    }

    private static int GraphemeStartOffset(string text, int graphemeIndex)
    {
        var starts = GraphemeStarts(text);
        if ((uint)graphemeIndex > (uint)starts.Length)
            throw new ArgumentOutOfRangeException(nameof(graphemeIndex), graphemeIndex, "Grapheme index is outside 0 .. GraphemeCount.");
        return graphemeIndex < starts.Length ? starts[graphemeIndex] : text.Length;
    }

    private static int GraphemeIndexAtOffset(string text, int charOffset)
    {
        if ((uint)charOffset > (uint)text.Length)
            throw new ArgumentOutOfRangeException(nameof(charOffset), charOffset, "Character offset is outside 0 .. Count.");
        var found = Array.BinarySearch(GraphemeStarts(text), charOffset);
        return found >= 0 ? found : ~found;   // count of grapheme starts strictly before charOffset
    }
}
