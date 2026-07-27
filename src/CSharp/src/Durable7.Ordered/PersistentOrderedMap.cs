// Represents an immutable insertion-ordered map with comparer-defined keyed lookup and explicit
// positional reordering.

using System.Collections;
using System.Diagnostics;
using Durable7.FingerTree;
using Durable7.Hamt;

namespace Durable7.Ordered;

/// <summary>
/// Represents an immutable insertion-ordered map with comparer-defined keyed lookup and explicit
/// positional reordering.
/// </summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <remarks>
/// The map couples a persistent CHAMP index with a persistent stamp-ordered sequence. One immutable
/// entry object is retained by both indexes, so keyed reads use the hash index while ordered reads
/// and enumeration require no hashing. Assigning an existing key retains its stored key
/// representative and position; only the explicit movement operations change order.
/// </remarks>
[DebuggerDisplay("Count = {Count}")]
public sealed partial class PersistentOrderedMap<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
{
    private const long StampStride = 1L << 20;

    private static readonly PersistentOrderedMap<TKey, TValue> EmptyInstance = new(
        FingerTreeDeque<Entry>.Empty,
        PersistentHashMap<TKey, Entry>.Empty,
        EqualityComparer<TValue>.Default);

    private readonly FingerTreeDeque<Entry> _order;
    private readonly PersistentHashMap<TKey, Entry> _byKey;

    private PersistentOrderedMap(
        FingerTreeDeque<Entry> order,
        PersistentHashMap<TKey, Entry> byKey,
        IEqualityComparer<TValue> valueComparer)
    {
        Debug.Assert(order.Count == byKey.Count);
        _order = order;
        _byKey = byKey;
        ValueComparer = valueComparer;
    }

    private sealed class Entry(long stamp, TKey key, TValue value)
    {
        /// <summary>Gets the stamp.</summary>
        internal long Stamp { get; } = stamp;
        /// <summary>Gets the stored key.</summary>
        internal TKey Key { get; } = key;
        /// <summary>Gets the stored value.</summary>
        internal TValue Value { get; } = value;
    }

    private sealed class StampOrder : IComparer<Entry>
    {
        /// <summary>
        /// Gets the shared instance. The value carries no state, so one instance serves every caller.
        /// </summary>
        internal static readonly StampOrder Instance = new();

        /// <summary>Orders two values.</summary>
        public int Compare(Entry? x, Entry? y) => x!.Stamp.CompareTo(y!.Stamp);
    }

    /// <summary>Gets the shared empty map using default key and value comparers.</summary>
    public static PersistentOrderedMap<TKey, TValue> Empty => EmptyInstance;

    /// <summary>Gets the number of entries.</summary>
    public int Count => _order.Count;

    /// <summary>Gets whether the map contains no entries.</summary>
    public bool IsEmpty => _order.IsEmpty;

    /// <summary>Gets the equality comparer defining key equivalence.</summary>
    public IEqualityComparer<TKey> KeyComparer => _byKey.Comparer;

    /// <summary>Gets the equality comparer used for value no-op detection.</summary>
    public IEqualityComparer<TValue> ValueComparer { get; }

    /// <summary>Gets the keys in map order.</summary>
    public IEnumerable<TKey> Keys => EnumerateKeys();

    /// <summary>Gets the values in corresponding map order.</summary>
    public IEnumerable<TValue> Values => EnumerateValues();

    /// <summary>Gets the value for an equivalent key.</summary>
    /// <exception cref="KeyNotFoundException">The key is absent.</exception>
    public TValue this[TKey key] =>
        _byKey.TryGetValue(key, out var entry) ? entry.Value : throw MissingKeyError(key);

    /// <summary>Gets the first entry in map order.</summary>
    /// <exception cref="InvalidOperationException">The map is empty.</exception>
    public KeyValuePair<TKey, TValue> First => IsEmpty ? throw EmptyError() : ToPair(_order.First);

    /// <summary>Gets the last entry in map order.</summary>
    /// <exception cref="InvalidOperationException">The map is empty.</exception>
    public KeyValuePair<TKey, TValue> Last => IsEmpty ? throw EmptyError() : ToPair(_order.Last);

    /// <summary>Creates an empty ordered map retaining independent key and value comparers.</summary>
    public static PersistentOrderedMap<TKey, TValue> Create(
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        keyComparer ??= EqualityComparer<TKey>.Default;
        valueComparer ??= EqualityComparer<TValue>.Default;
        if (ReferenceEquals(keyComparer, EqualityComparer<TKey>.Default)
            && ReferenceEquals(valueComparer, EqualityComparer<TValue>.Default))
        {
            return Empty;
        }

        return new(
            FingerTreeDeque<Entry>.Empty,
            PersistentHashMap<TKey, Entry>.Create(keyComparer),
            valueComparer);
    }

    /// <summary>
    /// Creates an ordered map from entries, retaining the first key representative and position and
    /// the last value for each key equivalence class.
    /// </summary>
    public static PersistentOrderedMap<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> items,
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = Create(keyComparer, valueComparer);
        foreach (var (key, value) in items)
            result = result.SetItem(key, value);
        return result;
    }

    /// <summary>Determines whether an equivalent key is present.</summary>
    public bool ContainsKey(TKey key) => _byKey.ContainsKey(key);

    /// <summary>Tries to retrieve the value for an equivalent key.</summary>
    public bool TryGetValue(TKey key, out TValue value)
    {
        if (_byKey.TryGetValue(key, out var entry))
        {
            value = entry.Value;
            return true;
        }

        value = default!;
        return false;
    }

    /// <summary>Tries to retrieve the stored key representative for an equivalent key.</summary>
    public bool TryGetKey(TKey equalKey, out TKey actualKey)
    {
        if (_byKey.TryGetValue(equalKey, out var entry))
        {
            actualKey = entry.Key;
            return true;
        }

        actualKey = equalKey;
        return false;
    }

    /// <summary>Gets the entry at a zero-based ordered position.</summary>
    public KeyValuePair<TKey, TValue> EntryAt(int index) => ToPair(_order[index]);

    /// <summary>Finds the ordered position of an equivalent key, or -1 when absent.</summary>
    public int IndexOfKey(TKey key) =>
        _byKey.TryGetValue(key, out var entry) ? IndexOfStamp(entry.Stamp) : -1;

    /// <summary>Adds a key/value pair at the end, rejecting an existing equivalent key.</summary>
    /// <exception cref="ArgumentException">An equivalent key already exists.</exception>
    public PersistentOrderedMap<TKey, TValue> Add(TKey key, TValue value) =>
        InsertCore(Count, key, value, throwOnDuplicate: true, out _);

    /// <summary>Attempts to append an absent key/value pair.</summary>
    public bool TryAdd(TKey key, TValue value, out PersistentOrderedMap<TKey, TValue> result)
    {
        result = InsertCore(Count, key, value, throwOnDuplicate: false, out var added);
        return added;
    }

    /// <summary>Adds an absent key/value pair at the first position.</summary>
    /// <exception cref="ArgumentException">An equivalent key already exists.</exception>
    public PersistentOrderedMap<TKey, TValue> AddFirst(TKey key, TValue value) =>
        InsertCore(0, key, value, throwOnDuplicate: true, out _);

    /// <summary>Inserts an absent key/value pair before a zero-based position.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The index is outside 0 through Count.</exception>
    /// <exception cref="ArgumentException">An equivalent key already exists.</exception>
    public PersistentOrderedMap<TKey, TValue> Insert(int index, TKey key, TValue value)
    {
        CheckInsertIndex(index);
        return InsertCore(index, key, value, throwOnDuplicate: true, out _);
    }

    /// <summary>
    /// Adds or replaces a key's value. Existing keys retain their representative and position.
    /// </summary>
    public PersistentOrderedMap<TKey, TValue> SetItem(TKey key, TValue value)
    {
        if (!_byKey.TryGetValue(key, out var oldEntry))
            return InsertCore(Count, key, value, throwOnDuplicate: true, out _);
        if (ValueComparer.Equals(oldEntry.Value, value))
            return this;

        var replacement = new Entry(oldEntry.Stamp, oldEntry.Key, value);
        var index = IndexOfStamp(oldEntry.Stamp);
        var byKey = _byKey.SetItem(oldEntry.Key, replacement);
        return new(_order.SetItem(index, replacement), byKey, ValueComparer);
    }

    /// <summary>Moves an existing key to the first position.</summary>
    /// <exception cref="KeyNotFoundException">The key is absent.</exception>
    public PersistentOrderedMap<TKey, TValue> MoveToFirst(TKey key) => MoveExisting(0, key);

    /// <summary>Moves an existing key to the last position.</summary>
    /// <exception cref="KeyNotFoundException">The key is absent.</exception>
    public PersistentOrderedMap<TKey, TValue> MoveToLast(TKey key) => MoveExisting(Count - 1, key);

    /// <summary>Moves an existing key to a final zero-based position.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The index does not identify an entry.</exception>
    /// <exception cref="KeyNotFoundException">The key is absent.</exception>
    public PersistentOrderedMap<TKey, TValue> MoveTo(int index, TKey key)
    {
        if ((uint)index >= (uint)Count)
            throw IndexError(index);
        return MoveExisting(index, key);
    }

    /// <summary>Removes an equivalent key, returning the receiver when absent.</summary>
    public PersistentOrderedMap<TKey, TValue> Remove(TKey key) =>
        TryRemove(key, out var result, out _) ? result : this;

    /// <summary>Attempts to remove an equivalent key and reports its stored value.</summary>
    public bool TryRemove(
        TKey key,
        out PersistentOrderedMap<TKey, TValue> result,
        out TValue value)
    {
        if (!_byKey.TryRemove(key, out var byKey, out var entry))
        {
            result = this;
            value = default!;
            return false;
        }

        value = entry.Value;
        result = Wrap(_order.RemoveAt(IndexOfStamp(entry.Stamp)), byKey, ValueComparer);
        return true;
    }

    /// <summary>Removes the entry at a zero-based ordered position.</summary>
    public PersistentOrderedMap<TKey, TValue> RemoveAt(int index)
    {
        var entry = _order[index];
        if (!_byKey.TryRemove(entry.Key, out var byKey, out var indexed)
            || !ReferenceEquals(entry, indexed))
        {
            throw IndexDisagreementError();
        }

        return Wrap(_order.RemoveAt(index), byKey, ValueComparer);
    }

    /// <summary>Returns an empty map retaining both comparers.</summary>
    public PersistentOrderedMap<TKey, TValue> Clear() => IsEmpty ? this : Create(KeyComparer, ValueComparer);

    /// <summary>Extracts a contiguous range in map order.</summary>
    public PersistentOrderedMap<TKey, TValue> GetRange(int index, int count)
    {
        CheckRange(index, count);
        if (count == Count)
            return this;
        if (count == 0)
            return Create(KeyComparer, ValueComparer);

        return BuildFromOrder(_order.GetRange(index, count), KeyComparer, ValueComparer, relabel: false);
    }

    /// <summary>Retains the first count entries.</summary>
    public PersistentOrderedMap<TKey, TValue> Take(int count)
    {
        CheckCount(count);
        return GetRange(0, count);
    }

    /// <summary>Removes the first count entries.</summary>
    public PersistentOrderedMap<TKey, TValue> Drop(int count)
    {
        CheckCount(count);
        return GetRange(count, Count - count);
    }

    /// <summary>Reverses map order, returning the receiver for counts zero and one.</summary>
    public PersistentOrderedMap<TKey, TValue> Reverse()
    {
        if (Count <= 1)
            return this;
        var entries = _order.ToArray();
        Array.Reverse(entries);
        return RebuildEntries(entries);
    }

    /// <summary>Copies entries to a fresh array in map order.</summary>
    public KeyValuePair<TKey, TValue>[] ToArray()
    {
        var result = new KeyValuePair<TKey, TValue>[Count];
        var index = 0;
        foreach (var entry in _order)
            result[index++] = ToPair(entry);
        return result;
    }

    /// <summary>Returns an enumerator over entries in map order.</summary>
    public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => EnumeratePairs().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>Checks the map's structural invariants. For tests and diagnostics.</summary>
    internal void ValidateInvariants()
    {
        if (_order.Count != _byKey.Count)
            throw IndexDisagreementError();

        long? previous = null;
        foreach (var entry in _order)
        {
            if (previous.HasValue && entry.Stamp <= previous.Value)
                throw new InvalidOperationException("Ordered-map stamps are not strictly increasing.");
            previous = entry.Stamp;
            if (!_byKey.TryGetValue(entry.Key, out var indexed) || !ReferenceEquals(entry, indexed))
                throw IndexDisagreementError();
        }

        foreach (var pair in _byKey)
        {
            var ordered = _order[IndexOfStamp(pair.Value.Stamp)];
            if (!ReferenceEquals(ordered, pair.Value))
                throw IndexDisagreementError();
        }
    }

    private PersistentOrderedMap<TKey, TValue> InsertCore(
        int index,
        TKey key,
        TValue value,
        bool throwOnDuplicate,
        out bool added)
    {
        if (_byKey.ContainsKey(key))
        {
            if (throwOnDuplicate)
                throw new ArgumentException("An equivalent key already exists.", nameof(key));
            added = false;
            return this;
        }

        if (Count == int.MaxValue)
            throw CountOverflowError();

        if (!TryPickStamp(_order, index, out var stamp))
        {
            // Sparse-label exhaustion is local to this produced version. Rebuild every entry with
            // evenly spaced labels; retained branches keep their old labels and remain untouched.
            added = true;
            var entries = new Entry[Count + 1];
            var target = 0;
            foreach (var entry in _order)
            {
                if (target == index)
                    target++;
                entries[target++] = entry;
            }
            entries[index] = new Entry(0, key, value);
            return RebuildEntries(entries);
        }

        var inserted = new Entry(stamp, key, value);
        var byKey = _byKey.Add(key, inserted);
        var order = index == 0
            ? _order.AddFirst(inserted)
            : index == Count
                ? _order.AddLast(inserted)
                : _order.InsertAt(index, inserted);
        added = true;
        return new(order, byKey, ValueComparer);
    }

    private PersistentOrderedMap<TKey, TValue> MoveExisting(int finalIndex, TKey key)
    {
        if (!_byKey.TryGetValue(key, out var oldEntry))
            throw MissingKeyError(key);
        var oldIndex = IndexOfStamp(oldEntry.Stamp);
        if (oldIndex == finalIndex)
            return this;

        var trimmed = _order.RemoveAt(oldIndex);
        if (!TryPickStamp(trimmed, finalIndex, out var stamp))
        {
            // Movement may exhaust the same midpoint gaps as insertion. Rebuild from logical order
            // rather than mutating either index independently, preserving failure atomicity.
            var entries = trimmed.ToArray();
            Array.Resize(ref entries, entries.Length + 1);
            Array.Copy(entries, finalIndex, entries, finalIndex + 1, entries.Length - finalIndex - 1);
            entries[finalIndex] = oldEntry;
            return RebuildEntries(entries);
        }

        var moved = new Entry(stamp, oldEntry.Key, oldEntry.Value);
        var order = finalIndex == 0
            ? trimmed.AddFirst(moved)
            : finalIndex == trimmed.Count
                ? trimmed.AddLast(moved)
                : trimmed.InsertAt(finalIndex, moved);
        return new(order, _byKey.SetItem(oldEntry.Key, moved), ValueComparer);
    }

    private int IndexOfStamp(long stamp)
    {
        var probe = new Entry(stamp, default!, default!);
        var index = _order.SortedLowerBound(probe, StampOrder.Instance);
        if ((uint)index >= (uint)Count || _order[index].Stamp != stamp)
            throw IndexDisagreementError();
        return index;
    }

    private static bool TryPickStamp(FingerTreeDeque<Entry> order, int index, out long stamp)
    {
        stamp = 0;
        if (order.IsEmpty)
            return true;
        if (index == 0)
        {
            if (order.First.Stamp < long.MinValue + StampStride)
                return false;
            stamp = order.First.Stamp - StampStride;
            return true;
        }
        if (index == order.Count)
        {
            if (order.Last.Stamp > long.MaxValue - StampStride)
                return false;
            stamp = order.Last.Stamp + StampStride;
            return true;
        }

        var left = order[index - 1].Stamp;
        var right = order[index].Stamp;
        var gap = unchecked((ulong)(right - left));
        if (gap < 2)
            return false;
        stamp = unchecked(left + (long)(gap >> 1));
        return true;
    }

    private PersistentOrderedMap<TKey, TValue> RebuildEntries(Entry[] entries)
    {
        var rebuilt = new Entry[entries.Length];
        for (var i = 0; i < entries.Length; i++)
            rebuilt[i] = new Entry(checked((long)i * StampStride), entries[i].Key, entries[i].Value);
        return BuildFromOrder(FingerTreeDeque<Entry>.Create(rebuilt), KeyComparer, ValueComparer, relabel: false);
    }

    private static PersistentOrderedMap<TKey, TValue> BuildFromOrder(
        FingerTreeDeque<Entry> order,
        IEqualityComparer<TKey> keyComparer,
        IEqualityComparer<TValue> valueComparer,
        bool relabel)
    {
        if (order.IsEmpty)
            return Create(keyComparer, valueComparer);

        var entries = relabel ? order.ToArray() : null;
        if (entries is not null)
        {
            for (var i = 0; i < entries.Length; i++)
                entries[i] = new Entry(checked((long)i * StampStride), entries[i].Key, entries[i].Value);
            order = FingerTreeDeque<Entry>.Create(entries);
        }

        var pairs = new KeyValuePair<TKey, Entry>[order.Count];
        var index = 0;
        foreach (var entry in order)
            pairs[index++] = KeyValuePair.Create(entry.Key, entry);
        return new(order, PersistentHashMap<TKey, Entry>.CreateRange(pairs, keyComparer), valueComparer);
    }

    private static PersistentOrderedMap<TKey, TValue> Wrap(
        FingerTreeDeque<Entry> order,
        PersistentHashMap<TKey, Entry> byKey,
        IEqualityComparer<TValue> valueComparer) =>
        order.IsEmpty ? Create(byKey.Comparer, valueComparer) : new(order, byKey, valueComparer);

    private IEnumerable<TKey> EnumerateKeys()
    {
        foreach (var entry in _order)
            yield return entry.Key;
    }

    private IEnumerable<TValue> EnumerateValues()
    {
        foreach (var entry in _order)
            yield return entry.Value;
    }

    private IEnumerable<KeyValuePair<TKey, TValue>> EnumeratePairs()
    {
        foreach (var entry in _order)
            yield return ToPair(entry);
    }

    private static KeyValuePair<TKey, TValue> ToPair(Entry entry) =>
        KeyValuePair.Create(entry.Key, entry.Value);

    private void CheckInsertIndex(int index)
    {
        if ((uint)index > (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(index), index, "Index must lie within 0 through Count.");
    }

    private void CheckRange(int index, int count)
    {
        if ((uint)index > (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(index), index, "Index must lie within 0 through Count.");
        if ((uint)count > (uint)(Count - index))
            throw new ArgumentOutOfRangeException(nameof(count), count, "The range extends past the map.");
    }

    private void CheckCount(int count)
    {
        if ((uint)count > (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(count), count, "Count must lie within 0 through map Count.");
    }

    private static InvalidOperationException EmptyError() => new("The ordered map is empty.");

    private static KeyNotFoundException MissingKeyError(TKey key) =>
        new($"The key '{key}' is absent from the ordered map.");

    private static ArgumentOutOfRangeException IndexError(int index) =>
        new(nameof(index), index, "Index must identify an entry in the map.");

    private static InvalidOperationException IndexDisagreementError() =>
        new("The ordered map's key and order indexes disagree.");

    private static OverflowException CountOverflowError() =>
        new("The operation would create more than Int32.MaxValue ordered-map entries.");
}
