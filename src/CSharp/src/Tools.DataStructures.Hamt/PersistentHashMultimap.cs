using System.Collections;
using System.Diagnostics;

namespace Tools.DataStructures.Hamt;

/// <summary>
/// Represents an immutable unordered set-valued hash multimap backed by nested persistent CHAMP
/// collections.
/// </summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <remarks>
/// Each key equivalence class owns one nonempty <see cref="PersistentHashSet{T}"/> of value classes.
/// Empty inner sets are never stored. Updates preserve the first representative independently in
/// both domains, return the receiver for logical no-ops, and share every untouched trie subtree.
/// </remarks>
[DebuggerDisplay("KeyCount = {KeyCount}, PairCount = {PairCount}")]
public sealed class PersistentHashMultimap<TKey, TValue> :
    IEnumerable<KeyValuePair<TKey, TValue>>
{
    private static readonly PersistentHashMultimap<TKey, TValue> EmptyInstance = new(
        PersistentHashMap<TKey, PersistentHashSet<TValue>>.Empty,
        EqualityComparer<TValue>.Default,
        pairCount: 0);

    private readonly PersistentHashMap<TKey, PersistentHashSet<TValue>> _groups;

    private PersistentHashMultimap(
        PersistentHashMap<TKey, PersistentHashSet<TValue>> groups,
        IEqualityComparer<TValue> valueComparer,
        long pairCount)
    {
        _groups = groups;
        ValueComparer = valueComparer;
        PairCount = pairCount;
    }

    /// <summary>Gets the shared empty multimap using default key and value comparers.</summary>
    public static PersistentHashMultimap<TKey, TValue> Empty => EmptyInstance;

    /// <summary>Gets the number of represented key equivalence classes.</summary>
    public int KeyCount => _groups.Count;

    /// <summary>Gets the total number of distinct key/value equivalence-class pairs.</summary>
    public long PairCount { get; }

    /// <summary>Gets whether the multimap contains no pairs.</summary>
    public bool IsEmpty => PairCount == 0;

    /// <summary>Gets the equality comparer defining key equivalence.</summary>
    public IEqualityComparer<TKey> KeyComparer => _groups.Comparer;

    /// <summary>Gets the equality comparer defining value equivalence within every group.</summary>
    public IEqualityComparer<TValue> ValueComparer { get; }

    /// <summary>Gets stored key representatives in stable-for-one-version HAMT order.</summary>
    public IEnumerable<TKey> Keys => _groups.Keys;

    /// <summary>Gets key and nonempty value-set groups in stable-for-one-version HAMT order.</summary>
    public IEnumerable<KeyValuePair<TKey, PersistentHashSet<TValue>>> Groups => _groups;

    /// <summary>Creates an empty multimap retaining independent key and value comparers.</summary>
    public static PersistentHashMultimap<TKey, TValue> Create(
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
            PersistentHashMap<TKey, PersistentHashSet<TValue>>.Create(keyComparer),
            valueComparer,
            pairCount: 0);
    }

    /// <summary>Creates a multimap from distinct-pair contributions in enumeration order.</summary>
    public static PersistentHashMultimap<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> pairs,
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(pairs);
        var result = Create(keyComparer, valueComparer);
        foreach (var (key, value) in pairs)
            result = result.Add(key, value);
        return result;
    }

    /// <summary>Determines whether an equivalent key owns a nonempty value set.</summary>
    public bool ContainsKey(TKey key) => _groups.ContainsKey(key);

    /// <summary>Determines whether an equivalent key/value pair is present.</summary>
    public bool Contains(TKey key, TValue value) =>
        _groups.TryGetValue(key, out var values) && values.Contains(value);

    /// <summary>Gets the number of value classes associated with an equivalent key.</summary>
    public int CountValues(TKey key) =>
        _groups.TryGetValue(key, out var values) ? values.Count : 0;

    /// <summary>
    /// Gets the persistent value set for an equivalent key, or a comparer-preserving empty set when
    /// the key is absent.
    /// </summary>
    public PersistentHashSet<TValue> GetValues(TKey key) =>
        _groups.TryGetValue(key, out var values) ? values : PersistentHashSet<TValue>.Create(ValueComparer);

    /// <summary>Tries to retrieve the nonempty persistent value set for an equivalent key.</summary>
    public bool TryGetValues(TKey key, out PersistentHashSet<TValue> values)
    {
        if (_groups.TryGetValue(key, out values!))
            return true;
        values = PersistentHashSet<TValue>.Create(ValueComparer);
        return false;
    }

    /// <summary>Tries to retrieve the stored key representative for an equivalent key.</summary>
    public bool TryGetKey(TKey equalKey, out TKey actualKey) =>
        _groups.TryGetKey(equalKey, out actualKey);

    /// <summary>Tries to retrieve the stored value representative within a key's group.</summary>
    public bool TryGetValue(TKey key, TValue equalValue, out TValue actualValue)
    {
        if (_groups.TryGetValue(key, out var values))
            return values.TryGetValue(equalValue, out actualValue);
        actualValue = equalValue;
        return false;
    }

    /// <summary>Adds a key/value pair, returning the receiver when an equivalent pair exists.</summary>
    public PersistentHashMultimap<TKey, TValue> Add(TKey key, TValue value)
    {
        var pairCount = checked(PairCount + 1);
        if (!_groups.TryGetValue(key, out var values))
        {
            var created = PersistentHashSet<TValue>.Create(ValueComparer).Add(value);
            return new(_groups.Add(key, created), ValueComparer, pairCount);
        }

        if (!values.TryAdd(value, out var updated))
            return this;
        return new(_groups.SetItem(key, updated), ValueComparer, pairCount);
    }

    /// <summary>Attempts to add an absent key/value pair.</summary>
    public bool TryAdd(
        TKey key,
        TValue value,
        out PersistentHashMultimap<TKey, TValue> result)
    {
        result = Add(key, value);
        return !ReferenceEquals(result, this);
    }

    /// <summary>Removes a key/value pair, returning the receiver when absent.</summary>
    public PersistentHashMultimap<TKey, TValue> Remove(TKey key, TValue value) =>
        TryRemove(key, value, out var result) ? result : this;

    /// <summary>Attempts to remove one key/value pair.</summary>
    public bool TryRemove(
        TKey key,
        TValue value,
        out PersistentHashMultimap<TKey, TValue> result)
    {
        if (!_groups.TryGetValue(key, out var values)
            || !values.TryRemove(value, out var updated))
        {
            result = this;
            return false;
        }

        // The outer trie never stores an empty group. Removing the last inner value therefore
        // contracts both logical levels in one published successor.
        var groups = updated.IsEmpty ? _groups.Remove(key) : _groups.SetItem(key, updated);
        result = Wrap(groups, ValueComparer, PairCount - 1);
        return true;
    }

    /// <summary>Removes an entire key group, returning the receiver when absent.</summary>
    public PersistentHashMultimap<TKey, TValue> RemoveKey(TKey key) =>
        TryRemoveKey(key, out var result, out _) ? result : this;

    /// <summary>Attempts to remove an entire key group and returns its persistent value set.</summary>
    public bool TryRemoveKey(
        TKey key,
        out PersistentHashMultimap<TKey, TValue> result,
        out PersistentHashSet<TValue> values)
    {
        if (!_groups.TryRemove(key, out var groups, out values!))
        {
            result = this;
            values = PersistentHashSet<TValue>.Create(ValueComparer);
            return false;
        }

        result = Wrap(groups, ValueComparer, checked(PairCount - values.Count));
        return true;
    }

    /// <summary>Returns a comparer-preserving empty multimap.</summary>
    public PersistentHashMultimap<TKey, TValue> Clear() =>
        IsEmpty ? this : Create(KeyComparer, ValueComparer);

    /// <summary>Copies all pairs to a fresh array in nested stable-for-one-version HAMT order.</summary>
    public KeyValuePair<TKey, TValue>[] ToArray()
    {
        if (PairCount > Array.MaxLength)
            throw new OverflowException("The multimap contains too many pairs to materialize as an array.");
        var result = new KeyValuePair<TKey, TValue>[(int)PairCount];
        var index = 0;
        foreach (var pair in this)
            result[index++] = pair;
        return result;
    }

    /// <summary>Returns an enumerator over all distinct pairs.</summary>
    public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => EnumeratePairs().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal void ValidateInvariants()
    {
        long count = 0;
        foreach (var (_, values) in _groups)
        {
            if (values.IsEmpty)
                throw new InvalidOperationException("A hash multimap stores an empty value group.");
            if (!ReferenceEquals(values.Comparer, ValueComparer))
                throw new InvalidOperationException("A hash multimap value group retains the wrong comparer.");
            count = checked(count + values.Count);
        }

        if (count != PairCount)
            throw new InvalidOperationException("The hash multimap pair count disagrees with its groups.");
    }

    private IEnumerable<KeyValuePair<TKey, TValue>> EnumeratePairs()
    {
        foreach (var (key, values) in _groups)
            foreach (var value in values)
                yield return KeyValuePair.Create(key, value);
    }

    private static PersistentHashMultimap<TKey, TValue> Wrap(
        PersistentHashMap<TKey, PersistentHashSet<TValue>> groups,
        IEqualityComparer<TValue> valueComparer,
        long pairCount) =>
        groups.IsEmpty ? Create(groups.Comparer, valueComparer) : new(groups, valueComparer, pairCount);
}
