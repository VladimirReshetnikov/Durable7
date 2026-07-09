using System.Text;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// A newline measure for characters: each <c>'\n'</c> measures 1 and the rest measure 0, so a
/// <c>MeasuredRope&lt;char, int, NewlineMeasure&gt;</c> tracks its line-break count and supports O(log n)
/// offset↔line navigation. This is the ready-made measure behind <see cref="RopeText"/>'s text rope.
/// </summary>
public readonly struct NewlineMeasure : IMeasure<char, int>
{
    /// <inheritdoc/>
    public static int Empty => 0;

    /// <inheritdoc/>
    public static int Measure(char value) => value == '\n' ? 1 : 0;

    /// <inheritdoc/>
    public static int Combine(int left, int right) => left + right;
}

/// <summary>
/// Text-oriented conveniences over the general ropes: <see cref="string"/> interop, line/column navigation, and
/// a <see cref="TextReader"/> adapter. These are extension methods on <c>Rope&lt;char&gt;</c> and the line-aware
/// <c>MeasuredRope&lt;char, int, NewlineMeasure&gt;</c>, so the rope cores stay element-agnostic while text use is
/// ergonomic. The line-aware rope (built with <see cref="ToTextRope"/>) gives O(log n) line lookups; a plain
/// <c>Rope&lt;char&gt;</c> is fine for content that needs no line addressing.
/// </summary>
/// <remarks>
/// Line numbering is zero-based. A rope's line count is its newline count plus one, so an empty rope is a single
/// empty line and a rope ending in <c>'\n'</c> has a trailing empty line — the usual text-editor convention.
/// </remarks>
public static class RopeText
{
    /// <summary>Builds a positional character rope from a string. O(n).</summary>
    /// <param name="text">Source text.</param>
    /// <returns>A <see cref="Rope{T}"/> of the string's characters.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="text"/> is <see langword="null"/>.</exception>
    public static Rope<char> ToCharRope(this string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        return Rope<char>.Create(text.AsSpan());
    }

    /// <summary>Builds a line-aware text rope from a string. O(n).</summary>
    /// <param name="text">Source text.</param>
    /// <returns>A <see cref="MeasuredRope{T, TMeasure, TMeasureOps}"/> measured by newline count.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="text"/> is <see langword="null"/>.</exception>
    public static MeasuredRope<char, int, NewlineMeasure> ToTextRope(this string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        return MeasuredRope<char, int, NewlineMeasure>.Create(text.AsSpan());
    }

    /// <summary>Materializes a character rope to a string. O(n).</summary>
    /// <param name="rope">The rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static string AsString(this Rope<char> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return rope.Count == 0 ? string.Empty : string.Create(rope.Count, rope, static (span, r) => r.CopyTo(0, span));
    }

    /// <summary>Materializes a text rope to a string. O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static string AsString(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return rope.Count == 0 ? string.Empty : string.Create(rope.Count, rope, static (span, r) => r.CopyTo(0, span));
    }

    /// <summary>Reads a character rope as a forward-only <see cref="TextReader"/>. O(1) to create, O(n) to read.</summary>
    /// <param name="rope">The rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static TextReader AsTextReader(this Rope<char> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return new RopeTextReader(rope.GetEnumerator());
    }

    /// <summary>Reads a text rope as a forward-only <see cref="TextReader"/>. O(1) to create, O(n) to read.</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static TextReader AsTextReader(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return new RopeTextReader(rope.GetEnumerator());
    }

    /// <summary>Gets the number of lines (newline count plus one). O(1).</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static int LineCount(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return rope.Measure + 1;
    }

    /// <summary>Returns the zero-based line index containing the character at <paramref name="offset"/> (the
    /// number of newlines before it). O(log n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="offset">A character offset in <c>0 .. Count</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="offset"/> is outside <c>0 .. Count</c>.</exception>
    public static int LineOfOffset(this MeasuredRope<char, int, NewlineMeasure> rope, int offset)
    {
        ArgumentNullException.ThrowIfNull(rope);
        return rope.PrefixMeasure(offset);   // PrefixMeasure validates the offset range
    }

    /// <summary>Returns the character offset at which line <paramref name="line"/> begins. O(log n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="line">A zero-based line index in <c>0 .. LineCount - 1</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="line"/> is outside the valid range.</exception>
    public static int LineStartOffset(this MeasuredRope<char, int, NewlineMeasure> rope, int line)
    {
        ArgumentNullException.ThrowIfNull(rope);
        if (line < 0 || line > rope.Measure)
            throw new ArgumentOutOfRangeException(nameof(line), line, "Line is outside the rope's range.");
        if (line == 0)
            return 0;
        // The character after the `line`-th newline starts line `line`. The range guard above guarantees the
        // `line`-th newline exists, so the locate always succeeds; the throw documents that invariant.
        if (!rope.TryLocateByMeasure(new NewlineAtLeastPredicate(line), out var index, out _, out _))
            throw new InvalidOperationException("Inconsistent rope: the requested line's starting newline was not found.");
        return index + 1;
    }

    /// <summary>Returns the zero-based (line, column) position of the character at <paramref name="offset"/>. O(log n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="offset">A character offset in <c>0 .. Count</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="offset"/> is outside <c>0 .. Count</c>.</exception>
    public static (int Line, int Column) LineColumnOf(this MeasuredRope<char, int, NewlineMeasure> rope, int offset)
    {
        var line = rope.LineOfOffset(offset);
        return (line, offset - rope.LineStartOffset(line));
    }

    /// <summary>Returns the character offset of the given zero-based (line, column) position. O(log n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="line">Zero-based line index.</param>
    /// <param name="column">
    /// Zero-based column within the line, in <c>0 .. line length</c>; the line length itself addresses the
    /// position of the line's terminating newline (or the end of the rope for the last line).
    /// </param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException">The position is outside the rope.</exception>
    public static int OffsetOf(this MeasuredRope<char, int, NewlineMeasure> rope, int line, int column)
    {
        var start = rope.LineStartOffset(line);   // validates line
        var end = line < rope.Measure ? rope.LineStartOffset(line + 1) - 1 : rope.Count;
        if (column < 0 || (long)start + column > end)   // widen so a huge column cannot overflow the check
            throw new ArgumentOutOfRangeException(nameof(column), column, "Column is outside the line.");
        return start + column;
    }

    /// <summary>Returns the text of line <paramref name="line"/> (without its trailing newline). O(log n + line length).</summary>
    /// <param name="rope">The text rope.</param>
    /// <param name="line">A zero-based line index in <c>0 .. LineCount - 1</c>.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="line"/> is outside the valid range.</exception>
    public static string GetLine(this MeasuredRope<char, int, NewlineMeasure> rope, int line)
    {
        var start = rope.LineStartOffset(line);   // validates line
        var end = line < rope.Measure ? rope.LineStartOffset(line + 1) - 1 : rope.Count;
        var length = end - start;
        return length == 0
            ? string.Empty
            : string.Create(length, (rope, start), static (span, state) => state.rope.CopyTo(state.start, span));
    }

    /// <summary>Enumerates the lines of the rope (each without its trailing newline). O(n).</summary>
    /// <param name="rope">The text rope.</param>
    /// <exception cref="ArgumentNullException"><paramref name="rope"/> is <see langword="null"/>.</exception>
    public static IEnumerable<string> Lines(this MeasuredRope<char, int, NewlineMeasure> rope)
    {
        ArgumentNullException.ThrowIfNull(rope);
        var builder = new StringBuilder();
        foreach (var c in rope)
        {
            if (c == '\n')
            {
                yield return builder.ToString();
                builder.Clear();
            }
            else
            {
                builder.Append(c);
            }
        }

        yield return builder.ToString();   // the final line (always present, possibly empty)
    }

    /// <summary>A value-type predicate locating the first position whose accumulated newline count reaches a
    /// target line, so <see cref="LineStartOffset"/> navigates with no delegate allocation.</summary>
    private readonly struct NewlineAtLeastPredicate(int line) : IMeasurePredicate<int>
    {
        public bool Invoke(int newlines) => newlines >= line;
    }

    /// <summary>A forward-only <see cref="TextReader"/> over a character rope's enumerator.</summary>
    private sealed class RopeTextReader(IEnumerator<char> source) : TextReader
    {
        private int _peeked = -1;

        public override int Peek()
        {
            if (_peeked >= 0)
                return _peeked;
            if (source.MoveNext())
                _peeked = source.Current;
            return _peeked;
        }

        public override int Read()
        {
            if (_peeked >= 0)
            {
                var value = _peeked;
                _peeked = -1;
                return value;
            }

            return source.MoveNext() ? source.Current : -1;
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
                source.Dispose();
            base.Dispose(disposing);
        }
    }
}
