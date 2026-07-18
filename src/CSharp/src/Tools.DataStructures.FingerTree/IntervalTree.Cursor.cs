namespace Tools.DataStructures.FingerTree;

internal readonly struct IntervalCountAboveCursorPredicate<T>(int rank) :
    IMeasurePredicate<IntervalAnnotation<T>>
{
    public bool Invoke(IntervalAnnotation<T> measure) => measure.Count > rank;
}

public sealed partial class IntervalTree<T>
{
    /// <summary>Creates an immutable low-ordered gap cursor at a rank in <c>0..Count</c>.</summary>
    public IntervalTreeCursor<T> GetCursor(int position = 0) => new(this, position);

    /// <summary>Creates a cursor before the first interval whose low endpoint is at least <paramref name="low"/>.</summary>
    public IntervalTreeCursor<T> GetCursorAtLowerBound(T low) =>
        new(this, CursorLowBoundRank(low, upper: false));

    /// <summary>Creates a cursor after all intervals whose low endpoint equals <paramref name="low"/>.</summary>
    public IntervalTreeCursor<T> GetCursorAtUpperBound(T low) =>
        new(this, CursorLowBoundRank(low, upper: true));

    /// <summary>Tries to locate the first endpoint-equivalent occurrence.</summary>
    public bool TryGetCursor(Interval<T> interval, out IntervalTreeCursor<T> cursor)
    {
        var comparer = Comparer<T>.Default;
        var position = CursorLowBoundRank(interval.Low, upper: false);
        while (position < Count)
        {
            var candidate = CursorEntryAt(position);
            if (comparer.Compare(candidate.Low, interval.Low) != 0)
                break;
            if (comparer.Compare(candidate.High, interval.High) == 0)
            {
                cursor = new(this, position);
                return true;
            }
            position++;
        }
        cursor = new(this, CursorLowBoundRank(interval.Low, upper: false));
        return false;
    }

    /// <summary>Tries to locate the first interval overlapping a closed query.</summary>
    public bool TryGetOverlapCursor(Interval<T> query, out IntervalTreeCursor<T> cursor) =>
        TryGetOverlapCursorFrom(0, query, out cursor);

    /// <summary>Tries to locate the first interval containing a point.</summary>
    public bool TryGetContainingCursor(T point, out IntervalTreeCursor<T> cursor) =>
        TryGetOverlapCursor(new(point, point), out cursor);

    internal int CursorLowBoundRank(T low, bool upper)
    {
        var comparer = Comparer<T>.Default;
        if (upper)
        {
            _tree.TryLocate(new LastLowAbovePredicate<T>(comparer, low), out var before, out _);
            return before.Count;
        }
        _tree.TryLocate(new LastLowAtLeastPredicate<T>(comparer, low), out var lowerBefore, out _);
        return lowerBefore.Count;
    }

    internal Interval<T> CursorEntryAt(int rank)
    {
        if ((uint)rank >= (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(rank));
        _tree.TryLocate(new IntervalCountAboveCursorPredicate<T>(rank), out _, out var interval);
        return interval;
    }

    internal IntervalTree<T> RemoveAtCursorRank(int rank)
    {
        var (before, atOrAfter) = _tree.Split(new IntervalCountAboveCursorPredicate<T>(rank));
        if (!atOrAfter.TryViewLeft(out _, out var after))
            throw new ArgumentOutOfRangeException(nameof(rank));
        var tree = before.Concat(after);
        return tree.IsEmpty ? Empty : new(tree);
    }

    internal bool TryGetOverlapCursorFrom(
        int start,
        Interval<T> query,
        out IntervalTreeCursor<T> cursor)
    {
        if ((uint)start > (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(start));
        if (start == Count)
        {
            cursor = new(this, Count);
            return false;
        }

        var (_, suffix) = _tree.Split(new IntervalCountAboveCursorPredicate<T>(start));
        if (suffix.TryLocate(
                new MaxHighAtLeastPredicate<T>(Comparer<T>.Default, query.Low),
                out var before,
                out var candidate)
            && Comparer<T>.Default.Compare(candidate.Low, query.High) <= 0)
        {
            cursor = new(this, checked(start + before.Count));
            return true;
        }
        cursor = new(this, Count);
        return false;
    }
}

/// <summary>Immutable low-endpoint-ordered root-plus-rank cursor over an interval tree.</summary>
public readonly struct IntervalTreeCursor<T>
{
    private readonly IntervalTree<T>? _snapshot;

    internal IntervalTreeCursor(IntervalTree<T> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        Position = position;
    }

    private IntervalTree<T> Value => _snapshot ?? throw UninitializedError();

    /// <summary>Gets the occurrence count in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the number of low-ordered occurrences before the gap.</summary>
    public int Position { get; }
    /// <summary>Gets whether the gap is before every occurrence.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is after every occurrence.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the occurrence immediately before the gap.</summary>
    public bool TryPeekPrevious(out Interval<T> interval)
    {
        if (Position == 0) { interval = default; return false; }
        interval = Value.CursorEntryAt(Position - 1);
        return true;
    }

    /// <summary>Attempts to read the occurrence immediately after the gap.</summary>
    public bool TryPeekNext(out Interval<T> interval)
    {
        if (Position == Count) { interval = default; return false; }
        interval = Value.CursorEntryAt(Position);
        return true;
    }

    /// <summary>Moves the gap one occurrence toward the start.</summary>
    public IntervalTreeCursor<T> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The interval-tree cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one occurrence toward the end.</summary>
    public IntervalTreeCursor<T> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The interval-tree cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute occurrence rank.</summary>
    public IntervalTreeCursor<T> SeekRank(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Searches strictly after the currently focused next occurrence.</summary>
    public bool TrySeekNextOverlap(Interval<T> query, out IntervalTreeCursor<T> cursor) =>
        Value.TryGetOverlapCursorFrom(Position < Count ? Position + 1 : Count, query, out cursor);

    /// <summary>Uses the ordinary equal-low placement rule and returns the gap after the new occurrence.</summary>
    public IntervalTreeCursor<T> Insert(Interval<T> interval)
    {
        var position = Value.CursorLowBoundRank(interval.Low, upper: false);
        return new(Value.Insert(interval), checked(position + 1));
    }

    /// <summary>Deletes the exact occurrence immediately after the gap.</summary>
    public IntervalTreeCursor<T> DeleteNext() => Position == Count
        ? throw new InvalidOperationException("The interval-tree cursor has no next occurrence.")
        : new(Value.RemoveAtCursorRank(Position), Position);

    /// <summary>Deletes the exact occurrence immediately before the gap.</summary>
    public IntervalTreeCursor<T> DeletePrevious() => Position == 0
        ? throw new InvalidOperationException("The interval-tree cursor has no previous occurrence.")
        : new(Value.RemoveAtCursorRank(Position - 1), Position - 1);

    /// <summary>Returns the exact persistent interval-tree version represented by this cursor.</summary>
    public IntervalTree<T> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized interval-tree cursor.");
}
