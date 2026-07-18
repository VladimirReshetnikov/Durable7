using System.Collections;
using System.Diagnostics.CodeAnalysis;
using Tools.DataStructures.Hamt.Internal;

namespace Tools.DataStructures.Hamt;

/// <summary>Represents an immutable map from signed 32-bit integer keys using a big-endian Patricia trie.</summary>
/// <typeparam name="TValue">The value type.</typeparam>
public sealed partial class PersistentIntMap<TValue> : IReadOnlyDictionary<int, TValue>
{
    private readonly PatriciaMapCore<int, TValue, Int32PatriciaKey> _core;

    private PersistentIntMap(PatriciaMapCore<int, TValue, Int32PatriciaKey> core) => _core = core;

    /// <summary>Gets the shared empty map.</summary>
    public static PersistentIntMap<TValue> Empty { get; } = new(PatriciaMapCore<int, TValue, Int32PatriciaKey>.Empty);

    /// <summary>Gets the number of entries.</summary>
    public int Count => _core.Count;

    /// <summary>Gets whether the map is empty.</summary>
    public bool IsEmpty => Count == 0;

    /// <summary>Gets the keys in ascending signed order.</summary>
    public IEnumerable<int> Keys => this.Select(entry => entry.Key);

    /// <summary>Gets the values in ascending signed-key order.</summary>
    public IEnumerable<TValue> Values => this.Select(entry => entry.Value);

    /// <summary>Gets the value associated with a key.</summary>
    /// <param name="key">The key to locate.</param>
    /// <exception cref="KeyNotFoundException">The key is absent.</exception>
    public TValue this[int key] => TryGetValue(key, out var value)
        ? value
        : throw new KeyNotFoundException($"The key '{key}' was not present in the map.");

    internal object? RootIdentity => _core.RootIdentity;

    /// <summary>Creates a map from entries with last-wins duplicate semantics.</summary>
    /// <param name="items">The entries to add in enumeration order.</param>
    /// <returns>A map containing the entries.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static PersistentIntMap<TValue> CreateRange(IEnumerable<KeyValuePair<int, TValue>> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = Empty;
        foreach (var (key, value) in items)
            result = result.SetItem(key, value);
        return result;
    }

    /// <summary>Determines whether a key is present.</summary>
    /// <param name="key">The key to locate.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool ContainsKey(int key) => _core.TryGetValue(key, out _);

    /// <summary>Tries to get a value.</summary>
    /// <param name="key">The key to locate.</param>
    /// <param name="value">The associated value on success.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool TryGetValue(int key, [MaybeNullWhen(false)] out TValue value) => _core.TryGetValue(key, out value);

    /// <summary>Adds or replaces an entry, preserving identity for an equal-value no-op.</summary>
    /// <param name="key">The key.</param>
    /// <param name="value">The value.</param>
    /// <returns>The updated map.</returns>
    public PersistentIntMap<TValue> SetItem(int key, TValue value) =>
        WithCore(_core.SetItem(key, value, overwrite: true, out _));

    /// <summary>Adds an entry and throws if the key exists.</summary>
    /// <param name="key">The key.</param>
    /// <param name="value">The value.</param>
    /// <returns>The updated map.</returns>
    /// <exception cref="ArgumentException">The key exists.</exception>
    public PersistentIntMap<TValue> Add(int key, TValue value)
    {
        if (!TryAdd(key, value, out var result))
            throw new ArgumentException($"The key '{key}' is already present.", nameof(key));
        return result;
    }

    /// <summary>Attempts to add an entry.</summary>
    /// <param name="key">The key.</param>
    /// <param name="value">The value.</param>
    /// <param name="result">The updated map on success; otherwise, this map.</param>
    /// <returns><see langword="true"/> when added.</returns>
    public bool TryAdd(int key, TValue value, out PersistentIntMap<TValue> result)
    {
        var core = _core.SetItem(key, value, overwrite: false, out var added);
        result = added ? WithCore(core) : this;
        return added;
    }

    /// <summary>Removes a key when present.</summary>
    /// <param name="key">The key.</param>
    /// <returns>The updated map, or this map when absent.</returns>
    public PersistentIntMap<TValue> Remove(int key) => WithCore(_core.Remove(key, out _, out _));

    /// <summary>Attempts to remove a key and return its value.</summary>
    /// <param name="key">The key.</param>
    /// <param name="result">The updated map on success; otherwise, this map.</param>
    /// <param name="value">The removed value on success.</param>
    /// <returns><see langword="true"/> when removed.</returns>
    public bool TryRemove(int key, out PersistentIntMap<TValue> result, [MaybeNullWhen(false)] out TValue value)
    {
        var core = _core.Remove(key, out var removed, out value);
        result = removed ? WithCore(core) : this;
        return removed;
    }

    /// <summary>Returns the shared empty map, or this map when already empty.</summary>
    /// <returns>An empty map.</returns>
    public PersistentIntMap<TValue> Clear() => IsEmpty ? this : Empty;

    /// <summary>Unions another map with right-biased values.</summary>
    /// <param name="other">The other map.</param>
    /// <returns>The union.</returns>
    public PersistentIntMap<TValue> Union(PersistentIntMap<TValue> other) =>
        WithCore(_core.UnionRight(other?._core ?? throw new ArgumentNullException(nameof(other))));

    /// <summary>Unions another map with a combining function for shared keys.</summary>
    /// <param name="other">The other map.</param>
    /// <param name="combine">The key/left/right combining function.</param>
    /// <returns>The union.</returns>
    public PersistentIntMap<TValue> Union(
        PersistentIntMap<TValue> other,
        Func<int, TValue, TValue, TValue> combine)
    {
        ArgumentNullException.ThrowIfNull(other);
        ArgumentNullException.ThrowIfNull(combine);
        return WithCore(_core.Union(other._core, combine));
    }

    /// <summary>Intersects with another map, retaining receiver values.</summary>
    /// <param name="other">The other map.</param>
    /// <returns>The intersection.</returns>
    public PersistentIntMap<TValue> Intersect(PersistentIntMap<TValue> other) =>
        WithCore(_core.IntersectLeft(other?._core ?? throw new ArgumentNullException(nameof(other))));

    /// <summary>Intersects with another map using a combining function.</summary>
    /// <param name="other">The other map.</param>
    /// <param name="combine">The key/left/right combining function.</param>
    /// <returns>The intersection.</returns>
    public PersistentIntMap<TValue> Intersect(
        PersistentIntMap<TValue> other,
        Func<int, TValue, TValue, TValue> combine)
    {
        ArgumentNullException.ThrowIfNull(other);
        ArgumentNullException.ThrowIfNull(combine);
        return WithCore(_core.Intersect(other._core, combine));
    }

    /// <summary>Removes every key present in another map.</summary>
    /// <param name="other">The other map.</param>
    /// <returns>The difference.</returns>
    public PersistentIntMap<TValue> Except(PersistentIntMap<TValue> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithCore(_core.Except(other._core));
    }

    /// <summary>Returns entries in ascending signed-key order.</summary>
    /// <returns>An enumerator.</returns>
    public IEnumerator<KeyValuePair<int, TValue>> GetEnumerator() => _core.Enumerate().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private PersistentIntMap<TValue> WithCore(PatriciaMapCore<int, TValue, Int32PatriciaKey> core) =>
        ReferenceEquals(core, _core) ? this : ReferenceEquals(core, PatriciaMapCore<int, TValue, Int32PatriciaKey>.Empty) ? Empty : new(core);
}
