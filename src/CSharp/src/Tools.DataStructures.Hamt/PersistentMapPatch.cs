using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.Hamt;

/// <summary>Represents either an absent map value or a present value, including a present null.</summary>
/// <typeparam name="TValue">The value type.</typeparam>
public readonly struct MapPatchValue<TValue>
{
    private readonly TValue? _value;

    private MapPatchValue(bool isPresent, TValue? value)
    {
        IsPresent = isPresent;
        _value = value;
    }

    /// <summary>Gets the absent value.</summary>
    public static MapPatchValue<TValue> Absent => default;

    /// <summary>Gets whether a value is present.</summary>
    public bool IsPresent { get; }

    /// <summary>Gets the present value.</summary>
    /// <exception cref="InvalidOperationException">This instance represents absence.</exception>
    public TValue Value => IsPresent
        ? _value!
        : throw new InvalidOperationException("The patch value is absent.");

    /// <summary>Creates a present patch value.</summary>
    /// <param name="value">The value, which may be null when <typeparamref name="TValue"/> permits it.</param>
    /// <returns>A presence-safe wrapper containing <paramref name="value"/>.</returns>
    public static MapPatchValue<TValue> Present(TValue value) => new(true, value);

    /// <inheritdoc/>
    public override string? ToString() => IsPresent ? _value?.ToString() : "<absent>";
}

/// <summary>Describes one presence-safe change in a persistent map patch.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <param name="Key">The affected stored key representative.</param>
/// <param name="Before">The value expected before application.</param>
/// <param name="After">The value to publish after application.</param>
public readonly record struct MapPatchEntry<TKey, TValue>(
    TKey Key,
    MapPatchValue<TValue> Before,
    MapPatchValue<TValue> After);

/// <summary>
/// Represents an immutable, invertible set of strict changes between compatible persistent hash maps.
/// </summary>
/// <typeparam name="TKey">The map key type.</typeparam>
/// <typeparam name="TValue">The map value type.</typeparam>
/// <remarks>
/// A patch records both the expected and replacement presence state for every changed key. Strict
/// application first validates every expectation and publishes no successor when any key conflicts.
/// Key policies and value-comparison policies are retained by object identity so composition cannot
/// silently mix incompatible equality domains.
/// </remarks>
[DebuggerDisplay("Count = {Count}")]
public sealed class PersistentMapPatch<TKey, TValue> :
    IEnumerable<MapPatchEntry<TKey, TValue>>
{
    private static readonly PersistentMapPatch<TKey, TValue> EmptyInstance = new(
        PersistentHashMap<TKey, Change>.Empty,
        EqualityComparer<TValue>.Default);

    private readonly PersistentHashMap<TKey, Change> _changes;

    private PersistentMapPatch(
        PersistentHashMap<TKey, Change> changes,
        IEqualityComparer<TValue> valueComparer)
    {
        _changes = changes;
        ValueComparer = valueComparer;
    }

    private readonly record struct Change(
        MapPatchValue<TValue> Before,
        MapPatchValue<TValue> After);

    /// <summary>Gets the shared empty patch using default key and value comparers.</summary>
    public static PersistentMapPatch<TKey, TValue> Empty => EmptyInstance;

    /// <summary>Gets the number of changed key classes.</summary>
    public int Count => _changes.Count;

    /// <summary>Gets whether the patch contains no changes.</summary>
    public bool IsEmpty => _changes.IsEmpty;

    /// <summary>Gets the equality comparer defining key equivalence.</summary>
    public IEqualityComparer<TKey> KeyComparer => _changes.Comparer;

    /// <summary>Gets the equality comparer used for expected and replacement values.</summary>
    public IEqualityComparer<TValue> ValueComparer { get; }

    /// <summary>Gets the changed stored keys in stable-for-this-version trie order.</summary>
    public IEnumerable<TKey> Keys => _changes.Keys;

    /// <summary>Creates an empty patch retaining independent key and value comparers.</summary>
    /// <param name="keyComparer">The key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <returns>An empty patch with the selected policies.</returns>
    public static PersistentMapPatch<TKey, TValue> Create(
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

        return new(PersistentHashMap<TKey, Change>.Create(keyComparer), valueComparer);
    }

    /// <summary>Creates a patch from entries, rejecting duplicate changed keys.</summary>
    /// <param name="entries">The changes to add.</param>
    /// <param name="keyComparer">The key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <returns>A patch containing every non-no-op entry.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="entries"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentException">Two non-no-op entries have equivalent keys.</exception>
    public static PersistentMapPatch<TKey, TValue> CreateRange(
        IEnumerable<MapPatchEntry<TKey, TValue>> entries,
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var result = Create(keyComparer, valueComparer);
        foreach (var entry in entries)
            result = result.Add(entry);
        return result;
    }

    /// <summary>Computes the strict patch that transforms one compatible map into another.</summary>
    /// <param name="source">The expected source map.</param>
    /// <param name="target">The desired target map.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <returns>A presence-safe persistent patch.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="source"/> or <paramref name="target"/> is null.</exception>
    /// <exception cref="ArgumentException">The maps do not retain the same key-comparer object.</exception>
    public static PersistentMapPatch<TKey, TValue> Between(
        PersistentHashMap<TKey, TValue> source,
        PersistentHashMap<TKey, TValue> target,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(target);
        valueComparer ??= EqualityComparer<TValue>.Default;

        var result = Create(source.Comparer, valueComparer);
        foreach (var difference in source.Diff(target, valueComparer))
        {
            var entry = difference.Kind switch
            {
                MapDifferenceKind.Added => new MapPatchEntry<TKey, TValue>(
                    difference.Key,
                    MapPatchValue<TValue>.Absent,
                    MapPatchValue<TValue>.Present(difference.NewValue!)),
                MapDifferenceKind.Removed => new MapPatchEntry<TKey, TValue>(
                    difference.Key,
                    MapPatchValue<TValue>.Present(difference.OldValue!),
                    MapPatchValue<TValue>.Absent),
                _ => new MapPatchEntry<TKey, TValue>(
                    difference.Key,
                    MapPatchValue<TValue>.Present(difference.OldValue!),
                    MapPatchValue<TValue>.Present(difference.NewValue!)),
            };
            result = result.Add(entry);
        }

        return result;
    }

    /// <summary>Determines whether an equivalent changed key exists.</summary>
    /// <param name="key">The key to find.</param>
    /// <returns><see langword="true"/> when a change exists.</returns>
    public bool ContainsKey(TKey key) => _changes.ContainsKey(key);

    /// <summary>Tries to retrieve the change for an equivalent key.</summary>
    /// <param name="key">The key to find.</param>
    /// <param name="entry">The stored change and key representative when found.</param>
    /// <returns><see langword="true"/> when a change exists.</returns>
    public bool TryGetEntry(
        TKey key,
        out MapPatchEntry<TKey, TValue> entry)
    {
        if (TryGetChange(key, out var actualKey, out var change))
        {
            entry = new(actualKey, change.Before, change.After);
            return true;
        }

        entry = default;
        return false;
    }

    /// <summary>Adds a non-no-op change, rejecting an equivalent changed key.</summary>
    /// <param name="entry">The change to add.</param>
    /// <returns>This patch for a semantic no-op; otherwise, the enlarged patch.</returns>
    /// <exception cref="ArgumentException">An equivalent changed key already exists.</exception>
    public PersistentMapPatch<TKey, TValue> Add(MapPatchEntry<TKey, TValue> entry)
    {
        if (ValuesEqual(entry.Before, entry.After))
            return this;

        return new(
            _changes.Add(entry.Key, new(entry.Before, entry.After)),
            ValueComparer);
    }

    /// <summary>Attempts to add a change.</summary>
    /// <param name="entry">The change to add.</param>
    /// <param name="result">The resulting patch, or this patch when the entry is a no-op or duplicate.</param>
    /// <returns><see langword="true"/> only when the change was added.</returns>
    public bool TryAdd(
        MapPatchEntry<TKey, TValue> entry,
        out PersistentMapPatch<TKey, TValue> result)
    {
        if (ValuesEqual(entry.Before, entry.After)
            || _changes.ContainsKey(entry.Key))
        {
            result = this;
            return false;
        }

        result = new(
            _changes.Add(entry.Key, new(entry.Before, entry.After)),
            ValueComparer);
        return true;
    }

    /// <summary>Removes a change for an equivalent key.</summary>
    /// <param name="key">The changed key to remove.</param>
    /// <returns>This patch when absent; otherwise, the reduced patch.</returns>
    public PersistentMapPatch<TKey, TValue> Remove(TKey key)
    {
        var updated = _changes.Remove(key);
        return ReferenceEquals(updated, _changes) ? this : new(updated, ValueComparer);
    }

    /// <summary>Returns an empty patch retaining the current policies.</summary>
    /// <returns>This patch when already empty; otherwise, an empty patch.</returns>
    public PersistentMapPatch<TKey, TValue> Clear() =>
        IsEmpty ? this : Create(KeyComparer, ValueComparer);

    /// <summary>Strictly applies this patch to a compatible source map.</summary>
    /// <param name="source">The source map whose current values must match every expectation.</param>
    /// <returns>The patched persistent map.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="source"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentException">The source retains a different key-comparer object.</exception>
    /// <exception cref="InvalidOperationException">At least one expected source value does not match.</exception>
    public PersistentHashMap<TKey, TValue> Apply(PersistentHashMap<TKey, TValue> source)
    {
        ArgumentNullException.ThrowIfNull(source);
        EnsureCompatible(source);
        if (!TryValidate(source, out var conflictingKey))
            throw new InvalidOperationException($"The source map conflicts with the patch at key '{conflictingKey}'.");
        return ApplyCore(source);
    }

    /// <summary>Attempts to strictly apply this patch to a compatible source map.</summary>
    /// <param name="source">The source map whose current values must match every expectation.</param>
    /// <param name="result">The patched map on success; otherwise, <paramref name="source"/>.</param>
    /// <param name="conflictingKey">The first conflicting stored patch key, or the default on success.</param>
    /// <returns><see langword="true"/> when every expectation matched and the patch was applied.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="source"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentException">The source retains a different key-comparer object.</exception>
    public bool TryApply(
        PersistentHashMap<TKey, TValue> source,
        out PersistentHashMap<TKey, TValue> result,
        [MaybeNullWhen(true)] out TKey conflictingKey)
    {
        ArgumentNullException.ThrowIfNull(source);
        EnsureCompatible(source);
        if (!TryValidate(source, out conflictingKey))
        {
            result = source;
            return false;
        }

        result = ApplyCore(source);
        conflictingKey = default;
        return true;
    }

    /// <summary>Returns the patch that reverses every change in this patch.</summary>
    /// <returns>An inverse patch retaining the same policies.</returns>
    public PersistentMapPatch<TKey, TValue> Invert()
    {
        if (IsEmpty)
            return this;

        var result = Create(KeyComparer, ValueComparer);
        foreach (var entry in this)
            result = result.Add(new(entry.Key, entry.After, entry.Before));
        return result;
    }

    /// <summary>Composes this patch followed by another compatible patch.</summary>
    /// <param name="next">The patch applied after this patch.</param>
    /// <returns>A strict patch with the same net effect.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="next"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentException">
    /// The policies differ, or a shared key's intermediate values do not match.
    /// </exception>
    public PersistentMapPatch<TKey, TValue> Compose(PersistentMapPatch<TKey, TValue> next)
    {
        ArgumentNullException.ThrowIfNull(next);
        if (!ReferenceEquals(KeyComparer, next.KeyComparer)
            || !ReferenceEquals(ValueComparer, next.ValueComparer))
        {
            throw new ArgumentException("Patches must retain the same key and value comparer objects.", nameof(next));
        }

        var result = this;
        foreach (var nextEntry in next)
        {
            if (!result.TryGetChange(nextEntry.Key, out var actualKey, out var current))
            {
                result = result.Add(nextEntry);
                continue;
            }

            if (!ValuesEqual(current.After, nextEntry.Before))
            {
                throw new ArgumentException(
                    $"The patches disagree about the intermediate value at key '{actualKey}'.",
                    nameof(next));
            }

            result = result.Remove(actualKey);
            result = result.Add(new(actualKey, current.Before, nextEntry.After));
        }

        return result;
    }

    /// <summary>Materializes the changed entries in stable-for-this-version trie order.</summary>
    /// <returns>An array containing every changed entry.</returns>
    public MapPatchEntry<TKey, TValue>[] ToArray()
    {
        var result = new MapPatchEntry<TKey, TValue>[Count];
        var index = 0;
        foreach (var entry in this)
            result[index++] = entry;
        return result;
    }

    /// <summary>Returns an enumerator over the immutable patch.</summary>
    /// <returns>An enumerator over changed entries.</returns>
    public IEnumerator<MapPatchEntry<TKey, TValue>> GetEnumerator() => EnumerateEntries().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal void ValidateInvariants()
    {
        foreach (var (_, change) in _changes)
        {
            if (ValuesEqual(change.Before, change.After))
                throw new InvalidOperationException("A map patch stores a semantic no-op change.");
        }
    }

    private bool TryGetChange(TKey key, out TKey actualKey, out Change change) =>
        _changes.TryGetEntry(key, out actualKey!, out change);

    private bool ValuesEqual(MapPatchValue<TValue> left, MapPatchValue<TValue> right) =>
        left.IsPresent == right.IsPresent
        && (!left.IsPresent || ValueComparer.Equals(left.Value, right.Value));

    private void EnsureCompatible(PersistentHashMap<TKey, TValue> source)
    {
        if (!ReferenceEquals(KeyComparer, source.Comparer))
            throw new ArgumentException("The patch and source map must retain the same key comparer object.", nameof(source));
    }

    private bool TryValidate(
        PersistentHashMap<TKey, TValue> source,
        [MaybeNullWhen(true)] out TKey conflictingKey)
    {
        foreach (var (key, change) in _changes)
        {
            var found = source.TryGetValue(key, out var current);
            if (found != change.Before.IsPresent
                || (found && !ValueComparer.Equals(current, change.Before.Value)))
            {
                conflictingKey = key;
                return false;
            }
        }

        conflictingKey = default;
        return true;
    }

    private PersistentHashMap<TKey, TValue> ApplyCore(PersistentHashMap<TKey, TValue> source)
    {
        var result = source;
        foreach (var (key, change) in _changes)
        {
            result = change.After.IsPresent
                ? result.SetItem(key, change.After.Value)
                : result.Remove(key);
        }

        return result;
    }

    private IEnumerable<MapPatchEntry<TKey, TValue>> EnumerateEntries()
    {
        foreach (var (key, change) in _changes)
            yield return new(key, change.Before, change.After);
    }
}
