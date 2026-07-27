// Represents an immutable map from signed 64-bit integer keys using a big-endian Patricia trie.

using System.Collections;
using System.Diagnostics.CodeAnalysis;
using Durable7.Hamt.Internal;

namespace Durable7.Hamt;

/// <summary>Represents an immutable map from signed 64-bit integer keys using a big-endian Patricia trie.</summary>
/// <typeparam name="TValue">The value type.</typeparam>
public sealed partial class PersistentLongMap<TValue> : IReadOnlyDictionary<long, TValue>
{
    private readonly PatriciaMapCore<long, TValue, Int64PatriciaKey> _core;

    private PersistentLongMap(PatriciaMapCore<long, TValue, Int64PatriciaKey> core) => _core = core;

    /// <summary>Gets the shared empty map.</summary>
    public static PersistentLongMap<TValue> Empty { get; } = new(PatriciaMapCore<long, TValue, Int64PatriciaKey>.Empty);

    /// <summary>Gets the number of entries.</summary>
    public int Count => _core.Count;

    /// <summary>Gets whether the map is empty.</summary>
    public bool IsEmpty => Count == 0;

    /// <summary>Gets the keys in ascending signed order.</summary>
    public IEnumerable<long> Keys => this.Select(entry => entry.Key);

    /// <summary>Gets the values in ascending signed-key order.</summary>
    public IEnumerable<TValue> Values => this.Select(entry => entry.Value);

    /// <summary>Gets the value associated with a key.</summary>
    /// <param name="key">The key to locate.</param>
    /// <exception cref="KeyNotFoundException">The key is absent.</exception>
    public TValue this[long key] => TryGetValue(key, out var value)
        ? value
        : throw new KeyNotFoundException($"The key '{key}' was not present in the map.");

    /// <summary>Gets the root node's identity, for tests that a no-op shared rather than copied.</summary>
    internal object? RootIdentity => _core.RootIdentity;

    /// <summary>Creates a map from entries with last-wins duplicate semantics.</summary>
    /// <param name="items">The entries to add.</param>
    /// <returns>A map containing the entries.</returns>
    public static PersistentLongMap<TValue> CreateRange(IEnumerable<KeyValuePair<long, TValue>> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = Empty;
        foreach (var (key, value) in items)
            result = result.SetItem(key, value);
        return result;
    }

    /// <summary>Determines whether a key is present.</summary>
    /// <param name="key">The key.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool ContainsKey(long key) => _core.TryGetValue(key, out _);

    /// <summary>Tries to get a value.</summary>
    /// <param name="key">The key.</param>
    /// <param name="value">The associated value on success.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool TryGetValue(long key, [MaybeNullWhen(false)] out TValue value) => _core.TryGetValue(key, out value);

    /// <summary>Adds or replaces an entry.</summary>
    /// <param name="key">The key.</param>
    /// <param name="value">The value.</param>
    /// <returns>The updated map.</returns>
    public PersistentLongMap<TValue> SetItem(long key, TValue value) =>
        WithCore(_core.SetItem(key, value, overwrite: true, out _));

    /// <summary>Adds an entry and throws if the key exists.</summary>
    /// <param name="key">The key.</param>
    /// <param name="value">The value.</param>
    /// <returns>The updated map.</returns>
    /// <exception cref="ArgumentException">The key exists.</exception>
    public PersistentLongMap<TValue> Add(long key, TValue value)
    {
        if (!TryAdd(key, value, out var result))
            throw new ArgumentException($"The key '{key}' is already present.", nameof(key));
        return result;
    }

    /// <summary>Attempts to add an entry.</summary>
    /// <param name="key">The key.</param>
    /// <param name="value">The value.</param>
    /// <param name="result">The updated map on success.</param>
    /// <returns><see langword="true"/> when added.</returns>
    public bool TryAdd(long key, TValue value, out PersistentLongMap<TValue> result)
    {
        var core = _core.SetItem(key, value, overwrite: false, out var added);
        result = added ? WithCore(core) : this;
        return added;
    }

    /// <summary>Removes a key when present.</summary>
    /// <param name="key">The key.</param>
    /// <returns>The updated map.</returns>
    public PersistentLongMap<TValue> Remove(long key) => WithCore(_core.Remove(key, out _, out _));

    /// <summary>Attempts to remove a key.</summary>
    /// <param name="key">The key.</param>
    /// <param name="result">The updated map on success.</param>
    /// <param name="value">The removed value on success.</param>
    /// <returns><see langword="true"/> when removed.</returns>
    public bool TryRemove(long key, out PersistentLongMap<TValue> result, [MaybeNullWhen(false)] out TValue value)
    {
        var core = _core.Remove(key, out var removed, out value);
        result = removed ? WithCore(core) : this;
        return removed;
    }

    /// <summary>Returns the shared empty map, or this map when already empty.</summary>
    /// <returns>An empty map.</returns>
    public PersistentLongMap<TValue> Clear() => IsEmpty ? this : Empty;

    /// <summary>Unions another map with right-biased values.</summary>
    /// <param name="other">The other map.</param>
    /// <returns>The union.</returns>
    public PersistentLongMap<TValue> Union(PersistentLongMap<TValue> other) =>
        WithCore(_core.UnionRight(other?._core ?? throw new ArgumentNullException(nameof(other))));

    /// <summary>Unions another map with a combining function.</summary>
    /// <param name="other">The other map.</param>
    /// <param name="combine">The key/left/right function.</param>
    /// <returns>The union.</returns>
    public PersistentLongMap<TValue> Union(
        PersistentLongMap<TValue> other,
        Func<long, TValue, TValue, TValue> combine)
    {
        ArgumentNullException.ThrowIfNull(other);
        ArgumentNullException.ThrowIfNull(combine);
        return WithCore(_core.Union(other._core, combine));
    }

    /// <summary>Intersects with another map, retaining receiver values.</summary>
    /// <param name="other">The other map.</param>
    /// <returns>The intersection.</returns>
    public PersistentLongMap<TValue> Intersect(PersistentLongMap<TValue> other) =>
        WithCore(_core.IntersectLeft(other?._core ?? throw new ArgumentNullException(nameof(other))));

    /// <summary>Intersects with another map using a combining function.</summary>
    /// <param name="other">The other map.</param>
    /// <param name="combine">The key/left/right function.</param>
    /// <returns>The intersection.</returns>
    public PersistentLongMap<TValue> Intersect(
        PersistentLongMap<TValue> other,
        Func<long, TValue, TValue, TValue> combine)
    {
        ArgumentNullException.ThrowIfNull(other);
        ArgumentNullException.ThrowIfNull(combine);
        return WithCore(_core.Intersect(other._core, combine));
    }

    /// <summary>Removes every key present in another map.</summary>
    /// <param name="other">The other map.</param>
    /// <returns>The difference.</returns>
    public PersistentLongMap<TValue> Except(PersistentLongMap<TValue> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithCore(_core.Except(other._core));
    }

    /// <summary>Returns entries in ascending signed-key order.</summary>
    /// <returns>An enumerator.</returns>
    public IEnumerator<KeyValuePair<long, TValue>> GetEnumerator() => _core.Enumerate().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private PersistentLongMap<TValue> WithCore(PatriciaMapCore<long, TValue, Int64PatriciaKey> core) =>
        ReferenceEquals(core, _core) ? this : ReferenceEquals(core, PatriciaMapCore<long, TValue, Int64PatriciaKey>.Empty) ? Empty : new(core);
}
