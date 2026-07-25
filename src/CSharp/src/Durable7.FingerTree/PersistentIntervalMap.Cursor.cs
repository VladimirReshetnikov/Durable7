namespace Durable7.FingerTree;

internal readonly struct IntervalMapCountAboveCursorPredicate<TEndpoint>(int rank) :
    IMeasurePredicate<IntervalMapAnnotation<TEndpoint>>
{
    public bool Invoke(IntervalMapAnnotation<TEndpoint> measure) => measure.Count > rank;
}

internal readonly struct IntervalMapLastKeyAboveCursorPredicate<TEndpoint>(Interval<TEndpoint> target) :
    IMeasurePredicate<IntervalMapAnnotation<TEndpoint>>
{
    public bool Invoke(IntervalMapAnnotation<TEndpoint> measure)
    {
        if (!measure.LastInterval.HasValue)
            return false;
        var comparer = Comparer<TEndpoint>.Default;
        var low = comparer.Compare(measure.LastInterval.Value.Low, target.Low);
        return low > 0 || (low == 0 && comparer.Compare(measure.LastInterval.Value.High, target.High) > 0);
    }
}

public sealed partial class PersistentIntervalMap<TEndpoint, TValue>
{
    /// <summary>Creates an immutable interval-key-ordered gap cursor at a rank in <c>0..Count</c>.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> GetCursor(int position = 0) =>
        new(this, position);

    /// <summary>Creates a cursor before the first equivalent or greater complete interval key.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> GetCursorAtLowerBound(
        Interval<TEndpoint> interval) => new(this, CursorBoundRank(interval, upper: false));

    /// <summary>Creates a cursor after an equivalent complete interval key, when present.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> GetCursorAtUpperBound(
        Interval<TEndpoint> interval) => new(this, CursorBoundRank(interval, upper: true));

    /// <summary>Tries to locate a complete interval key; a miss returns its lower-bound cursor.</summary>
    public bool TryGetCursor(
        Interval<TEndpoint> interval,
        out PersistentIntervalMapCursor<TEndpoint, TValue> cursor)
    {
        ValidateInterval(interval, nameof(interval));
        var position = CursorBoundRank(interval, upper: false);
        cursor = new(this, position);
        return position < Count && CompareIntervals(CursorEntryAt(position).Key, interval) == 0;
    }

    /// <summary>Tries to locate the first entry whose closed interval overlaps a query.</summary>
    public bool TryGetOverlapCursor(
        Interval<TEndpoint> query,
        out PersistentIntervalMapCursor<TEndpoint, TValue> cursor)
    {
        ValidateInterval(query, nameof(query));
        return TryGetOverlapCursorFrom(0, query, out cursor);
    }

    /// <summary>Tries to locate the first entry whose interval contains a point.</summary>
    public bool TryGetContainingCursor(
        TEndpoint point,
        out PersistentIntervalMapCursor<TEndpoint, TValue> cursor) =>
        TryGetOverlapCursor(new(point, point), out cursor);

    internal int CursorBoundRank(Interval<TEndpoint> interval, bool upper)
    {
        ValidateInterval(interval, nameof(interval));
        if (upper)
        {
            _tree.TryLocate(new IntervalMapLastKeyAboveCursorPredicate<TEndpoint>(interval), out var before, out _);
            return before.Count;
        }
        _tree.TryLocate(new IntervalMapLastKeyAtLeast<TEndpoint>(interval), out var lowerBefore, out _);
        return lowerBefore.Count;
    }

    internal KeyValuePair<Interval<TEndpoint>, TValue> CursorEntryAt(int rank)
    {
        if ((uint)rank >= (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(rank));
        _tree.TryLocate(new IntervalMapCountAboveCursorPredicate<TEndpoint>(rank), out _, out var entry);
        return ToPair(entry);
    }

    internal bool TryGetOverlapCursorFrom(
        int start,
        Interval<TEndpoint> query,
        out PersistentIntervalMapCursor<TEndpoint, TValue> cursor)
    {
        ValidateInterval(query, nameof(query));
        if ((uint)start > (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(start));
        if (start == Count)
        {
            cursor = new(this, Count);
            return false;
        }
        var (_, suffix) = _tree.Split(new IntervalMapCountAboveCursorPredicate<TEndpoint>(start));
        if (suffix.TryLocate(
                new IntervalMapMaxHighAtLeast<TEndpoint>(query.Low),
                out var before,
                out var candidate)
            && Comparer<TEndpoint>.Default.Compare(candidate.Interval.Low, query.High) <= 0)
        {
            cursor = new(this, checked(start + before.Count));
            return true;
        }
        cursor = new(this, Count);
        return false;
    }
}

/// <summary>Immutable interval-key-ordered root-plus-rank cursor over a persistent interval map.</summary>
public readonly struct PersistentIntervalMapCursor<TEndpoint, TValue>
{
    private readonly PersistentIntervalMap<TEndpoint, TValue>? _snapshot;
    private readonly int _position;

    internal PersistentIntervalMapCursor(
        PersistentIntervalMap<TEndpoint, TValue> snapshot,
        int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private PersistentIntervalMap<TEndpoint, TValue> Value =>
        _snapshot ?? throw UninitializedError();

    /// <summary>Gets the entry count in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the number of entries before the gap.</summary>
    public int Position
    {
        get
        {
            _ = Value;
            return _position;
        }
    }
    /// <summary>Gets whether the gap is before every entry.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is after every entry.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the entry immediately before the gap.</summary>
    public bool TryPeekPrevious(out KeyValuePair<Interval<TEndpoint>, TValue> entry)
    {
        if (Position == 0) { entry = default; return false; }
        entry = Value.CursorEntryAt(Position - 1);
        return true;
    }

    /// <summary>Attempts to read the entry immediately after the gap.</summary>
    public bool TryPeekNext(out KeyValuePair<Interval<TEndpoint>, TValue> entry)
    {
        if (Position == Count) { entry = default; return false; }
        entry = Value.CursorEntryAt(Position);
        return true;
    }

    /// <summary>Moves the gap one entry toward the start.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The interval-map cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one entry toward the end.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The interval-map cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute interval-key rank.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> SeekRank(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Searches strictly after the currently focused next entry for another overlap.</summary>
    public bool TrySeekNextOverlap(
        Interval<TEndpoint> query,
        out PersistentIntervalMapCursor<TEndpoint, TValue> cursor)
    {
        var start = Position < Count ? Position + 1 : Count;
        return Value.TryGetOverlapCursorFrom(start, query, out cursor);
    }

    /// <summary>Strictly inserts an interval entry and returns the gap after it.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> Insert(
        Interval<TEndpoint> interval,
        TValue value)
    {
        var position = Value.CursorBoundRank(interval, upper: false);
        return new(Value.Add(interval, value), checked(position + 1));
    }

    /// <summary>Attempts a strict insertion, returning the lower-bound cursor on collision.</summary>
    public bool TryInsert(
        Interval<TEndpoint> interval,
        TValue value,
        out PersistentIntervalMapCursor<TEndpoint, TValue> result)
    {
        var position = Value.CursorBoundRank(interval, upper: false);
        if (!Value.TryAdd(interval, value, out var snapshot))
        {
            result = new(Value, position);
            return false;
        }
        result = new(snapshot, checked(position + 1));
        return true;
    }

    /// <summary>Updates the next payload while retaining its interval representative and gap.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> SetNextValue(TValue value)
    {
        if (!TryPeekNext(out var entry))
            throw new InvalidOperationException("The interval-map cursor has no next entry.");
        return new(Value.SetItem(entry.Key, value), Position);
    }

    /// <summary>Deletes the entry immediately after the gap.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> DeleteNext()
    {
        if (!TryPeekNext(out var entry))
            throw new InvalidOperationException("The interval-map cursor has no next entry.");
        return new(Value.Remove(entry.Key), Position);
    }

    /// <summary>Deletes the entry immediately before the gap.</summary>
    public PersistentIntervalMapCursor<TEndpoint, TValue> DeletePrevious()
    {
        if (!TryPeekPrevious(out var entry))
            throw new InvalidOperationException("The interval-map cursor has no previous entry.");
        return new(Value.Remove(entry.Key), Position - 1);
    }

    /// <summary>Returns the exact persistent interval-map version represented by this cursor.</summary>
    public PersistentIntervalMap<TEndpoint, TValue> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized interval-map cursor.");
}
