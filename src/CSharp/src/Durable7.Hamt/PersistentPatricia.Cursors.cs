namespace Durable7.Hamt;

public sealed partial class PersistentIntMap<TValue>
{
    /// <summary>Creates an immutable ordered cursor at a rank gap. O(1).</summary>
    /// <param name="position">The number of entries before the gap, in <c>0 .. Count</c>.</param>
    /// <returns>A cursor over this map.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="position"/> is outside <c>0 .. Count</c>.</exception>
    public PersistentIntMapCursor<TValue> GetCursor(int position = 0)
    {
        PatriciaCursorGuard.ValidatePosition(position, Count);
        return new PersistentIntMapCursor<TValue>(this, position);
    }

    /// <summary>Creates an immutable ordered cursor after the final entry. O(1).</summary>
    /// <returns>A cursor at <see cref="Count"/>.</returns>
    public PersistentIntMapCursor<TValue> GetCursorAtEnd() => new(this, Count);

    /// <summary>Creates a cursor before the first entry whose key is not less than <paramref name="key"/>. O(32).</summary>
    /// <param name="key">The lower-bound key.</param>
    /// <returns>The lower-bound cursor.</returns>
    public PersistentIntMapCursor<TValue> GetLowerBoundCursor(int key) =>
        new(this, _core.GetLowerBoundRank(key, out _));

    /// <summary>Creates a cursor before the first entry whose key is greater than <paramref name="key"/>. O(32).</summary>
    /// <param name="key">The upper-bound key.</param>
    /// <returns>The upper-bound cursor.</returns>
    public PersistentIntMapCursor<TValue> GetUpperBoundCursor(int key)
    {
        var rank = _core.GetLowerBoundRank(key, out var found);
        return new PersistentIntMapCursor<TValue>(this, found ? checked(rank + 1) : rank);
    }

    /// <summary>Creates a lower-bound cursor and reports whether its next entry has <paramref name="key"/>. O(32).</summary>
    /// <param name="key">The exact key to seek.</param>
    /// <param name="found">Whether the key is present.</param>
    /// <returns>A usable lower-bound cursor for both hits and misses.</returns>
    public PersistentIntMapCursor<TValue> GetCursorAtKey(int key, out bool found)
    {
        var rank = _core.GetLowerBoundRank(key, out found);
        return new PersistentIntMapCursor<TValue>(this, rank);
    }

    internal bool TryGetAt(int index, out KeyValuePair<int, TValue> entry) => _core.TryGetAt(index, out entry);

    internal int GetLowerBoundRank(int key, out bool found) => _core.GetLowerBoundRank(key, out found);
}

/// <summary>
/// An immutable persistent cursor over a <see cref="PersistentIntMap{TValue}"/> in ascending signed-key order.
/// The cursor denotes a gap; its next entry is the focused search candidate.
/// </summary>
/// <typeparam name="TValue">The map value type.</typeparam>
/// <remarks>
/// This implementation is a canonical-root-plus-rank checkpoint. Movement returns new values over the same map;
/// edits path-copy through the owning map and return independent cursor versions. Peeks and edits are O(32), while
/// count, position, movement, seeking by rank, and snapshot are O(1). The default struct value is invalid.
/// </remarks>
public readonly struct PersistentIntMapCursor<TValue>
{
    private readonly PersistentIntMap<TValue>? _source;
    private readonly int _position;

    internal PersistentIntMapCursor(PersistentIntMap<TValue> source, int position)
    {
        _source = source;
        _position = position;
    }

    /// <summary>Gets the entry count. O(1).</summary>
    public int Count => Source.Count;

    /// <summary>Gets the number of entries before the gap. O(1).</summary>
    public int Position { get { _ = Source; return _position; } }

    /// <summary>Gets whether the gap precedes every entry.</summary>
    public bool IsAtStart => Position == 0;

    /// <summary>Gets whether the gap follows every entry.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the entry immediately before the gap. O(32).</summary>
    public bool TryPeekPrevious(out KeyValuePair<int, TValue> entry)
    {
        var source = Source;
        if (_position > 0)
            return source.TryGetAt(_position - 1, out entry);
        entry = default;
        return false;
    }

    /// <summary>Attempts to read the entry immediately after the gap. O(32).</summary>
    public bool TryPeekNext(out KeyValuePair<int, TValue> entry) =>
        Source.TryGetAt(_position, out entry);

    /// <summary>Moves the gap one entry toward the start. O(1).</summary>
    /// <exception cref="InvalidOperationException">The cursor is at the start or is uninitialized.</exception>
    public PersistentIntMapCursor<TValue> MovePrevious()
    {
        var source = Source;
        if (_position == 0)
            throw new InvalidOperationException("The cursor is already at the start.");
        return new PersistentIntMapCursor<TValue>(source, _position - 1);
    }

    /// <summary>Moves the gap one entry toward the end. O(1).</summary>
    /// <exception cref="InvalidOperationException">The cursor is at the end or is uninitialized.</exception>
    public PersistentIntMapCursor<TValue> MoveNext()
    {
        var source = Source;
        if (_position == source.Count)
            throw new InvalidOperationException("The cursor is already at the end.");
        return new PersistentIntMapCursor<TValue>(source, _position + 1);
    }

    /// <summary>Moves the gap to a rank in <c>0 .. Count</c>. O(1).</summary>
    public PersistentIntMapCursor<TValue> Seek(int position)
    {
        var source = Source;
        PatriciaCursorGuard.ValidatePosition(position, source.Count);
        return position == _position ? this : new PersistentIntMapCursor<TValue>(source, position);
    }

    /// <summary>Strictly inserts a missing key at its lower-bound gap and returns the gap after it. O(32).</summary>
    /// <exception cref="ArgumentException">The key already exists.</exception>
    /// <exception cref="InvalidOperationException">This is not the key's lower-bound gap.</exception>
    public PersistentIntMapCursor<TValue> Insert(int key, TValue value)
    {
        var source = Source;
        var rank = source.GetLowerBoundRank(key, out var found);
        if (found)
            throw new ArgumentException($"The key '{key}' is already present.", nameof(key));
        EnsureCurrentGap(rank, key);
        return new PersistentIntMapCursor<TValue>(source.Add(key, value), checked(_position + 1));
    }

    /// <summary>
    /// Sets an exact next entry or inserts a missing key at its lower-bound gap. Updates keep the gap fixed;
    /// insertions return the gap after the new entry. O(32).
    /// </summary>
    public PersistentIntMapCursor<TValue> SetItem(int key, TValue value)
    {
        var source = Source;
        var rank = source.GetLowerBoundRank(key, out var found);
        EnsureCurrentGap(rank, key);
        var edited = source.SetItem(key, value);
        if (ReferenceEquals(edited, source))
            return this;
        return new PersistentIntMapCursor<TValue>(edited, found ? _position : checked(_position + 1));
    }

    /// <summary>Replaces the next entry's value while retaining its key and keeping the gap fixed. O(32).</summary>
    public PersistentIntMapCursor<TValue> SetNextValue(TValue value)
    {
        var source = Source;
        if (!source.TryGetAt(_position, out var next))
            throw new InvalidOperationException("No entry follows the cursor gap.");
        var edited = source.SetItem(next.Key, value);
        return ReferenceEquals(edited, source) ? this : new PersistentIntMapCursor<TValue>(edited, _position);
    }

    /// <summary>Deletes the entry before the gap and moves the gap left. O(32).</summary>
    public PersistentIntMapCursor<TValue> DeletePrevious()
    {
        var source = Source;
        if (_position == 0 || !source.TryGetAt(_position - 1, out var previous))
            throw new InvalidOperationException("No entry precedes the cursor gap.");
        return new PersistentIntMapCursor<TValue>(source.Remove(previous.Key), _position - 1);
    }

    /// <summary>Deletes the entry after the gap and keeps the gap fixed. O(32).</summary>
    public PersistentIntMapCursor<TValue> DeleteNext()
    {
        var source = Source;
        if (!source.TryGetAt(_position, out var next))
            throw new InvalidOperationException("No entry follows the cursor gap.");
        return new PersistentIntMapCursor<TValue>(source.Remove(next.Key), _position);
    }

    /// <summary>Returns this cursor version's canonical immutable map. O(1).</summary>
    public PersistentIntMap<TValue> Snapshot() => Source;

    private PersistentIntMap<TValue> Source => _source ?? throw new InvalidOperationException(
        "The default PersistentIntMapCursor<TValue> value is not initialized. Obtain a cursor from PersistentIntMap<TValue>.");

    private void EnsureCurrentGap(int rank, int key)
    {
        if (rank != _position)
            throw new InvalidOperationException($"Key '{key}' belongs at gap {rank}, not at the current gap {_position}.");
    }

}

public sealed partial class PersistentLongMap<TValue>
{
    /// <summary>Creates an immutable ordered cursor at a rank gap. O(1).</summary>
    public PersistentLongMapCursor<TValue> GetCursor(int position = 0)
    {
        PatriciaCursorGuard.ValidatePosition(position, Count);
        return new PersistentLongMapCursor<TValue>(this, position);
    }

    /// <summary>Creates an immutable ordered cursor after the final entry. O(1).</summary>
    public PersistentLongMapCursor<TValue> GetCursorAtEnd() => new(this, Count);

    /// <summary>Creates a lower-bound cursor in ascending signed-key order. O(64).</summary>
    public PersistentLongMapCursor<TValue> GetLowerBoundCursor(long key) =>
        new(this, _core.GetLowerBoundRank(key, out _));

    /// <summary>Creates an upper-bound cursor in ascending signed-key order. O(64).</summary>
    public PersistentLongMapCursor<TValue> GetUpperBoundCursor(long key)
    {
        var rank = _core.GetLowerBoundRank(key, out var found);
        return new PersistentLongMapCursor<TValue>(this, found ? checked(rank + 1) : rank);
    }

    /// <summary>Creates a lower-bound cursor and reports whether its next entry has the exact key. O(64).</summary>
    public PersistentLongMapCursor<TValue> GetCursorAtKey(long key, out bool found)
    {
        var rank = _core.GetLowerBoundRank(key, out found);
        return new PersistentLongMapCursor<TValue>(this, rank);
    }

    internal bool TryGetAt(int index, out KeyValuePair<long, TValue> entry) => _core.TryGetAt(index, out entry);

    internal int GetLowerBoundRank(long key, out bool found) => _core.GetLowerBoundRank(key, out found);
}

/// <summary>An immutable persistent gap cursor over a <see cref="PersistentLongMap{TValue}"/>.</summary>
/// <typeparam name="TValue">The map value type.</typeparam>
/// <remarks>
/// This is a canonical-root-plus-rank checkpoint. Peeks and edits are O(64); count, position, movement,
/// rank seeking, and snapshot are O(1). The default struct value is invalid.
/// </remarks>
public readonly struct PersistentLongMapCursor<TValue>
{
    private readonly PersistentLongMap<TValue>? _source;
    private readonly int _position;

    internal PersistentLongMapCursor(PersistentLongMap<TValue> source, int position)
    {
        _source = source;
        _position = position;
    }

    /// <summary>Gets the entry count.</summary>
    public int Count => Source.Count;

    /// <summary>Gets the number of entries before the gap.</summary>
    public int Position { get { _ = Source; return _position; } }

    /// <summary>Gets whether the gap is at the start.</summary>
    public bool IsAtStart => Position == 0;

    /// <summary>Gets whether the gap is at the end.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the entry before the gap.</summary>
    public bool TryPeekPrevious(out KeyValuePair<long, TValue> entry)
    {
        var source = Source;
        if (_position > 0)
            return source.TryGetAt(_position - 1, out entry);
        entry = default;
        return false;
    }

    /// <summary>Attempts to read the entry after the gap.</summary>
    public bool TryPeekNext(out KeyValuePair<long, TValue> entry) => Source.TryGetAt(_position, out entry);

    /// <summary>Moves the gap one entry toward the start.</summary>
    public PersistentLongMapCursor<TValue> MovePrevious()
    {
        var source = Source;
        if (_position == 0)
            throw new InvalidOperationException("The cursor is already at the start.");
        return new PersistentLongMapCursor<TValue>(source, _position - 1);
    }

    /// <summary>Moves the gap one entry toward the end.</summary>
    public PersistentLongMapCursor<TValue> MoveNext()
    {
        var source = Source;
        if (_position == source.Count)
            throw new InvalidOperationException("The cursor is already at the end.");
        return new PersistentLongMapCursor<TValue>(source, _position + 1);
    }

    /// <summary>Moves the gap to a rank in <c>0 .. Count</c>.</summary>
    public PersistentLongMapCursor<TValue> Seek(int position)
    {
        var source = Source;
        PatriciaCursorGuard.ValidatePosition(position, source.Count);
        return position == _position ? this : new PersistentLongMapCursor<TValue>(source, position);
    }

    /// <summary>Strictly inserts a missing key at its lower-bound gap and returns the gap after it.</summary>
    public PersistentLongMapCursor<TValue> Insert(long key, TValue value)
    {
        var source = Source;
        var rank = source.GetLowerBoundRank(key, out var found);
        if (found)
            throw new ArgumentException($"The key '{key}' is already present.", nameof(key));
        EnsureCurrentGap(rank, key);
        return new PersistentLongMapCursor<TValue>(source.Add(key, value), checked(_position + 1));
    }

    /// <summary>Sets the exact next entry or inserts a key at its missing lower-bound gap.</summary>
    public PersistentLongMapCursor<TValue> SetItem(long key, TValue value)
    {
        var source = Source;
        var rank = source.GetLowerBoundRank(key, out var found);
        EnsureCurrentGap(rank, key);
        var edited = source.SetItem(key, value);
        if (ReferenceEquals(edited, source))
            return this;
        return new PersistentLongMapCursor<TValue>(edited, found ? _position : checked(_position + 1));
    }

    /// <summary>Replaces the next entry's value while retaining its key and gap.</summary>
    public PersistentLongMapCursor<TValue> SetNextValue(TValue value)
    {
        var source = Source;
        if (!source.TryGetAt(_position, out var next))
            throw new InvalidOperationException("No entry follows the cursor gap.");
        var edited = source.SetItem(next.Key, value);
        return ReferenceEquals(edited, source) ? this : new PersistentLongMapCursor<TValue>(edited, _position);
    }

    /// <summary>Deletes the entry before the gap and moves the gap left.</summary>
    public PersistentLongMapCursor<TValue> DeletePrevious()
    {
        var source = Source;
        if (_position == 0 || !source.TryGetAt(_position - 1, out var previous))
            throw new InvalidOperationException("No entry precedes the cursor gap.");
        return new PersistentLongMapCursor<TValue>(source.Remove(previous.Key), _position - 1);
    }

    /// <summary>Deletes the entry after the gap and keeps the gap fixed.</summary>
    public PersistentLongMapCursor<TValue> DeleteNext()
    {
        var source = Source;
        if (!source.TryGetAt(_position, out var next))
            throw new InvalidOperationException("No entry follows the cursor gap.");
        return new PersistentLongMapCursor<TValue>(source.Remove(next.Key), _position);
    }

    /// <summary>Returns this cursor version's canonical immutable map.</summary>
    public PersistentLongMap<TValue> Snapshot() => Source;

    private PersistentLongMap<TValue> Source => _source ?? throw new InvalidOperationException(
        "The default PersistentLongMapCursor<TValue> value is not initialized. Obtain a cursor from PersistentLongMap<TValue>.");

    private void EnsureCurrentGap(int rank, long key)
    {
        if (rank != _position)
            throw new InvalidOperationException($"Key '{key}' belongs at gap {rank}, not at the current gap {_position}.");
    }

}

public sealed partial class PersistentIntSet
{
    /// <summary>Creates an immutable ordered cursor at a population-rank gap. O(1).</summary>
    public PersistentIntSetCursor GetCursor(int position = 0)
    {
        PatriciaCursorGuard.ValidatePosition(position, Count);
        return new PersistentIntSetCursor(this, position);
    }

    /// <summary>Creates a cursor after the final item. O(1).</summary>
    public PersistentIntSetCursor GetCursorAtEnd() => new(this, Count);

    /// <summary>Creates the lower-bound cursor for an item. O(32).</summary>
    public PersistentIntSetCursor GetLowerBoundCursor(int item) => new(this, _map.GetLowerBoundRank(item, out _));

    /// <summary>Creates the upper-bound cursor for an item. O(32).</summary>
    public PersistentIntSetCursor GetUpperBoundCursor(int item)
    {
        var rank = _map.GetLowerBoundRank(item, out var found);
        return new PersistentIntSetCursor(this, found ? checked(rank + 1) : rank);
    }

    /// <summary>Creates a lower-bound cursor and reports whether its next item is exact. O(32).</summary>
    public PersistentIntSetCursor GetCursorAtItem(int item, out bool found)
    {
        var rank = _map.GetLowerBoundRank(item, out found);
        return new PersistentIntSetCursor(this, rank);
    }

    internal bool TryGetAt(int index, out int item)
    {
        if (_map.TryGetAt(index, out var entry))
        {
            item = entry.Key;
            return true;
        }
        item = default;
        return false;
    }

    internal int GetLowerBoundRank(int item, out bool found) => _map.GetLowerBoundRank(item, out found);
}

/// <summary>An immutable persistent gap cursor over a <see cref="PersistentIntSet"/>.</summary>
/// <remarks>Peeks and edits are O(32); rank movement and snapshot are O(1). The default value is invalid.</remarks>
public readonly struct PersistentIntSetCursor
{
    private readonly PersistentIntSet? _source;
    private readonly int _position;

    internal PersistentIntSetCursor(PersistentIntSet source, int position)
    {
        _source = source;
        _position = position;
    }

    /// <summary>Gets the item count.</summary>
    public int Count => Source.Count;

    /// <summary>Gets the number of items before the gap.</summary>
    public int Position { get { _ = Source; return _position; } }

    /// <summary>Gets whether the gap is at the start.</summary>
    public bool IsAtStart => Position == 0;

    /// <summary>Gets whether the gap is at the end.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the item before the gap.</summary>
    public bool TryPeekPrevious(out int item)
    {
        if (_position > 0)
            return Source.TryGetAt(_position - 1, out item);
        _ = Source;
        item = default;
        return false;
    }

    /// <summary>Attempts to read the item after the gap.</summary>
    public bool TryPeekNext(out int item) => Source.TryGetAt(_position, out item);

    /// <summary>Moves the gap one item toward the start.</summary>
    public PersistentIntSetCursor MovePrevious()
    {
        var source = Source;
        if (_position == 0)
            throw new InvalidOperationException("The cursor is already at the start.");
        return new PersistentIntSetCursor(source, _position - 1);
    }

    /// <summary>Moves the gap one item toward the end.</summary>
    public PersistentIntSetCursor MoveNext()
    {
        var source = Source;
        if (_position == source.Count)
            throw new InvalidOperationException("The cursor is already at the end.");
        return new PersistentIntSetCursor(source, _position + 1);
    }

    /// <summary>Moves the gap to a rank in <c>0 .. Count</c>.</summary>
    public PersistentIntSetCursor Seek(int position)
    {
        var source = Source;
        PatriciaCursorGuard.ValidatePosition(position, source.Count);
        return position == _position ? this : new PersistentIntSetCursor(source, position);
    }

    /// <summary>Adds an item at its lower-bound gap, returning the gap after a new item.</summary>
    public PersistentIntSetCursor Add(int item)
    {
        var source = Source;
        var rank = source.GetLowerBoundRank(item, out var found);
        EnsureCurrentGap(rank, item);
        if (found)
            return this;
        return new PersistentIntSetCursor(source.Add(item), checked(_position + 1));
    }

    /// <summary>Deletes the item before the gap and moves the gap left.</summary>
    public PersistentIntSetCursor DeletePrevious()
    {
        var source = Source;
        if (_position == 0 || !source.TryGetAt(_position - 1, out var previous))
            throw new InvalidOperationException("No item precedes the cursor gap.");
        return new PersistentIntSetCursor(source.Remove(previous), _position - 1);
    }

    /// <summary>Deletes the item after the gap and keeps the gap fixed.</summary>
    public PersistentIntSetCursor DeleteNext()
    {
        var source = Source;
        if (!source.TryGetAt(_position, out var next))
            throw new InvalidOperationException("No item follows the cursor gap.");
        return new PersistentIntSetCursor(source.Remove(next), _position);
    }

    /// <summary>Returns this cursor version's canonical immutable set.</summary>
    public PersistentIntSet Snapshot() => Source;

    private PersistentIntSet Source => _source ?? throw new InvalidOperationException(
        "The default PersistentIntSetCursor value is not initialized. Obtain a cursor from PersistentIntSet.");

    private void EnsureCurrentGap(int rank, int item)
    {
        if (rank != _position)
            throw new InvalidOperationException($"Item '{item}' belongs at gap {rank}, not at the current gap {_position}.");
    }
}

public sealed partial class PersistentLongSet
{
    /// <summary>Creates an immutable ordered cursor at a population-rank gap. O(1).</summary>
    public PersistentLongSetCursor GetCursor(int position = 0)
    {
        PatriciaCursorGuard.ValidatePosition(position, Count);
        return new PersistentLongSetCursor(this, position);
    }

    /// <summary>Creates a cursor after the final item. O(1).</summary>
    public PersistentLongSetCursor GetCursorAtEnd() => new(this, Count);

    /// <summary>Creates the lower-bound cursor for an item. O(64).</summary>
    public PersistentLongSetCursor GetLowerBoundCursor(long item) => new(this, _map.GetLowerBoundRank(item, out _));

    /// <summary>Creates the upper-bound cursor for an item. O(64).</summary>
    public PersistentLongSetCursor GetUpperBoundCursor(long item)
    {
        var rank = _map.GetLowerBoundRank(item, out var found);
        return new PersistentLongSetCursor(this, found ? checked(rank + 1) : rank);
    }

    /// <summary>Creates a lower-bound cursor and reports whether its next item is exact. O(64).</summary>
    public PersistentLongSetCursor GetCursorAtItem(long item, out bool found)
    {
        var rank = _map.GetLowerBoundRank(item, out found);
        return new PersistentLongSetCursor(this, rank);
    }

    internal bool TryGetAt(int index, out long item)
    {
        if (_map.TryGetAt(index, out var entry))
        {
            item = entry.Key;
            return true;
        }
        item = default;
        return false;
    }

    internal int GetLowerBoundRank(long item, out bool found) => _map.GetLowerBoundRank(item, out found);
}

/// <summary>An immutable persistent gap cursor over a <see cref="PersistentLongSet"/>.</summary>
/// <remarks>Peeks and edits are O(64); rank movement and snapshot are O(1). The default value is invalid.</remarks>
public readonly struct PersistentLongSetCursor
{
    private readonly PersistentLongSet? _source;
    private readonly int _position;

    internal PersistentLongSetCursor(PersistentLongSet source, int position)
    {
        _source = source;
        _position = position;
    }

    /// <summary>Gets the item count.</summary>
    public int Count => Source.Count;

    /// <summary>Gets the number of items before the gap.</summary>
    public int Position { get { _ = Source; return _position; } }

    /// <summary>Gets whether the gap is at the start.</summary>
    public bool IsAtStart => Position == 0;

    /// <summary>Gets whether the gap is at the end.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the item before the gap.</summary>
    public bool TryPeekPrevious(out long item)
    {
        if (_position > 0)
            return Source.TryGetAt(_position - 1, out item);
        _ = Source;
        item = default;
        return false;
    }

    /// <summary>Attempts to read the item after the gap.</summary>
    public bool TryPeekNext(out long item) => Source.TryGetAt(_position, out item);

    /// <summary>Moves the gap one item toward the start.</summary>
    public PersistentLongSetCursor MovePrevious()
    {
        var source = Source;
        if (_position == 0)
            throw new InvalidOperationException("The cursor is already at the start.");
        return new PersistentLongSetCursor(source, _position - 1);
    }

    /// <summary>Moves the gap one item toward the end.</summary>
    public PersistentLongSetCursor MoveNext()
    {
        var source = Source;
        if (_position == source.Count)
            throw new InvalidOperationException("The cursor is already at the end.");
        return new PersistentLongSetCursor(source, _position + 1);
    }

    /// <summary>Moves the gap to a rank in <c>0 .. Count</c>.</summary>
    public PersistentLongSetCursor Seek(int position)
    {
        var source = Source;
        PatriciaCursorGuard.ValidatePosition(position, source.Count);
        return position == _position ? this : new PersistentLongSetCursor(source, position);
    }

    /// <summary>Adds an item at its lower-bound gap, returning the gap after a new item.</summary>
    public PersistentLongSetCursor Add(long item)
    {
        var source = Source;
        var rank = source.GetLowerBoundRank(item, out var found);
        EnsureCurrentGap(rank, item);
        if (found)
            return this;
        return new PersistentLongSetCursor(source.Add(item), checked(_position + 1));
    }

    /// <summary>Deletes the item before the gap and moves the gap left.</summary>
    public PersistentLongSetCursor DeletePrevious()
    {
        var source = Source;
        if (_position == 0 || !source.TryGetAt(_position - 1, out var previous))
            throw new InvalidOperationException("No item precedes the cursor gap.");
        return new PersistentLongSetCursor(source.Remove(previous), _position - 1);
    }

    /// <summary>Deletes the item after the gap and keeps the gap fixed.</summary>
    public PersistentLongSetCursor DeleteNext()
    {
        var source = Source;
        if (!source.TryGetAt(_position, out var next))
            throw new InvalidOperationException("No item follows the cursor gap.");
        return new PersistentLongSetCursor(source.Remove(next), _position);
    }

    /// <summary>Returns this cursor version's canonical immutable set.</summary>
    public PersistentLongSet Snapshot() => Source;

    private PersistentLongSet Source => _source ?? throw new InvalidOperationException(
        "The default PersistentLongSetCursor value is not initialized. Obtain a cursor from PersistentLongSet.");

    private void EnsureCurrentGap(int rank, long item)
    {
        if (rank != _position)
            throw new InvalidOperationException($"Item '{item}' belongs at gap {rank}, not at the current gap {_position}.");
    }
}

internal static class PatriciaCursorGuard
{
    internal static void ValidatePosition(int position, int count)
    {
        if ((uint)position > (uint)count)
            throw new ArgumentOutOfRangeException(nameof(position), position, $"Position must be in 0 .. {count}.");
    }
}
