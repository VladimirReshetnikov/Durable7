using System.Collections;
using System.Diagnostics;

namespace Durable7.Ordered;

/// <summary>
/// Represents an immutable key-grouped multimap that preserves insertion order for both key groups
/// and the distinct values within each group.
/// </summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <remarks>
/// The map composes a <see cref="PersistentOrderedMap{TKey, TValue}"/> of
/// <see cref="PersistentOrderedSet{T}"/> values. Enumeration is key-grouped: key groups appear in
/// first-insertion order and each group's values appear in their own first-insertion order. It does
/// not preserve one global interleaved pair-arrival order. Empty value groups are never stored.
/// </remarks>
[DebuggerDisplay("KeyCount = {KeyCount}, PairCount = {PairCount}")]
public sealed partial class PersistentOrderedMultimap<TKey, TValue> :
    IEnumerable<KeyValuePair<TKey, TValue>>
{
    private static readonly PersistentOrderedMultimap<TKey, TValue> EmptyInstance = new(
        PersistentOrderedMap<TKey, PersistentOrderedSet<TValue>>.Empty,
        EqualityComparer<TValue>.Default,
        0);

    private readonly PersistentOrderedMap<TKey, PersistentOrderedSet<TValue>> _groups;

    private PersistentOrderedMultimap(
        PersistentOrderedMap<TKey, PersistentOrderedSet<TValue>> groups,
        IEqualityComparer<TValue> valueComparer,
        long pairCount)
    {
        Debug.Assert(pairCount >= 0);
        _groups = groups;
        ValueComparer = valueComparer;
        PairCount = pairCount;
    }

    /// <summary>Gets the shared empty multimap using default key and value comparers.</summary>
    public static PersistentOrderedMultimap<TKey, TValue> Empty => EmptyInstance;

    /// <summary>Gets the number of nonempty key groups.</summary>
    public int KeyCount => _groups.Count;

    /// <summary>Gets the total number of distinct key/value pairs.</summary>
    public long PairCount { get; }

    /// <summary>Gets whether the multimap contains no pairs.</summary>
    public bool IsEmpty => PairCount == 0;

    /// <summary>Gets the equality comparer defining key equivalence.</summary>
    public IEqualityComparer<TKey> KeyComparer => _groups.KeyComparer;

    /// <summary>Gets the equality comparer defining value equivalence within each key group.</summary>
    public IEqualityComparer<TValue> ValueComparer { get; }

    /// <summary>Gets the stored keys in key-group order.</summary>
    public IEnumerable<TKey> Keys => _groups.Keys;

    /// <summary>Gets the nonempty groups in key-group order.</summary>
    public IEnumerable<KeyValuePair<TKey, PersistentOrderedSet<TValue>>> Groups => _groups;

    /// <summary>Creates an empty ordered multimap retaining independent key and value comparers.</summary>
    /// <param name="keyComparer">The key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <returns>An empty ordered multimap with the selected policies.</returns>
    public static PersistentOrderedMultimap<TKey, TValue> Create(
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
            PersistentOrderedMap<TKey, PersistentOrderedSet<TValue>>.Create(keyComparer),
            valueComparer,
            0);
    }

    /// <summary>Creates an ordered multimap from pairs in enumeration order.</summary>
    /// <param name="pairs">The pairs to add.</param>
    /// <param name="keyComparer">The key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <returns>An ordered multimap containing the distinct supplied pairs.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="pairs"/> is <see langword="null"/>.</exception>
    public static PersistentOrderedMultimap<TKey, TValue> CreateRange(
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

    /// <summary>Determines whether an equivalent key has a nonempty value group.</summary>
    /// <param name="key">The key to find.</param>
    /// <returns><see langword="true"/> when a group exists.</returns>
    public bool ContainsKey(TKey key) => _groups.ContainsKey(key);

    /// <summary>Determines whether an equivalent key/value pair is present.</summary>
    /// <param name="key">The key to find.</param>
    /// <param name="value">The value to find within the key group.</param>
    /// <returns><see langword="true"/> when the pair is present.</returns>
    public bool Contains(TKey key, TValue value) =>
        _groups.TryGetValue(key, out var values) && values.Contains(value);

    /// <summary>Gets the number of values associated with an equivalent key.</summary>
    /// <param name="key">The key to find.</param>
    /// <returns>The group size, or zero when the key is absent.</returns>
    public int CountValues(TKey key) =>
        _groups.TryGetValue(key, out var values) ? values.Count : 0;

    /// <summary>Gets the ordered value set for an equivalent key.</summary>
    /// <param name="key">The key to find.</param>
    /// <returns>The stored immutable value set, or an empty set retaining <see cref="ValueComparer"/>.</returns>
    public PersistentOrderedSet<TValue> GetValues(TKey key) =>
        _groups.TryGetValue(key, out var values)
            ? values
            : PersistentOrderedSet<TValue>.Create(ValueComparer);

    /// <summary>Tries to get the ordered value set for an equivalent key.</summary>
    /// <param name="key">The key to find.</param>
    /// <param name="values">The stored immutable value set when found.</param>
    /// <returns><see langword="true"/> when the key is present.</returns>
    public bool TryGetValues(TKey key, out PersistentOrderedSet<TValue> values) =>
        _groups.TryGetValue(key, out values!);

    /// <summary>Tries to recover the originally stored key representative.</summary>
    /// <param name="equalKey">A key equivalent to the stored key.</param>
    /// <param name="actualKey">The stored key representative when found.</param>
    /// <returns><see langword="true"/> when a key group exists.</returns>
    public bool TryGetKey(TKey equalKey, out TKey actualKey) =>
        _groups.TryGetKey(equalKey, out actualKey!);

    /// <summary>Tries to recover the stored value representative within a key group.</summary>
    /// <param name="key">The key whose group is searched.</param>
    /// <param name="equalValue">A value equivalent to the stored value.</param>
    /// <param name="actualValue">The stored value representative when found.</param>
    /// <returns><see langword="true"/> when the pair is present.</returns>
    public bool TryGetValue(TKey key, TValue equalValue, out TValue actualValue)
    {
        if (_groups.TryGetValue(key, out var values))
            return values.TryGetValue(equalValue, out actualValue!);

        actualValue = default!;
        return false;
    }

    /// <summary>Adds a pair, returning this instance when an equivalent pair already exists.</summary>
    /// <param name="key">The key to add.</param>
    /// <param name="value">The value to add within the key group.</param>
    /// <returns>The resulting ordered multimap.</returns>
    /// <exception cref="OverflowException">The total pair count cannot be represented by <see cref="long"/>.</exception>
    public PersistentOrderedMultimap<TKey, TValue> Add(TKey key, TValue value)
    {
        if (_groups.TryGetValue(key, out var values))
        {
            var updatedValues = values.Add(value);
            if (ReferenceEquals(updatedValues, values))
                return this;

            return new(
                _groups.SetItem(key, updatedValues),
                ValueComparer,
                checked(PairCount + 1));
        }

        var firstValues = PersistentOrderedSet<TValue>.Create(ValueComparer).Add(value);
        return new(
            _groups.Add(key, firstValues),
            ValueComparer,
            checked(PairCount + 1));
    }

    /// <summary>Attempts to add a pair.</summary>
    /// <param name="key">The key to add.</param>
    /// <param name="value">The value to add within the key group.</param>
    /// <param name="result">The resulting multimap, or this instance when the pair already exists.</param>
    /// <returns><see langword="true"/> when the pair was added.</returns>
    public bool TryAdd(
        TKey key,
        TValue value,
        out PersistentOrderedMultimap<TKey, TValue> result)
    {
        result = Add(key, value);
        return !ReferenceEquals(result, this);
    }

    /// <summary>Removes a pair, returning this instance when it is absent.</summary>
    /// <param name="key">The key to find.</param>
    /// <param name="value">The value to remove.</param>
    /// <returns>The resulting ordered multimap.</returns>
    public PersistentOrderedMultimap<TKey, TValue> Remove(TKey key, TValue value) =>
        TryRemove(key, value, out var result) ? result : this;

    /// <summary>Attempts to remove a pair.</summary>
    /// <param name="key">The key to find.</param>
    /// <param name="value">The value to remove.</param>
    /// <param name="result">The resulting multimap, or this instance when the pair is absent.</param>
    /// <returns><see langword="true"/> when the pair was removed.</returns>
    public bool TryRemove(
        TKey key,
        TValue value,
        out PersistentOrderedMultimap<TKey, TValue> result)
    {
        if (!_groups.TryGetValue(key, out var values)
            || !values.TryRemove(value, out var updatedValues))
        {
            result = this;
            return false;
        }

        var updatedGroups = updatedValues.IsEmpty
            ? _groups.Remove(key)
            : _groups.SetItem(key, updatedValues);
        result = new(updatedGroups, ValueComparer, PairCount - 1);
        return true;
    }

    /// <summary>Removes an entire key group, returning this instance when the key is absent.</summary>
    /// <param name="key">The key to remove.</param>
    /// <returns>The resulting ordered multimap.</returns>
    public PersistentOrderedMultimap<TKey, TValue> RemoveKey(TKey key) =>
        TryRemoveKey(key, out var result) ? result : this;

    /// <summary>Attempts to remove an entire key group.</summary>
    /// <param name="key">The key to remove.</param>
    /// <param name="result">The resulting multimap, or this instance when the key is absent.</param>
    /// <returns><see langword="true"/> when a key group was removed.</returns>
    public bool TryRemoveKey(
        TKey key,
        out PersistentOrderedMultimap<TKey, TValue> result)
    {
        if (!_groups.TryGetValue(key, out var values))
        {
            result = this;
            return false;
        }

        result = new(_groups.Remove(key), ValueComparer, PairCount - values.Count);
        return true;
    }

    /// <summary>Returns an empty multimap retaining the current policies.</summary>
    /// <returns>This instance when already empty; otherwise, an empty ordered multimap.</returns>
    public PersistentOrderedMultimap<TKey, TValue> Clear() =>
        IsEmpty ? this : Create(KeyComparer, ValueComparer);

    /// <summary>Materializes all pairs in key-grouped order.</summary>
    /// <returns>An array containing the pairs.</returns>
    /// <exception cref="OverflowException">The pair count exceeds the maximum array length.</exception>
    public KeyValuePair<TKey, TValue>[] ToArray()
    {
        if (PairCount > int.MaxValue)
            throw new OverflowException("The multimap contains too many pairs to materialize as one array.");

        var result = new KeyValuePair<TKey, TValue>[(int)PairCount];
        var index = 0;
        foreach (var pair in this)
            result[index++] = pair;
        return result;
    }

    /// <summary>Returns an enumerator over pairs in key-grouped order.</summary>
    /// <returns>An enumerator over the immutable snapshot.</returns>
    public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => EnumeratePairs().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal void ValidateInvariants()
    {
        _groups.ValidateInvariants();
        long pairCount = 0;
        foreach (var (_, values) in _groups)
        {
            values.ValidateInvariants();
            if (values.IsEmpty)
                throw new InvalidOperationException("An ordered multimap stores an empty value group.");
            if (!ReferenceEquals(values.Comparer, ValueComparer))
                throw new InvalidOperationException("An ordered multimap value group retains the wrong comparer.");
            pairCount = checked(pairCount + values.Count);
        }

        if (pairCount != PairCount)
            throw new InvalidOperationException("The ordered multimap pair count disagrees with its groups.");
    }

    private IEnumerable<KeyValuePair<TKey, TValue>> EnumeratePairs()
    {
        foreach (var (key, values) in _groups)
        {
            foreach (var value in values)
                yield return new(key, value);
        }
    }
}
