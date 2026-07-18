namespace Tools.DataStructures.FingerTree;

public sealed partial class PrioritySearchQueue<TKey, TPriority, TValue>
{
    /// <summary>Creates an immutable key-ordered gap cursor at a rank in <c>0..Count</c>.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> GetCursor(int position = 0) =>
        new(this, position);

    /// <summary>Creates a cursor before the first equivalent or greater key.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> GetCursorAtLowerBound(TKey key) =>
        new(this, CursorBoundRank(key, upper: false));

    /// <summary>Creates a cursor after the equivalent key, when present.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> GetCursorAtUpperBound(TKey key) =>
        new(this, CursorBoundRank(key, upper: true));

    /// <summary>Tries to locate a key; a miss returns its lower-bound cursor.</summary>
    public bool TryGetCursor(
        TKey key,
        out PrioritySearchQueueCursor<TKey, TPriority, TValue> cursor)
    {
        var position = CursorBoundRank(key, upper: false);
        cursor = new(this, position);
        return position < Count && KeyComparer.Compare(CursorEntryAt(position).Key, key) == 0;
    }

    /// <summary>Creates a key-order cursor before the cached priority winner, or at end when empty.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> GetCursorAtMinimumPriority() =>
        _root is null ? new(this, 0) : GetCursorAtLowerBound(_root.Winner.Key);

    private int CursorBoundRank(TKey key, bool upper)
    {
        var rank = 0;
        var node = _root;
        while (node is not null)
        {
            var comparison = KeyComparer.Compare(node.Entry.Key, key);
            if (comparison < 0 || (upper && comparison == 0))
            {
                rank = checked(rank + (node.Left?.Count ?? 0) + 1);
                node = node.Right;
            }
            else
            {
                node = node.Left;
            }
        }
        return rank;
    }

    internal PrioritySearchEntry<TKey, TPriority, TValue> CursorEntryAt(int rank)
    {
        if ((uint)rank >= (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(rank));
        var node = _root!;
        while (true)
        {
            var leftCount = node.Left?.Count ?? 0;
            if (rank < leftCount) { node = node.Left!; continue; }
            if (rank == leftCount) return node.Entry;
            rank -= leftCount + 1;
            node = node.Right!;
        }
    }
}

/// <summary>Immutable key-order root-plus-rank cursor over a priority-search queue.</summary>
public readonly struct PrioritySearchQueueCursor<TKey, TPriority, TValue>
{
    private readonly PrioritySearchQueue<TKey, TPriority, TValue>? _snapshot;

    internal PrioritySearchQueueCursor(
        PrioritySearchQueue<TKey, TPriority, TValue> snapshot,
        int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        Position = position;
    }

    private PrioritySearchQueue<TKey, TPriority, TValue> Value =>
        _snapshot ?? throw UninitializedError();

    /// <summary>Gets the entry count in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the number of key-ordered entries before the gap.</summary>
    public int Position { get; }
    /// <summary>Gets whether the gap is before every entry.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is after every entry.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the key-ordered entry immediately before the gap.</summary>
    public bool TryPeekPrevious(out PrioritySearchEntry<TKey, TPriority, TValue> entry)
    {
        if (Position == 0) { entry = default; return false; }
        entry = Value.CursorEntryAt(Position - 1);
        return true;
    }

    /// <summary>Attempts to read the key-ordered entry immediately after the gap.</summary>
    public bool TryPeekNext(out PrioritySearchEntry<TKey, TPriority, TValue> entry)
    {
        if (Position == Count) { entry = default; return false; }
        entry = Value.CursorEntryAt(Position);
        return true;
    }

    /// <summary>Moves the gap one entry toward the start.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The priority-search cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one entry toward the end.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The priority-search cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute key rank.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> SeekRank(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Strictly inserts an entry and returns the gap after it.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> Insert(
        TKey key,
        TPriority priority,
        TValue value)
    {
        var location = Value.GetCursorAtLowerBound(key);
        if (!Value.TryAdd(key, priority, value, out var snapshot))
            throw new ArgumentException("An entry with the same key already exists.", nameof(key));
        return new(snapshot, checked(location.Position + 1));
    }

    /// <summary>Attempts a strict insertion, returning the lower-bound cursor on collision.</summary>
    public bool TryInsert(
        TKey key,
        TPriority priority,
        TValue value,
        out PrioritySearchQueueCursor<TKey, TPriority, TValue> result)
    {
        var location = Value.GetCursorAtLowerBound(key);
        if (!Value.TryAdd(key, priority, value, out var snapshot))
        {
            result = location;
            return false;
        }
        result = new(snapshot, checked(location.Position + 1));
        return true;
    }

    /// <summary>Sets a keyed entry, updating an exact hit or inserting a miss.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> SetItem(
        TKey key,
        TPriority priority,
        TValue value)
    {
        var location = Value.GetCursorAtLowerBound(key);
        var hit = location.TryPeekNext(out var found) && Value.KeyComparer.Compare(found.Key, key) == 0;
        var snapshot = Value.SetItem(key, priority, value);
        return new(snapshot, hit ? location.Position : checked(location.Position + 1));
    }

    /// <summary>Updates the next entry while retaining its stored key and the current gap.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> SetNext(
        TPriority priority,
        TValue value)
    {
        if (!TryPeekNext(out var entry))
            throw new InvalidOperationException("The priority-search cursor has no next entry.");
        return new(Value.SetItem(entry.Key, priority, value), Position);
    }

    /// <summary>Deletes the entry immediately after the gap.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> DeleteNext()
    {
        if (!TryPeekNext(out var entry))
            throw new InvalidOperationException("The priority-search cursor has no next entry.");
        return new(Value.Remove(entry.Key), Position);
    }

    /// <summary>Deletes the entry immediately before the gap.</summary>
    public PrioritySearchQueueCursor<TKey, TPriority, TValue> DeletePrevious()
    {
        if (!TryPeekPrevious(out var entry))
            throw new InvalidOperationException("The priority-search cursor has no previous entry.");
        return new(Value.Remove(entry.Key), Position - 1);
    }

    /// <summary>Returns the exact comparer-preserving queue version represented by this cursor.</summary>
    public PrioritySearchQueue<TKey, TPriority, TValue> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized priority-search cursor.");
}
