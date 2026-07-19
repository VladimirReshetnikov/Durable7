namespace Tools.DataStructures.Ordered;

public sealed partial class PersistentOrderedMap<TKey, TValue>
{
    /// <summary>Creates an immutable explicit-order gap cursor at a position in <c>0..Count</c>.</summary>
    public PersistentOrderedMapCursor<TKey, TValue> GetCursor(int position = 0) => new(this, position);

    /// <summary>Tries to focus an equivalent key; a miss returns the append-position cursor.</summary>
    public bool TryGetCursor(TKey key, out PersistentOrderedMapCursor<TKey, TValue> cursor)
    {
        var position = IndexOfKey(key);
        cursor = new(this, position < 0 ? Count : position);
        return position >= 0;
    }
}

/// <summary>Immutable root-plus-position gap cursor over a persistent ordered map.</summary>
public readonly struct PersistentOrderedMapCursor<TKey, TValue>
{
    private readonly PersistentOrderedMap<TKey, TValue>? _snapshot;
    private readonly int _position;

    internal PersistentOrderedMapCursor(PersistentOrderedMap<TKey, TValue> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private PersistentOrderedMap<TKey, TValue> Value => _snapshot ?? throw UninitializedError();

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
    /// <summary>Gets whether the gap precedes every entry.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap follows every entry.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the entry immediately before the gap.</summary>
    public bool TryPeekPrevious(out KeyValuePair<TKey, TValue> entry)
    {
        if (Position == 0) { entry = default; return false; }
        entry = Value.EntryAt(Position - 1);
        return true;
    }

    /// <summary>Attempts to read the entry immediately after the gap.</summary>
    public bool TryPeekNext(out KeyValuePair<TKey, TValue> entry)
    {
        if (Position == Count) { entry = default; return false; }
        entry = Value.EntryAt(Position);
        return true;
    }

    /// <summary>Moves the gap one entry toward the start.</summary>
    public PersistentOrderedMapCursor<TKey, TValue> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The ordered-map cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one entry toward the end.</summary>
    public PersistentOrderedMapCursor<TKey, TValue> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The ordered-map cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute explicit-order position.</summary>
    public PersistentOrderedMapCursor<TKey, TValue> Seek(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Strictly inserts an absent entry at the gap and returns its following gap.</summary>
    public PersistentOrderedMapCursor<TKey, TValue> Insert(TKey key, TValue value) =>
        new(Value.Insert(Position, key, value), checked(Position + 1));

    /// <summary>Attempts insertion at the gap; a duplicate returns the cursor at its stored entry.</summary>
    public bool TryInsert(
        TKey key,
        TValue value,
        out PersistentOrderedMapCursor<TKey, TValue> result)
    {
        var existing = Value.IndexOfKey(key);
        if (existing >= 0)
        {
            result = new(Value, existing);
            return false;
        }
        result = Insert(key, value);
        return true;
    }

    /// <summary>Updates the next entry's value while preserving its key representative and gap.</summary>
    public PersistentOrderedMapCursor<TKey, TValue> SetNextValue(TValue value)
    {
        if (!TryPeekNext(out var entry))
            throw new InvalidOperationException("The ordered-map cursor has no next entry.");
        return new(Value.SetItem(entry.Key, value), Position);
    }

    /// <summary>Deletes the entry immediately before the gap.</summary>
    public PersistentOrderedMapCursor<TKey, TValue> DeletePrevious() => Position == 0
        ? throw new InvalidOperationException("The ordered-map cursor has no previous entry.")
        : new(Value.RemoveAt(Position - 1), Position - 1);

    /// <summary>Deletes the entry immediately after the gap.</summary>
    public PersistentOrderedMapCursor<TKey, TValue> DeleteNext() => Position == Count
        ? throw new InvalidOperationException("The ordered-map cursor has no next entry.")
        : new(Value.RemoveAt(Position), Position);

    /// <summary>Returns the exact persistent ordered-map version represented by this cursor.</summary>
    public PersistentOrderedMap<TKey, TValue> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized ordered-map cursor.");
}
