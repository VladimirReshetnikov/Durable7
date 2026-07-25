namespace Durable7.Ordered;

public sealed partial class PersistentOrderedMultimap<TKey, TValue>
{
    /// <summary>Creates an immutable key-grouped pair gap cursor at a rank in <c>0..PairCount</c>.</summary>
    public PersistentOrderedMultimapCursor<TKey, TValue> GetCursor(long position = 0) =>
        new(this, position);

    /// <summary>Tries to focus a pair; a miss returns the append-position pair cursor.</summary>
    public bool TryGetCursor(
        TKey key,
        TValue value,
        out PersistentOrderedMultimapCursor<TKey, TValue> cursor)
    {
        var position = CursorIndexOf(key, value);
        cursor = new(this, position < 0 ? PairCount : position);
        return position >= 0;
    }

    /// <summary>Tries to focus the first pair of a key group; a miss returns the end cursor.</summary>
    public bool TryGetGroupCursor(
        TKey key,
        out PersistentOrderedMultimapCursor<TKey, TValue> cursor)
    {
        long position = 0;
        foreach (var pair in this)
        {
            if (KeyComparer.Equals(pair.Key, key))
            {
                cursor = new(this, position);
                return true;
            }
            position++;
        }
        cursor = new(this, PairCount);
        return false;
    }

    internal KeyValuePair<TKey, TValue> CursorEntryAt(long rank)
    {
        if (rank < 0 || rank >= PairCount)
            throw new ArgumentOutOfRangeException(nameof(rank));
        long position = 0;
        foreach (var pair in this)
        {
            if (position++ == rank)
                return pair;
        }
        throw new InvalidOperationException("The ordered-multimap pair count disagrees with its groups.");
    }

    internal long CursorIndexOf(TKey key, TValue value)
    {
        long position = 0;
        foreach (var pair in this)
        {
            if (KeyComparer.Equals(pair.Key, key) && ValueComparer.Equals(pair.Value, value))
                return position;
            position++;
        }
        return -1;
    }

    /// <summary>
    /// Gets the pair rank of the gap immediately after the last pair of an equivalent key group, or
    /// the end rank when no such group is present. Key groups are contiguous in the flattened
    /// enumeration, so this walks the leading pairs once and consults only <see cref="KeyComparer"/>.
    /// </summary>
    /// <remarks>
    /// <see cref="Add(TKey, TValue)"/> appends the value to the end of the key's group, or appends a
    /// fresh last group when the key is absent, so the following gap is a group boundary. Deriving it
    /// from the group boundary rather than re-scanning for the inserted pair by value keeps insertion
    /// total: a value that is not reflexive under <see cref="ValueComparer"/>, such as a
    /// <see cref="double.NaN"/>, is one the collection accepts but a content re-scan can never find
    /// again.
    /// </remarks>
    internal long CursorGroupEnd(TKey key)
    {
        long position = 0;
        var withinGroup = false;
        foreach (var pair in this)
        {
            if (KeyComparer.Equals(pair.Key, key))
                withinGroup = true;
            else if (withinGroup)
                return position;
            position++;
        }
        return position;
    }
}

/// <summary>Immutable root-plus-pair-rank gap cursor over a grouped persistent ordered multimap.</summary>
public readonly struct PersistentOrderedMultimapCursor<TKey, TValue>
{
    private readonly PersistentOrderedMultimap<TKey, TValue>? _snapshot;
    private readonly long _position;

    internal PersistentOrderedMultimapCursor(
        PersistentOrderedMultimap<TKey, TValue> snapshot,
        long position)
    {
        if (position < 0 || position > snapshot.PairCount)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private PersistentOrderedMultimap<TKey, TValue> Value =>
        _snapshot ?? throw UninitializedError();

    /// <summary>Gets the flattened key-grouped pair count in this cursor version.</summary>
    public long PairCount => Value.PairCount;
    /// <summary>Gets the number of key-grouped pairs before the gap.</summary>
    public long Position
    {
        get
        {
            _ = Value;
            return _position;
        }
    }
    /// <summary>Gets whether the gap precedes every pair.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap follows every pair.</summary>
    public bool IsAtEnd => Position == PairCount;

    /// <summary>Attempts to read the pair immediately before the gap.</summary>
    public bool TryPeekPrevious(out KeyValuePair<TKey, TValue> pair)
    {
        if (Position == 0) { pair = default; return false; }
        pair = Value.CursorEntryAt(Position - 1);
        return true;
    }

    /// <summary>Attempts to read the pair immediately after the gap.</summary>
    public bool TryPeekNext(out KeyValuePair<TKey, TValue> pair)
    {
        if (Position == PairCount) { pair = default; return false; }
        pair = Value.CursorEntryAt(Position);
        return true;
    }

    /// <summary>Moves the gap one key-grouped pair toward the start.</summary>
    public PersistentOrderedMultimapCursor<TKey, TValue> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The ordered-multimap cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one key-grouped pair toward the end.</summary>
    public PersistentOrderedMultimapCursor<TKey, TValue> MoveNext() => Position == PairCount
        ? throw new InvalidOperationException("The ordered-multimap cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute key-grouped pair rank.</summary>
    public PersistentOrderedMultimapCursor<TKey, TValue> Seek(long position) =>
        position == Position ? this : new(Value, position);

    /// <summary>
    /// Adds a pair using grouped collection semantics and returns its following pair gap. A present
    /// equivalent pair preserves this cursor; insertion is not forced into an unrelated group gap.
    /// </summary>
    /// <remarks>
    /// The following gap is derived from the key group's end via <typeparamref name="TKey"/> equality rather
    /// than by re-scanning for the inserted pair by value. A value that is not reflexive under the
    /// value policy, such as a <see cref="double.NaN"/>, is one the collection accepts but a content
    /// re-scan could never find again; the group-end derivation keeps this insertion total.
    /// </remarks>
    public PersistentOrderedMultimapCursor<TKey, TValue> Add(TKey key, TValue value)
    {
        var snapshot = Value.Add(key, value);
        if (ReferenceEquals(snapshot, Value))
            return this;
        return new(snapshot, snapshot.CursorGroupEnd(key));
    }

    /// <summary>Attempts grouped pair insertion without disturbing this cursor on equivalence.</summary>
    public bool TryAdd(
        TKey key,
        TValue value,
        out PersistentOrderedMultimapCursor<TKey, TValue> result)
    {
        result = Add(key, value);
        return !ReferenceEquals(result.Value, Value);
    }

    /// <summary>Deletes the pair immediately before the gap, contracting empty groups.</summary>
    /// <exception cref="InvalidOperationException">
    /// The gap is at the start, or the peeked pair is not reflexive under the value policy (such as a
    /// <see cref="double.NaN"/>) and could not be removed by content, which would otherwise report a
    /// deletion that removed nothing.
    /// </exception>
    public PersistentOrderedMultimapCursor<TKey, TValue> DeletePrevious()
    {
        if (!TryPeekPrevious(out var pair))
            throw new InvalidOperationException("The ordered-multimap cursor has no previous pair.");
        var snapshot = Value.Remove(pair.Key, pair.Value);
        if (snapshot.PairCount == PairCount)
            throw NonRemovablePairError();
        return new(snapshot, Position - 1);
    }

    /// <summary>Deletes the pair immediately after the gap, contracting empty groups.</summary>
    /// <exception cref="InvalidOperationException">
    /// The gap is at the end, or the peeked pair is not reflexive under the value policy (such as a
    /// <see cref="double.NaN"/>) and could not be removed by content, which would otherwise report a
    /// deletion that removed nothing.
    /// </exception>
    public PersistentOrderedMultimapCursor<TKey, TValue> DeleteNext()
    {
        if (!TryPeekNext(out var pair))
            throw new InvalidOperationException("The ordered-multimap cursor has no next pair.");
        var snapshot = Value.Remove(pair.Key, pair.Value);
        if (snapshot.PairCount == PairCount)
            throw NonRemovablePairError();
        return new(snapshot, Position);
    }

    /// <summary>Returns the exact persistent ordered-multimap version represented by this cursor.</summary>
    public PersistentOrderedMultimap<TKey, TValue> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized ordered-multimap cursor.");

    private static InvalidOperationException NonRemovablePairError() =>
        new("The peeked ordered-multimap pair is not reflexive under the value policy and could not be removed.");
}
