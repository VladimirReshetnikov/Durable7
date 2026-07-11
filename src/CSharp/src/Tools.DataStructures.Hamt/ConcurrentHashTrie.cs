using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Threading;

namespace Tools.DataStructures.Hamt;

/// <summary>
/// Represents a lock-free mutable hash trie with constant-time immutable snapshots.
/// </summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <remarks>
/// <para>
/// Writers compute a new immutable CHAMP root and publish a generation-stamped state with one
/// compare-and-swap. A failed compare-and-swap retries against the newer state. Reads capture one
/// state and never lock; <see cref="Snapshot"/> returns that state's persistent map in O(1).
/// </para>
/// <para>
/// Delegate-based update factories can run more than once under contention and must therefore be
/// free of non-repeatable side effects. Published snapshots and enumerators remain stable while the
/// trie continues to change.
/// </para>
/// </remarks>
[DebuggerDisplay("Count = {Count}, Generation = {Generation}")]
public sealed class ConcurrentHashTrie<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
{
    private State _state;

    /// <summary>Initializes an empty trie with the default key comparer.</summary>
    public ConcurrentHashTrie()
        : this(comparer: null)
    {
    }

    /// <summary>Initializes an empty trie with a supplied key comparer.</summary>
    /// <param name="comparer">
    /// The key comparer, or <see langword="null"/> to use <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    public ConcurrentHashTrie(IEqualityComparer<TKey>? comparer)
    {
        _state = new State(PersistentHashMap<TKey, TValue>.Create(comparer), generation: 0);
    }

    /// <summary>Gets the number of entries in the currently published generation.</summary>
    public int Count => ReadState().Map.Count;

    /// <summary>Gets whether the currently published generation contains no entries.</summary>
    public bool IsEmpty => ReadState().Map.IsEmpty;

    /// <summary>Gets the key comparer retained by every generation and snapshot.</summary>
    public IEqualityComparer<TKey> Comparer => ReadState().Map.Comparer;

    /// <summary>Gets the monotonically increasing number of successful state publications.</summary>
    public long Generation => ReadState().Generation;

    /// <summary>Gets a snapshot view of the current keys.</summary>
    public IEnumerable<TKey> Keys => Snapshot().Keys;

    /// <summary>Gets a snapshot view of the current values.</summary>
    public IEnumerable<TValue> Values => Snapshot().Values;

    /// <summary>Gets or sets the value associated with a key.</summary>
    /// <param name="key">The key to read or set.</param>
    /// <exception cref="KeyNotFoundException">The key is absent during a get.</exception>
    public TValue this[TKey key]
    {
        get => ReadState().Map[key];
        set => SetItem(key, value);
    }

    /// <summary>Creates a trie populated from a sequence with last-wins key semantics.</summary>
    /// <param name="items">The entries to add in enumeration order.</param>
    /// <param name="comparer">The key comparer, or <see langword="null"/> for the default.</param>
    /// <returns>A mutable trie containing the supplied entries at generation zero.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static ConcurrentHashTrie<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> items,
        IEqualityComparer<TKey>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = new ConcurrentHashTrie<TKey, TValue>(comparer);
        result._state = new State(PersistentHashMap<TKey, TValue>.CreateRange(items, comparer), generation: 0);
        return result;
    }

    /// <summary>Returns the immutable map published by the current generation.</summary>
    /// <returns>An O(1) stable snapshot sharing the current CHAMP root.</returns>
    public PersistentHashMap<TKey, TValue> Snapshot() => ReadState().Map;

    /// <summary>Determines whether the current generation contains a key.</summary>
    /// <param name="key">The key to locate.</param>
    /// <returns><see langword="true"/> when the key is present; otherwise, <see langword="false"/>.</returns>
    public bool ContainsKey(TKey key) => ReadState().Map.ContainsKey(key);

    /// <summary>Tries to read a value from one stable generation.</summary>
    /// <param name="key">The key to locate.</param>
    /// <param name="value">The associated value on success; otherwise, the default value.</param>
    /// <returns><see langword="true"/> when the key is present; otherwise, <see langword="false"/>.</returns>
    public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value) =>
        ReadState().Map.TryGetValue(key, out value);

    /// <summary>Adds or replaces a key/value pair atomically.</summary>
    /// <param name="key">The key to add or replace.</param>
    /// <param name="value">The new value.</param>
    /// <remarks>An equal-value no-op does not publish a generation.</remarks>
    public void SetItem(TKey key, TValue value)
    {
        while (true)
        {
            var before = ReadState();
            var map = before.Map.SetItem(key, value);
            if (ReferenceEquals(map, before.Map))
                return;
            if (TryPublish(before, map))
                return;
        }
    }

    /// <summary>Attempts to add a key/value pair atomically.</summary>
    /// <param name="key">The key to add.</param>
    /// <param name="value">The value to add.</param>
    /// <returns><see langword="true"/> when added; <see langword="false"/> when the key already exists.</returns>
    public bool TryAdd(TKey key, TValue value)
    {
        while (true)
        {
            var before = ReadState();
            if (!before.Map.TryAdd(key, value, out var map))
                return false;
            if (TryPublish(before, map))
                return true;
        }
    }

    /// <summary>Gets an existing value or atomically adds a factory-produced value.</summary>
    /// <param name="key">The key to locate or add.</param>
    /// <param name="valueFactory">A repeatable value factory that may run more than once under contention.</param>
    /// <returns>The value observed or successfully published for <paramref name="key"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="valueFactory"/> is <see langword="null"/>.</exception>
    public TValue GetOrAdd(TKey key, Func<TKey, TValue> valueFactory)
    {
        ArgumentNullException.ThrowIfNull(valueFactory);
        while (true)
        {
            var before = ReadState();
            if (before.Map.TryGetValue(key, out var existing))
                return existing;

            var value = valueFactory(key);
            if (!before.Map.TryAdd(key, value, out var map))
                continue;
            if (TryPublish(before, map))
                return value;
        }
    }

    /// <summary>Adds a missing key or atomically updates an existing key.</summary>
    /// <param name="key">The key to add or update.</param>
    /// <param name="addFactory">A repeatable factory for a missing key.</param>
    /// <param name="updateFactory">A repeatable factory receiving the value from the attempted generation.</param>
    /// <returns>The successfully published value.</returns>
    /// <exception cref="ArgumentNullException">Either factory is <see langword="null"/>.</exception>
    public TValue AddOrUpdate(
        TKey key,
        Func<TKey, TValue> addFactory,
        Func<TKey, TValue, TValue> updateFactory)
    {
        ArgumentNullException.ThrowIfNull(addFactory);
        ArgumentNullException.ThrowIfNull(updateFactory);
        while (true)
        {
            var before = ReadState();
            var value = before.Map.TryGetValue(key, out var existing)
                ? updateFactory(key, existing)
                : addFactory(key);
            var map = before.Map.SetItem(key, value);
            if (ReferenceEquals(map, before.Map))
                return existing!;
            if (TryPublish(before, map))
                return value;
        }
    }

    /// <summary>Replaces a value only when it equals a comparison value.</summary>
    /// <param name="key">The key to update.</param>
    /// <param name="newValue">The replacement value.</param>
    /// <param name="comparisonValue">The required current value.</param>
    /// <returns><see langword="true"/> when the comparison matched and the update succeeded.</returns>
    public bool TryUpdate(TKey key, TValue newValue, TValue comparisonValue)
    {
        var values = EqualityComparer<TValue>.Default;
        while (true)
        {
            var before = ReadState();
            if (!before.Map.TryGetValue(key, out var existing) || !values.Equals(existing, comparisonValue))
                return false;
            var map = before.Map.SetItem(key, newValue);
            if (ReferenceEquals(map, before.Map))
                return true;
            if (TryPublish(before, map))
                return true;
        }
    }

    /// <summary>Attempts to remove a key/value pair atomically.</summary>
    /// <param name="key">The key to remove.</param>
    /// <param name="value">The value removed on success; otherwise, the default value.</param>
    /// <returns><see langword="true"/> when removed; otherwise, <see langword="false"/>.</returns>
    public bool TryRemove(TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        while (true)
        {
            var before = ReadState();
            if (!before.Map.TryRemove(key, out var map, out var removedValue))
            {
                value = default;
                return false;
            }
            if (TryPublish(before, map))
            {
                value = removedValue;
                return true;
            }
        }
    }

    /// <summary>Atomically publishes an empty generation when the trie is nonempty.</summary>
    public void Clear()
    {
        while (true)
        {
            var before = ReadState();
            var map = before.Map.Clear();
            if (ReferenceEquals(map, before.Map))
                return;
            if (TryPublish(before, map))
                return;
        }
    }

    /// <summary>Returns an enumerator bound to one immutable generation.</summary>
    /// <returns>A stable allocation-free CHAMP enumerator.</returns>
    public PersistentHashMap<TKey, TValue>.Enumerator GetEnumerator() => Snapshot().GetEnumerator();

    IEnumerator<KeyValuePair<TKey, TValue>> IEnumerable<KeyValuePair<TKey, TValue>>.GetEnumerator() =>
        GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private State ReadState() => Volatile.Read(ref _state);

    private bool TryPublish(State before, PersistentHashMap<TKey, TValue> map)
    {
        var after = new State(map, checked(before.Generation + 1));
        return ReferenceEquals(Interlocked.CompareExchange(ref _state, after, before), before);
    }

    private sealed class State(PersistentHashMap<TKey, TValue> map, long generation)
    {
        internal PersistentHashMap<TKey, TValue> Map { get; } = map;

        internal long Generation { get; } = generation;
    }
}
