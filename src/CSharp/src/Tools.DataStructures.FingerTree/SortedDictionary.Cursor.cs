namespace Tools.DataStructures.FingerTree;

public sealed partial class SortedDictionary<TKey, TValue>
{
    /// <summary>Creates an immutable key-ordered gap cursor at a rank in <c>0..Count</c>.</summary>
    public SortedDictionaryCursor<TKey, TValue> GetCursor(int position = 0) => new(this, position);

    /// <summary>Creates a cursor before the first equivalent or greater key.</summary>
    public SortedDictionaryCursor<TKey, TValue> GetCursorAtLowerBound(TKey key) =>
        new(this, CountBelow(new KeyAtLeastPredicate<TKey>(_comparer, key)));

    /// <summary>Creates a cursor after the equivalent key, when present.</summary>
    public SortedDictionaryCursor<TKey, TValue> GetCursorAtUpperBound(TKey key) =>
        new(this, CountBelow(new KeyAbovePredicate<TKey>(_comparer, key)));

    /// <summary>Tries to locate a key; a miss returns its lower-bound cursor.</summary>
    public bool TryGetCursor(TKey key, out SortedDictionaryCursor<TKey, TValue> cursor)
    {
        var position = CountBelow(new KeyAtLeastPredicate<TKey>(_comparer, key));
        cursor = new(this, position);
        return position < Count && _comparer.Compare(EntryAt(position).Key, key) == 0;
    }
}

/// <summary>Immutable key-ordered root-plus-rank cursor over a persistent sorted dictionary.</summary>
public readonly struct SortedDictionaryCursor<TKey, TValue>
{
    private readonly SortedDictionary<TKey, TValue>? _snapshot;
    private readonly int _position;

    internal SortedDictionaryCursor(SortedDictionary<TKey, TValue> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private SortedDictionary<TKey, TValue> Value => _snapshot ?? throw UninitializedError();

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
    public SortedDictionaryCursor<TKey, TValue> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The sorted-dictionary cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one entry toward the end.</summary>
    public SortedDictionaryCursor<TKey, TValue> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The sorted-dictionary cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute key rank.</summary>
    public SortedDictionaryCursor<TKey, TValue> SeekRank(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Strictly inserts a key and returns the gap after it.</summary>
    public SortedDictionaryCursor<TKey, TValue> Insert(TKey key, TValue value)
    {
        var location = Value.GetCursorAtLowerBound(key);
        var snapshot = Value.Add(key, value);
        return new(snapshot, checked(location.Position + 1));
    }

    /// <summary>Attempts a strict insertion, returning the lower-bound cursor on collision.</summary>
    public bool TryInsert(
        TKey key,
        TValue value,
        out SortedDictionaryCursor<TKey, TValue> result)
    {
        var location = Value.GetCursorAtLowerBound(key);
        if (!Value.TryAdd(key, value, out var snapshot))
        {
            result = location;
            return false;
        }
        result = new(snapshot, checked(location.Position + 1));
        return true;
    }

    /// <summary>Sets a keyed entry, updating an exact hit or inserting a miss.</summary>
    public SortedDictionaryCursor<TKey, TValue> SetItem(TKey key, TValue value)
    {
        var location = Value.GetCursorAtLowerBound(key);
        var hit = location.TryPeekNext(out var found) && Value.Comparer.Compare(found.Key, key) == 0;
        var snapshot = Value.SetItem(key, value);
        return new(snapshot, hit ? location.Position : checked(location.Position + 1));
    }

    /// <summary>Updates the next value while retaining its stored key and the current gap.</summary>
    public SortedDictionaryCursor<TKey, TValue> SetNextValue(TValue value)
    {
        if (!TryPeekNext(out var entry))
            throw new InvalidOperationException("The sorted-dictionary cursor has no next entry.");
        return new(Value.SetItem(entry.Key, value), Position);
    }

    /// <summary>Deletes the entry immediately after the gap.</summary>
    public SortedDictionaryCursor<TKey, TValue> DeleteNext()
    {
        if (!TryPeekNext(out var entry))
            throw new InvalidOperationException("The sorted-dictionary cursor has no next entry.");
        return new(Value.Remove(entry.Key), Position);
    }

    /// <summary>Deletes the entry immediately before the gap.</summary>
    public SortedDictionaryCursor<TKey, TValue> DeletePrevious()
    {
        if (!TryPeekPrevious(out var entry))
            throw new InvalidOperationException("The sorted-dictionary cursor has no previous entry.");
        return new(Value.Remove(entry.Key), Position - 1);
    }

    /// <summary>Returns the exact persistent dictionary version represented by this cursor.</summary>
    public SortedDictionary<TKey, TValue> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized sorted-dictionary cursor.");
}
