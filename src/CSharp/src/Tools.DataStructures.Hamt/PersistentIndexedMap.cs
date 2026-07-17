using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.Hamt;

/// <summary>
/// Represents an immutable primary hash map with one automatically maintained nonunique secondary index.
/// </summary>
/// <typeparam name="TKey">The primary key type.</typeparam>
/// <typeparam name="TValue">The row value type.</typeparam>
/// <typeparam name="TIndexKey">The secondary index-key type.</typeparam>
/// <remarks>
/// The primary CHAMP stores each row together with the exact secondary key selected when that row was
/// published. A <see cref="PersistentHashMultimap{TKey, TValue}"/> maps secondary keys back to primary
/// keys. The retained selector is invoked once for each genuinely new or value-changing row and is
/// never re-invoked for removal or a value-comparer no-op. Both successor indexes are built before a
/// new facade is published.
/// </remarks>
[DebuggerDisplay("Count = {Count}, IndexKeyCount = {IndexKeyCount}")]
public sealed class PersistentIndexedMap<TKey, TValue, TIndexKey> :
    IReadOnlyDictionary<TKey, TValue>
{
    private readonly PersistentHashMap<TKey, Entry> _primary;
    private readonly PersistentHashMultimap<TIndexKey, TKey> _index;

    private PersistentIndexedMap(
        PersistentHashMap<TKey, Entry> primary,
        PersistentHashMultimap<TIndexKey, TKey> index,
        Func<TKey, TValue, TIndexKey> indexSelector,
        IEqualityComparer<TValue> valueComparer)
    {
        Debug.Assert(primary.Count == index.PairCount);
        Debug.Assert(ReferenceEquals(primary.Comparer, index.ValueComparer));
        _primary = primary;
        _index = index;
        IndexSelector = indexSelector;
        ValueComparer = valueComparer;
    }

    private sealed class Entry(TValue value, TIndexKey indexKey)
    {
        internal TValue Value { get; } = value;
        internal TIndexKey IndexKey { get; } = indexKey;
    }

    /// <summary>Gets the number of primary rows.</summary>
    public int Count => _primary.Count;

    /// <summary>Gets whether the map contains no rows.</summary>
    public bool IsEmpty => _primary.IsEmpty;

    /// <summary>Gets the number of represented secondary index-key classes.</summary>
    public int IndexKeyCount => _index.KeyCount;

    /// <summary>Gets the equality comparer defining primary key equivalence.</summary>
    public IEqualityComparer<TKey> KeyComparer => _primary.Comparer;

    /// <summary>Gets the equality comparer used for value no-op detection.</summary>
    public IEqualityComparer<TValue> ValueComparer { get; }

    /// <summary>Gets the equality comparer defining secondary index-key equivalence.</summary>
    public IEqualityComparer<TIndexKey> IndexComparer => _index.KeyComparer;

    /// <summary>Gets the retained selector used for genuinely new or changed rows.</summary>
    public Func<TKey, TValue, TIndexKey> IndexSelector { get; }

    /// <summary>Gets primary keys in stable-for-this-version trie order.</summary>
    public IEnumerable<TKey> Keys => _primary.Keys;

    /// <summary>Gets row values corresponding to <see cref="Keys"/> in the same order.</summary>
    public IEnumerable<TValue> Values => EnumerateValues();

    /// <summary>Gets represented secondary index keys in stable-for-this-version trie order.</summary>
    public IEnumerable<TIndexKey> IndexKeys => _index.Keys;

    /// <summary>Gets nonempty secondary groups in stable-for-this-version trie order.</summary>
    public IEnumerable<KeyValuePair<TIndexKey, PersistentHashSet<TKey>>> IndexGroups => _index.Groups;

    /// <summary>Gets the row value for an equivalent primary key.</summary>
    /// <param name="key">The primary key to find.</param>
    /// <returns>The stored row value.</returns>
    /// <exception cref="KeyNotFoundException">The primary key is absent.</exception>
    public TValue this[TKey key] =>
        _primary.TryGetValue(key, out var entry)
            ? entry.Value
            : throw new KeyNotFoundException($"The key '{key}' was not present in the indexed map.");

    /// <summary>Creates an empty indexed map with retained selector and equality policies.</summary>
    /// <param name="indexSelector">The selector that derives a secondary key from a primary key and value.</param>
    /// <param name="keyComparer">The primary key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <param name="indexComparer">The index-key comparer, or <see langword="null"/> for the default.</param>
    /// <returns>An empty indexed map retaining the supplied selector and policies.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="indexSelector"/> is <see langword="null"/>.</exception>
    public static PersistentIndexedMap<TKey, TValue, TIndexKey> Create(
        Func<TKey, TValue, TIndexKey> indexSelector,
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null,
        IEqualityComparer<TIndexKey>? indexComparer = null)
    {
        ArgumentNullException.ThrowIfNull(indexSelector);
        keyComparer ??= EqualityComparer<TKey>.Default;
        valueComparer ??= EqualityComparer<TValue>.Default;
        indexComparer ??= EqualityComparer<TIndexKey>.Default;
        return new(
            PersistentHashMap<TKey, Entry>.Create(keyComparer),
            PersistentHashMultimap<TIndexKey, TKey>.Create(indexComparer, keyComparer),
            indexSelector,
            valueComparer);
    }

    /// <summary>Creates an indexed map from rows, with later distinct values replacing earlier values.</summary>
    /// <param name="items">The primary rows to set in enumeration order.</param>
    /// <param name="indexSelector">The selector that derives a secondary key from a primary key and value.</param>
    /// <param name="keyComparer">The primary key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <param name="indexComparer">The index-key comparer, or <see langword="null"/> for the default.</param>
    /// <returns>An indexed map containing the supplied rows.</returns>
    /// <exception cref="ArgumentNullException">
    /// <paramref name="items"/> or <paramref name="indexSelector"/> is <see langword="null"/>.
    /// </exception>
    public static PersistentIndexedMap<TKey, TValue, TIndexKey> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> items,
        Func<TKey, TValue, TIndexKey> indexSelector,
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null,
        IEqualityComparer<TIndexKey>? indexComparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = Create(indexSelector, keyComparer, valueComparer, indexComparer);
        foreach (var (key, value) in items)
            result = result.SetItem(key, value);
        return result;
    }

    /// <summary>Determines whether an equivalent primary key exists.</summary>
    /// <param name="key">The primary key to find.</param>
    /// <returns><see langword="true"/> when the row exists.</returns>
    public bool ContainsKey(TKey key) => _primary.ContainsKey(key);

    /// <summary>Tries to get the value for an equivalent primary key.</summary>
    /// <param name="key">The primary key to find.</param>
    /// <param name="value">The stored value when found.</param>
    /// <returns><see langword="true"/> when the row exists.</returns>
    public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        if (_primary.TryGetValue(key, out var entry))
        {
            value = entry.Value;
            return true;
        }

        value = default;
        return false;
    }

    /// <summary>Tries to recover the first stored representative of a primary key class.</summary>
    /// <param name="equalKey">A key equivalent to the stored key.</param>
    /// <param name="actualKey">The stored primary key representative when found.</param>
    /// <returns><see langword="true"/> when the row exists.</returns>
    public bool TryGetKey(TKey equalKey, out TKey actualKey) =>
        _primary.TryGetKey(equalKey, out actualKey!);

    /// <summary>Tries to get the retained secondary index key for a primary row.</summary>
    /// <param name="key">The primary key to find.</param>
    /// <param name="indexKey">The globally retained secondary key representative when found.</param>
    /// <returns><see langword="true"/> when the row exists.</returns>
    public bool TryGetIndexKey(TKey key, out TIndexKey indexKey)
    {
        if (_primary.TryGetValue(key, out var entry))
        {
            indexKey = entry.IndexKey;
            return true;
        }

        indexKey = default!;
        return false;
    }

    /// <summary>Determines whether a secondary index-key class has at least one row.</summary>
    /// <param name="indexKey">The secondary key to find.</param>
    /// <returns><see langword="true"/> when a nonempty group exists.</returns>
    public bool ContainsIndexKey(TIndexKey indexKey) => _index.ContainsKey(indexKey);

    /// <summary>Gets the number of primary rows in a secondary group.</summary>
    /// <param name="indexKey">The secondary key to find.</param>
    /// <returns>The group size, or zero when absent.</returns>
    public int CountByIndex(TIndexKey indexKey) => _index.CountValues(indexKey);

    /// <summary>Gets the persistent primary-key set for a secondary group.</summary>
    /// <param name="indexKey">The secondary key to find.</param>
    /// <returns>A set retaining <see cref="KeyComparer"/>; empty when the group is absent.</returns>
    public PersistentHashSet<TKey> GetKeysByIndex(TIndexKey indexKey) => _index.GetValues(indexKey);

    /// <summary>Tries to get the persistent primary-key set for a secondary group.</summary>
    /// <param name="indexKey">The secondary key to find.</param>
    /// <param name="keys">The stored primary-key set when found.</param>
    /// <returns><see langword="true"/> when the group exists.</returns>
    public bool TryGetKeysByIndex(TIndexKey indexKey, out PersistentHashSet<TKey> keys) =>
        _index.TryGetValues(indexKey, out keys!);

    /// <summary>Adds a primary row, rejecting an equivalent primary key.</summary>
    /// <param name="key">The primary key to add.</param>
    /// <param name="value">The row value.</param>
    /// <returns>The enlarged indexed map.</returns>
    /// <exception cref="ArgumentException">An equivalent primary key already exists.</exception>
    public PersistentIndexedMap<TKey, TValue, TIndexKey> Add(TKey key, TValue value)
    {
        if (_primary.ContainsKey(key))
            throw new ArgumentException("An equivalent primary key already exists.", nameof(key));

        var selectedIndex = IndexSelector(key, value);
        var index = _index.Add(selectedIndex, key);
        index.TryGetKey(selectedIndex, out var actualIndex);
        var primary = _primary.Add(key, new(value, actualIndex));
        return new(primary, index, IndexSelector, ValueComparer);
    }

    /// <summary>Attempts to add a primary row.</summary>
    /// <param name="key">The primary key to add.</param>
    /// <param name="value">The row value.</param>
    /// <param name="result">The resulting indexed map, or this map when the key exists.</param>
    /// <returns><see langword="true"/> when the row was added.</returns>
    public bool TryAdd(
        TKey key,
        TValue value,
        out PersistentIndexedMap<TKey, TValue, TIndexKey> result)
    {
        if (_primary.ContainsKey(key))
        {
            result = this;
            return false;
        }

        result = Add(key, value);
        return true;
    }

    /// <summary>Sets a primary row while maintaining its secondary index membership.</summary>
    /// <param name="key">The primary key to add or update.</param>
    /// <param name="value">The new row value.</param>
    /// <returns>
    /// This map when an existing value compares equal; otherwise, a map with both indexes updated.
    /// </returns>
    public PersistentIndexedMap<TKey, TValue, TIndexKey> SetItem(TKey key, TValue value)
    {
        if (!_primary.TryGetEntry(key, out var actualKey, out var current))
            return Add(key, value);

        if (ValueComparer.Equals(current.Value, value))
            return this;

        var selectedIndex = IndexSelector(actualKey, value);
        PersistentHashMultimap<TIndexKey, TKey> index;
        TIndexKey actualIndex;
        if (IndexComparer.Equals(current.IndexKey, selectedIndex))
        {
            index = _index;
            actualIndex = current.IndexKey;
        }
        else
        {
            index = _index.Remove(current.IndexKey, actualKey).Add(selectedIndex, actualKey);
            index.TryGetKey(selectedIndex, out actualIndex!);
        }

        var primary = _primary.SetItem(actualKey, new(value, actualIndex));
        return new(primary, index, IndexSelector, ValueComparer);
    }

    /// <summary>Removes a primary row and its secondary membership.</summary>
    /// <param name="key">The primary key to remove.</param>
    /// <returns>This map when absent; otherwise, the reduced indexed map.</returns>
    public PersistentIndexedMap<TKey, TValue, TIndexKey> Remove(TKey key)
    {
        if (!_primary.TryGetEntry(key, out var actualKey, out var current))
            return this;

        var primary = _primary.Remove(actualKey);
        var index = _index.Remove(current.IndexKey, actualKey);
        return new(primary, index, IndexSelector, ValueComparer);
    }

    /// <summary>Returns an empty indexed map retaining the selector and all policies.</summary>
    /// <returns>This map when already empty; otherwise, an empty indexed map.</returns>
    public PersistentIndexedMap<TKey, TValue, TIndexKey> Clear() =>
        IsEmpty ? this : Create(IndexSelector, KeyComparer, ValueComparer, IndexComparer);

    /// <summary>Materializes primary rows in stable-for-this-version trie order.</summary>
    /// <returns>An array containing every primary row.</returns>
    public KeyValuePair<TKey, TValue>[] ToArray()
    {
        var result = new KeyValuePair<TKey, TValue>[Count];
        var index = 0;
        foreach (var pair in this)
            result[index++] = pair;
        return result;
    }

    /// <summary>Returns an enumerator over primary rows.</summary>
    /// <returns>An enumerator over the immutable indexed-map snapshot.</returns>
    public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => EnumerateRows().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal void ValidateInvariants()
    {
        _index.ValidateInvariants();
        if (_primary.Count != _index.PairCount)
            throw new InvalidOperationException("The indexed map's primary and secondary counts disagree.");
        if (!ReferenceEquals(_primary.Comparer, _index.ValueComparer))
            throw new InvalidOperationException("The indexed map's primary-key policies disagree.");

        foreach (var (key, entry) in _primary)
        {
            if (!_index.Contains(entry.IndexKey, key))
                throw new InvalidOperationException("A primary row is absent from its secondary index group.");
        }

        foreach (var (indexKey, key) in _index)
        {
            if (!_primary.TryGetEntry(key, out var actualKey, out var entry)
                || !IndexComparer.Equals(indexKey, entry.IndexKey))
            {
                throw new InvalidOperationException("A secondary membership disagrees with its primary row.");
            }

            if (!typeof(TKey).IsValueType && !ReferenceEquals(key, actualKey))
                throw new InvalidOperationException("A secondary membership retains the wrong primary-key representative.");
            if (!typeof(TIndexKey).IsValueType && !ReferenceEquals(indexKey, entry.IndexKey))
                throw new InvalidOperationException("A primary row retains the wrong secondary-key representative.");
        }
    }

    private IEnumerable<TValue> EnumerateValues()
    {
        foreach (var entry in _primary.Values)
            yield return entry.Value;
    }

    private IEnumerable<KeyValuePair<TKey, TValue>> EnumerateRows()
    {
        foreach (var (key, entry) in _primary)
            yield return new(key, entry.Value);
    }
}
