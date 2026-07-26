// Represents an immutable one-to-one mapping backed by a pair of persistent hash-array mapped
// tries.

using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Threading;

namespace Durable7.Hamt;

/// <summary>
/// Represents an immutable one-to-one mapping backed by a pair of persistent hash-array mapped tries.
/// </summary>
/// <typeparam name="TKey">The type of keys.</typeparam>
/// <typeparam name="TValue">The type of values.</typeparam>
/// <remarks>
/// <para>
/// Every key equivalence class maps to exactly one value equivalence class, and every value class
/// maps back to exactly one key class. Key and value equality policies are retained independently.
/// </para>
/// <para>
/// Updates publish both successor tries only after all comparer callbacks and allocations succeed;
/// retained versions remain unchanged. <see cref="Inverse"/> is a cached O(1) facade over the same
/// two roots and does not rebuild or enumerate the mapping.
/// </para>
/// </remarks>
[DebuggerDisplay("Count = {Count}")]
public sealed class PersistentBiMap<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
{
    /// <summary>Gets the shared empty bimap using default key and value comparers.</summary>
    public static PersistentBiMap<TKey, TValue> Empty { get; } = new(
        PersistentHashMap<TKey, TValue>.Empty,
        PersistentHashMap<TValue, TKey>.Empty);

    private readonly PersistentHashMap<TKey, TValue> _forward;
    private readonly PersistentHashMap<TValue, TKey> _inverse;
    private PersistentBiMap<TValue, TKey>? _inverseView;

    private PersistentBiMap(
        PersistentHashMap<TKey, TValue> forward,
        PersistentHashMap<TValue, TKey> inverse)
    {
        _forward = forward;
        _inverse = inverse;
    }

    /// <summary>Gets the number of pairs.</summary>
    public int Count => _forward.Count;

    /// <summary>Gets whether the mapping contains no pairs.</summary>
    public bool IsEmpty => _forward.IsEmpty;

    /// <summary>Gets the comparer defining key equivalence.</summary>
    public IEqualityComparer<TKey> KeyComparer => _forward.Comparer;

    /// <summary>Gets the comparer defining value equivalence.</summary>
    public IEqualityComparer<TValue> ValueComparer => _inverse.Comparer;

    /// <summary>Gets a stable-for-one-version, otherwise unordered view of stored keys.</summary>
    public IEnumerable<TKey> Keys => _forward.Keys;

    /// <summary>Gets the values corresponding to <see cref="Keys"/> in the same enumeration order.</summary>
    public IEnumerable<TValue> Values => _forward.Values;

    /// <summary>Gets the value mapped from <paramref name="key"/>.</summary>
    /// <param name="key">The key to find.</param>
    /// <returns>The stored value representative.</returns>
    /// <exception cref="KeyNotFoundException">No equivalent key exists.</exception>
    public TValue this[TKey key] => _forward[key];

    /// <summary>Gets a cached inverse facade mapping stored values back to stored keys.</summary>
    /// <remarks>
    /// The first access allocates at most one published facade under contention. It performs no
    /// trie traversal. The returned facade's <see cref="Inverse"/> property is this instance.
    /// </remarks>
    public PersistentBiMap<TValue, TKey> Inverse
    {
        get
        {
            var current = Volatile.Read(ref _inverseView);
            if (current is not null)
                return current;

            var candidate = new PersistentBiMap<TValue, TKey>(_inverse, _forward)
            {
                _inverseView = this,
            };
            return Interlocked.CompareExchange(ref _inverseView, candidate, comparand: null) ?? candidate;
        }
    }

    internal PersistentHashMap<TKey, TValue> ForwardForTesting => _forward;

    internal PersistentHashMap<TValue, TKey> InverseForTesting => _inverse;

    /// <summary>Creates an empty bimap with independent key and value comparers.</summary>
    /// <param name="keyComparer">The key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <returns>An empty bimap retaining the selected comparer objects.</returns>
    public static PersistentBiMap<TKey, TValue> Create(
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
            PersistentHashMap<TKey, TValue>.Create(keyComparer),
            PersistentHashMap<TValue, TKey>.Create(valueComparer));
    }

    /// <summary>Creates a bimap from pairs, rejecting duplicates on either side.</summary>
    /// <param name="items">The pairs to add in enumeration order.</param>
    /// <param name="keyComparer">The key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <returns>A bimap containing all supplied pairs.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentException">An equivalent key or value occurs more than once.</exception>
    public static PersistentBiMap<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> items,
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = Create(keyComparer, valueComparer);
        foreach (var (key, value) in items)
            result = result.Add(key, value);
        return result;
    }

    /// <summary>Determines whether an equivalent key exists.</summary>
    /// <param name="key">The key to find.</param>
    /// <returns><see langword="true"/> when the key exists.</returns>
    public bool ContainsKey(TKey key) => _forward.ContainsKey(key);

    /// <summary>Determines whether an equivalent value exists.</summary>
    /// <param name="value">The value to find.</param>
    /// <returns><see langword="true"/> when the value exists.</returns>
    public bool ContainsValue(TValue value) => _inverse.ContainsKey(value);

    /// <summary>Looks up a key and returns its stored value representative.</summary>
    /// <param name="key">The key to find.</param>
    /// <param name="value">The stored value when found; otherwise the default value.</param>
    /// <returns><see langword="true"/> when the key exists.</returns>
    public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value) =>
        _forward.TryGetValue(key, out value);

    /// <summary>Looks up a value and returns its stored key representative.</summary>
    /// <param name="value">The value to find.</param>
    /// <param name="key">The stored key when found; otherwise the default key.</param>
    /// <returns><see langword="true"/> when the value exists.</returns>
    public bool TryGetKey(TValue value, [MaybeNullWhen(false)] out TKey key) =>
        _inverse.TryGetValue(value, out key);

    /// <summary>Adds a pair when neither side is already represented.</summary>
    /// <param name="key">The key representative to store.</param>
    /// <param name="value">The value representative to store.</param>
    /// <returns>The successor bimap.</returns>
    /// <exception cref="ArgumentException">An equivalent key or value already exists.</exception>
    public PersistentBiMap<TKey, TValue> Add(TKey key, TValue value)
    {
        if (_forward.ContainsKey(key))
            throw new ArgumentException("An equivalent key already exists.", nameof(key));
        if (_inverse.ContainsKey(value))
            throw new ArgumentException("An equivalent value already exists.", nameof(value));

        var forward = _forward.Add(key, value);
        var inverse = _inverse.Add(value, key);
        return new(forward, inverse);
    }

    /// <summary>Attempts to add a pair when neither side is already represented.</summary>
    /// <param name="key">The key representative to store.</param>
    /// <param name="value">The value representative to store.</param>
    /// <param name="result">The successor on success; otherwise this instance.</param>
    /// <returns><see langword="true"/> when the pair was added.</returns>
    public bool TryAdd(TKey key, TValue value, out PersistentBiMap<TKey, TValue> result)
    {
        if (_forward.ContainsKey(key) || _inverse.ContainsKey(value))
        {
            result = this;
            return false;
        }

        result = new(_forward.Add(key, value), _inverse.Add(value, key));
        return true;
    }

    /// <summary>Adds or replaces the value for a key without displacing another key.</summary>
    /// <param name="key">The key to add or update.</param>
    /// <param name="value">The new value.</param>
    /// <returns>The successor bimap, or this instance for a comparer-equivalent existing pair.</returns>
    /// <exception cref="ArgumentException">
    /// <paramref name="value"/> is already mapped from a different key equivalence class.
    /// </exception>
    public PersistentBiMap<TKey, TValue> SetItem(TKey key, TValue value)
    {
        if (!_forward.TryGetValue(key, out var previousValue))
        {
            if (_inverse.ContainsKey(value))
                throw new ArgumentException("An equivalent value is already mapped from another key.", nameof(value));
            return new(_forward.Add(key, value), _inverse.Add(value, key));
        }

        if (ValueComparer.Equals(previousValue, value))
            return this;
        if (_inverse.ContainsKey(value))
            throw new ArgumentException("An equivalent value is already mapped from another key.", nameof(value));

        _ = _inverse.TryGetValue(previousValue, out var storedKey);
        var forward = _forward.Remove(storedKey!).Add(storedKey!, value);
        var inverse = _inverse.Remove(previousValue!).Add(value, storedKey!);
        return new(forward, inverse);
    }

    /// <summary>Removes the pair selected by a key.</summary>
    /// <param name="key">The key to remove.</param>
    /// <returns>The successor bimap, or this instance when absent.</returns>
    public PersistentBiMap<TKey, TValue> RemoveKey(TKey key) =>
        TryRemoveKey(key, out var result, out _) ? result : this;

    /// <summary>Attempts to remove the pair selected by a key.</summary>
    /// <param name="key">The key to remove.</param>
    /// <param name="result">The successor on success; otherwise this instance.</param>
    /// <param name="value">The removed stored value, or the default value when absent.</param>
    /// <returns><see langword="true"/> when a pair was removed.</returns>
    public bool TryRemoveKey(
        TKey key,
        out PersistentBiMap<TKey, TValue> result,
        [MaybeNullWhen(false)] out TValue value)
    {
        if (!_forward.TryGetValue(key, out value))
        {
            result = this;
            return false;
        }

        _ = _inverse.TryGetValue(value, out var storedKey);
        result = new(_forward.Remove(storedKey!), _inverse.Remove(value!));
        return true;
    }

    /// <summary>Removes the pair selected by a value.</summary>
    /// <param name="value">The value to remove.</param>
    /// <returns>The successor bimap, or this instance when absent.</returns>
    public PersistentBiMap<TKey, TValue> RemoveValue(TValue value) =>
        TryRemoveValue(value, out var result, out _) ? result : this;

    /// <summary>Attempts to remove the pair selected by a value.</summary>
    /// <param name="value">The value to remove.</param>
    /// <param name="result">The successor on success; otherwise this instance.</param>
    /// <param name="key">The removed stored key, or the default key when absent.</param>
    /// <returns><see langword="true"/> when a pair was removed.</returns>
    public bool TryRemoveValue(
        TValue value,
        out PersistentBiMap<TKey, TValue> result,
        [MaybeNullWhen(false)] out TKey key)
    {
        if (!_inverse.TryGetValue(value, out key))
        {
            result = this;
            return false;
        }

        _ = _forward.TryGetValue(key, out var storedValue);
        result = new(_forward.Remove(key!), _inverse.Remove(storedValue!));
        return true;
    }

    /// <summary>Returns an empty bimap preserving both comparer objects.</summary>
    /// <returns>This instance when already empty; otherwise an empty compatible bimap.</returns>
    public PersistentBiMap<TKey, TValue> Clear() =>
        IsEmpty ? this : Create(KeyComparer, ValueComparer);

    /// <summary>Returns an allocation-free enumerator over forward pairs.</summary>
    /// <returns>An enumerator in stable-for-one-version, otherwise unspecified HAMT order.</returns>
    public Enumerator GetEnumerator() => new(_forward.GetEnumerator());

    IEnumerator<KeyValuePair<TKey, TValue>> IEnumerable<KeyValuePair<TKey, TValue>>.GetEnumerator() =>
        GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal bool ValidateBijection()
    {
        if (_forward.Count != _inverse.Count)
            return false;
        foreach (var (key, value) in _forward)
        {
            if (!_inverse.TryGetValue(value, out var inverseKey) || !KeyComparer.Equals(key, inverseKey))
                return false;
        }

        foreach (var (value, key) in _inverse)
        {
            if (!_forward.TryGetValue(key, out var forwardValue) || !ValueComparer.Equals(value, forwardValue))
                return false;
        }

        return true;
    }

    /// <summary>Enumerates forward key/value pairs without heap traversal storage.</summary>
    public struct Enumerator : IEnumerator<KeyValuePair<TKey, TValue>>
    {
        private PersistentHashMap<TKey, TValue>.Enumerator _inner;

        internal Enumerator(PersistentHashMap<TKey, TValue>.Enumerator inner) => _inner = inner;

        /// <summary>Gets the current pair.</summary>
        public readonly KeyValuePair<TKey, TValue> Current => _inner.Current;

        object IEnumerator.Current => Current;

        /// <summary>Advances to the next pair.</summary>
        /// <returns><see langword="true"/> when another pair exists.</returns>
        public bool MoveNext() => _inner.MoveNext();

        /// <summary>Releases enumeration resources.</summary>
        public readonly void Dispose() => _inner.Dispose();

        void IEnumerator.Reset() => ((IEnumerator)_inner).Reset();
    }
}
