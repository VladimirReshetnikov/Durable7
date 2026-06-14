using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// Measures a dictionary entry by its key for an order-statistic key-ordered map: count adds and the key is
/// right-biased (so a prefix's measure carries the count and its last entry's key), enabling logarithmic
/// key search and order-statistic access while ignoring the value.
/// </summary>
/// <typeparam name="TKey">Key type.</typeparam>
/// <typeparam name="TValue">Value type.</typeparam>
public readonly struct EntryMeasure<TKey, TValue> : IMeasure<KeyValuePair<TKey, TValue>, RankedKey<TKey>>
{
    /// <inheritdoc/>
    public static RankedKey<TKey> Empty => new(0, Optional<TKey>.None);

    /// <inheritdoc/>
    public static RankedKey<TKey> Measure(KeyValuePair<TKey, TValue> element) =>
        new(1, Optional<TKey>.Some(element.Key));

    /// <inheritdoc/>
    public static RankedKey<TKey> Combine(RankedKey<TKey> left, RankedKey<TKey> right) =>
        new(left.Count + right.Count, right.Key.HasValue ? right.Key : left.Key);
}

/// <summary>
/// An immutable, persistent sorted dictionary (unique keys, ordered by key) backed by an order-statistic
/// measured finger tree: logarithmic lookup/insert/replace/remove and key-neighbor queries, order-statistic
/// access by rank, key-range extraction, and constant-time count and endpoints.
/// </summary>
/// <typeparam name="TKey">Key type.</typeparam>
/// <typeparam name="TValue">Value type.</typeparam>
/// <remarks>
/// <para>
/// The key-ordered analogue of <see cref="SortedSet{T}"/>, built on
/// <see cref="EntryMeasure{TKey, TValue}"/>. The measure's combine is order-independent, so the dictionary
/// stores a runtime <see cref="IComparer{TKey}"/> applied in the split predicates; it defaults to
/// <see cref="Comparer{TKey}.Default"/>. Implements <see cref="IReadOnlyDictionary{TKey, TValue}"/>.
/// </para>
/// <para>
/// Complexity: <see cref="Count"/>, <see cref="IsEmpty"/>, <see cref="MinEntry"/>, <see cref="MaxEntry"/>
/// are O(1); <see cref="SetItem"/>, <see cref="Add"/>, <see cref="Remove(TKey)"/>, the key indexer,
/// <see cref="ContainsKey"/>, <see cref="TryGetValue"/>, <see cref="IndexOfKey"/>, <see cref="EntryAt"/>,
/// the navigable queries, and <see cref="GetRange"/> are O(log n). Instances are immutable and safe for
/// concurrent reads when the supplied comparer is safe for concurrent <c>Compare</c> calls.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// var map = SortedDictionary&lt;int, string&gt;.Empty
///     .SetItem(3, "three")
///     .SetItem(1, "one")
///     .SetItem(2, "two");
///
/// var two = map[2];                          // "two"
/// map.TryCeilingEntry(2, out var atLeast2);  // (2, "two")
/// var firstByKey = map.EntryAt(0);           // (1, "one")
/// </code>
/// </example>
public sealed class SortedDictionary<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
{
    private static readonly SortedDictionary<TKey, TValue> EmptyDefault = new(
        FingerTree<KeyValuePair<TKey, TValue>, RankedKey<TKey>, EntryMeasure<TKey, TValue>>.Empty,
        Comparer<TKey>.Default);

    private readonly FingerTree<KeyValuePair<TKey, TValue>, RankedKey<TKey>, EntryMeasure<TKey, TValue>> _tree;
    private readonly IComparer<TKey> _comparer;

    private SortedDictionary(
        FingerTree<KeyValuePair<TKey, TValue>, RankedKey<TKey>, EntryMeasure<TKey, TValue>> tree,
        IComparer<TKey> comparer)
    {
        _tree = tree;
        _comparer = comparer;
    }

    /// <summary>Gets the empty dictionary ordered by <see cref="Comparer{TKey}.Default"/>.</summary>
    public static SortedDictionary<TKey, TValue> Empty => EmptyDefault;

    /// <summary>Creates an empty dictionary with the given key comparer (or the default when null).</summary>
    /// <param name="comparer">Key order, or <see langword="null"/> for <see cref="Comparer{TKey}.Default"/>.</param>
    /// <returns>An empty dictionary using the chosen order.</returns>
    public static SortedDictionary<TKey, TValue> Create(IComparer<TKey>? comparer = null) =>
        comparer is null || ReferenceEquals(comparer, Comparer<TKey>.Default)
            ? EmptyDefault
            : new(FingerTree<KeyValuePair<TKey, TValue>, RankedKey<TKey>, EntryMeasure<TKey, TValue>>.Empty, comparer);

    /// <summary>
    /// Creates a dictionary from key-value entries, ordered by key. On duplicate keys the last entry wins.
    /// O(n log n).
    /// </summary>
    /// <param name="entries">Entries to include.</param>
    /// <param name="comparer">Key order, or <see langword="null"/> for <see cref="Comparer{TKey}.Default"/>.</param>
    /// <returns>A dictionary of the entries.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="entries"/> is <see langword="null"/>.</exception>
    public static SortedDictionary<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> entries,
        IComparer<TKey>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var order = comparer ?? Comparer<TKey>.Default;
        var sorted = entries.OrderBy(entry => entry.Key, order).ToList(); // stable, so a key's last entry stays last
        var tree = FingerTree<KeyValuePair<TKey, TValue>, RankedKey<TKey>, EntryMeasure<TKey, TValue>>.Empty;
        for (var i = 0; i < sorted.Count; i++)
        {
            // Drop all but the last entry of each equal-key run (last-wins).
            if (i + 1 < sorted.Count && order.Compare(sorted[i].Key, sorted[i + 1].Key) == 0)
                continue;
            tree = tree.Append(sorted[i]);
        }

        return new(tree, order);
    }

    /// <summary>Gets the number of entries. O(1).</summary>
    public int Count => _tree.Measure.Count;

    /// <summary>Gets a value indicating whether the dictionary is empty. O(1).</summary>
    public bool IsEmpty => _tree.IsEmpty;

    /// <summary>Gets the comparer defining the key order.</summary>
    public IComparer<TKey> Comparer => _comparer;

    /// <summary>
    /// Gets the keys in ascending order. The sequence is lazy; each enumeration is O(n) and materializes a
    /// snapshot.
    /// </summary>
    public IEnumerable<TKey> Keys => _tree.Select(entry => entry.Key);

    /// <summary>
    /// Gets the values in ascending key order. The sequence is lazy; each enumeration is O(n) and
    /// materializes a snapshot.
    /// </summary>
    public IEnumerable<TValue> Values => _tree.Select(entry => entry.Value);

    /// <summary>Gets the value associated with <paramref name="key"/>. O(log n).</summary>
    /// <param name="key">Key to look up.</param>
    /// <returns>The value for <paramref name="key"/>.</returns>
    /// <exception cref="KeyNotFoundException"><paramref name="key"/> is not present.</exception>
    public TValue this[TKey key] =>
        TryGetValue(key, out var value) ? value : throw new KeyNotFoundException("The key was not present in the dictionary.");

    /// <summary>Gets the least entry by key. O(1).</summary>
    /// <exception cref="InvalidOperationException">The dictionary is empty.</exception>
    public KeyValuePair<TKey, TValue> MinEntry => _tree.IsEmpty ? throw EmptyError() : _tree.First;

    /// <summary>Gets the greatest entry by key. O(1).</summary>
    /// <exception cref="InvalidOperationException">The dictionary is empty.</exception>
    public KeyValuePair<TKey, TValue> MaxEntry => _tree.IsEmpty ? throw EmptyError() : _tree.Last;

    /// <summary>Determines whether <paramref name="key"/> is present. O(log n).</summary>
    /// <param name="key">Key to test.</param>
    /// <returns><see langword="true"/> when present; otherwise <see langword="false"/>.</returns>
    public bool ContainsKey(TKey key) =>
        _tree.TryLocate(new KeyAtLeastPredicate<TKey>(_comparer, key), out _, out var found)
        && _comparer.Compare(found.Key, key) == 0;

    /// <summary>Looks up the value for <paramref name="key"/>. O(log n).</summary>
    /// <param name="key">Key to look up.</param>
    /// <param name="value">The value when present; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when present; otherwise <see langword="false"/>.</returns>
    public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        if (_tree.TryLocate(new KeyAtLeastPredicate<TKey>(_comparer, key), out _, out var found)
            && _comparer.Compare(found.Key, key) == 0)
        {
            value = found.Value;
            return true;
        }

        value = default;
        return false;
    }

    /// <summary>Returns the entry at rank <paramref name="index"/> (the <c>index</c>-th by key). O(log n).</summary>
    /// <param name="index">Zero-based rank.</param>
    /// <returns>The entry with that rank.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count - 1</c>.</exception>
    public KeyValuePair<TKey, TValue> EntryAt(int index)
    {
        if ((uint)index >= (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(index), index, "Rank is outside the dictionary's range.");
        return EntryAtRank(index);
    }

    /// <summary>Returns the rank of <paramref name="key"/> (number of smaller keys), or -1 when absent. O(log n).</summary>
    /// <param name="key">Key to locate.</param>
    /// <returns>The zero-based rank, or -1 when <paramref name="key"/> is not present.</returns>
    public int IndexOfKey(TKey key) =>
        _tree.TryLocate(new KeyAtLeastPredicate<TKey>(_comparer, key), out var before, out var found)
        && _comparer.Compare(found.Key, key) == 0
            ? before.Count
            : -1;

    /// <summary>Adds or replaces the entry for <paramref name="key"/>. O(log n).</summary>
    /// <param name="key">Key to set.</param>
    /// <param name="value">Value to associate.</param>
    /// <returns>A dictionary with <paramref name="key"/> mapped to <paramref name="value"/>.</returns>
    public SortedDictionary<TKey, TValue> SetItem(TKey key, TValue value)
    {
        var entry = new KeyValuePair<TKey, TValue>(key, value);
        var (less, atLeast) = SplitAtLeast(key);
        if (atLeast.TryViewLeft(out var head, out var tail) && _comparer.Compare(head.Key, key) == 0)
            return Wrap(less.Append(entry).Concat(tail));     // replace the existing entry
        return Wrap(less.Append(entry).Concat(atLeast));      // insert a new entry
    }

    /// <summary>Adds a new entry. O(log n).</summary>
    /// <param name="key">Key to add.</param>
    /// <param name="value">Value to associate.</param>
    /// <returns>A dictionary including the new entry.</returns>
    /// <exception cref="ArgumentException"><paramref name="key"/> is already present.</exception>
    public SortedDictionary<TKey, TValue> Add(TKey key, TValue value)
    {
        var (less, atLeast) = SplitAtLeast(key);
        if (!atLeast.IsEmpty && _comparer.Compare(atLeast.First.Key, key) == 0)
            throw new ArgumentException("An entry with the same key already exists.", nameof(key));
        return Wrap(less.Append(new KeyValuePair<TKey, TValue>(key, value)).Concat(atLeast));
    }

    /// <summary>Adds a new entry unless the key is already present. O(log n).</summary>
    /// <param name="key">Key to add.</param>
    /// <param name="value">Value to associate.</param>
    /// <param name="result">The dictionary including the new entry, or the unchanged dictionary.</param>
    /// <returns><see langword="true"/> when the entry was added; otherwise <see langword="false"/>.</returns>
    public bool TryAdd(TKey key, TValue value, out SortedDictionary<TKey, TValue> result)
    {
        var (less, atLeast) = SplitAtLeast(key);
        if (!atLeast.IsEmpty && _comparer.Compare(atLeast.First.Key, key) == 0)
        {
            result = this;
            return false;
        }

        result = Wrap(less.Append(new KeyValuePair<TKey, TValue>(key, value)).Concat(atLeast));
        return true;
    }

    /// <summary>Removes the entry for <paramref name="key"/>, if present. O(log n).</summary>
    /// <param name="key">Key to remove.</param>
    /// <param name="result">The dictionary without the key; otherwise the unchanged dictionary.</param>
    /// <returns><see langword="true"/> when an entry was removed; otherwise <see langword="false"/>.</returns>
    public bool TryRemove(TKey key, out SortedDictionary<TKey, TValue> result)
    {
        var (less, atLeast) = SplitAtLeast(key);
        if (atLeast.TryViewLeft(out var head, out var tail) && _comparer.Compare(head.Key, key) == 0)
        {
            result = Wrap(less.Concat(tail));
            return true;
        }

        result = this;
        return false;
    }

    /// <summary>Removes the entry for <paramref name="key"/>, or returns the unchanged dictionary. O(log n).</summary>
    /// <param name="key">Key to remove.</param>
    /// <returns>The resulting dictionary.</returns>
    public SortedDictionary<TKey, TValue> Remove(TKey key) => TryRemove(key, out var result) ? result : this;

    /// <summary>Finds the entry with the greatest key less than or equal to <paramref name="key"/>. O(log n).</summary>
    /// <param name="key">Reference key.</param>
    /// <param name="entry">The floor entry when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a floor exists; otherwise <see langword="false"/>.</returns>
    public bool TryFloorEntry(TKey key, out KeyValuePair<TKey, TValue> entry)
    {
        // The floor is the entry just before the first one with a key greater than `key`.
        var atMost = CountBelow(new KeyAbovePredicate<TKey>(_comparer, key));
        if (atMost == 0)
        {
            entry = default;
            return false;
        }

        entry = EntryAtRank(atMost - 1);
        return true;
    }

    /// <summary>Finds the entry with the least key greater than or equal to <paramref name="key"/>. O(log n).</summary>
    /// <param name="key">Reference key.</param>
    /// <param name="entry">The ceiling entry when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a ceiling exists; otherwise <see langword="false"/>.</returns>
    public bool TryCeilingEntry(TKey key, out KeyValuePair<TKey, TValue> entry) =>
        _tree.TryLocate(new KeyAtLeastPredicate<TKey>(_comparer, key), out _, out entry);

    /// <summary>Finds the entry with the greatest key strictly less than <paramref name="key"/>. O(log n).</summary>
    /// <param name="key">Reference key.</param>
    /// <param name="entry">The predecessor entry when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a strictly smaller key exists; otherwise <see langword="false"/>.</returns>
    public bool TryLowerEntry(TKey key, out KeyValuePair<TKey, TValue> entry)
    {
        // The strict predecessor is the entry just before the first one with a key at or after `key`.
        var less = CountBelow(new KeyAtLeastPredicate<TKey>(_comparer, key));
        if (less == 0)
        {
            entry = default;
            return false;
        }

        entry = EntryAtRank(less - 1);
        return true;
    }

    /// <summary>Finds the entry with the least key strictly greater than <paramref name="key"/>. O(log n).</summary>
    /// <param name="key">Reference key.</param>
    /// <param name="entry">The successor entry when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a strictly greater key exists; otherwise <see langword="false"/>.</returns>
    public bool TryHigherEntry(TKey key, out KeyValuePair<TKey, TValue> entry) =>
        _tree.TryLocate(new KeyAbovePredicate<TKey>(_comparer, key), out _, out entry);

    /// <summary>Returns the sub-dictionary of entries whose keys lie in the inclusive range <c>[low, high]</c>. O(log n).</summary>
    /// <param name="low">Inclusive lower key bound.</param>
    /// <param name="high">Inclusive upper key bound.</param>
    /// <returns>A dictionary of the in-range entries (empty when <paramref name="low"/> exceeds <paramref name="high"/>).</returns>
    public SortedDictionary<TKey, TValue> GetRange(TKey low, TKey high)
    {
        var (_, atLeast) = SplitAtLeast(low);
        var (inRange, _) = atLeast.Split(m => m.Key.HasValue && _comparer.Compare(m.Key.Value, high) > 0);
        return Wrap(inRange);
    }

    /// <summary>Copies the entries to a new array in ascending key order. O(n).</summary>
    /// <returns>A sorted array of the entries.</returns>
    public KeyValuePair<TKey, TValue>[] ToArray() => _tree.ToArray();

    /// <summary>Returns an enumerator over the entries in ascending key order.</summary>
    /// <returns>An enumerator over the entries.</returns>
    public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => _tree.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private (FingerTree<KeyValuePair<TKey, TValue>, RankedKey<TKey>, EntryMeasure<TKey, TValue>> Less,
        FingerTree<KeyValuePair<TKey, TValue>, RankedKey<TKey>, EntryMeasure<TKey, TValue>> AtLeast)
        SplitAtLeast(TKey key) =>
        _tree.Split(m => m.Key.HasValue && _comparer.Compare(m.Key.Value, key) >= 0);

    /// <summary>Reads the entry at rank <paramref name="rank"/> without reconstructing a subtree. O(log n).</summary>
    /// <param name="rank">A zero-based rank known to be in <c>0 .. Count - 1</c>.</param>
    private KeyValuePair<TKey, TValue> EntryAtRank(int rank)
    {
        _tree.TryLocate(new CountAbovePredicate<TKey>(rank), out _, out var entry);
        return entry;
    }

    /// <summary>Counts the entries strictly before the first one satisfying <paramref name="atOrPast"/>, without
    /// reconstructing a subtree or allocating a closure. O(log n).</summary>
    /// <typeparam name="TPredicate">A value-type boundary predicate.</typeparam>
    /// <param name="atOrPast">Monotone predicate marking the boundary entry.</param>
    private int CountBelow<TPredicate>(TPredicate atOrPast)
        where TPredicate : struct, IMeasurePredicate<RankedKey<TKey>>
    {
        _tree.TryLocate(atOrPast, out var before, out _);
        return before.Count;
    }

    // Collapse to the shared empty singleton only when the tree is empty AND the default comparer is in use;
    // a non-default-comparer dictionary must keep its own comparer when it becomes empty.
    private SortedDictionary<TKey, TValue> Wrap(
        FingerTree<KeyValuePair<TKey, TValue>, RankedKey<TKey>, EntryMeasure<TKey, TValue>> tree) =>
        tree.IsEmpty && ReferenceEquals(_comparer, Comparer<TKey>.Default) ? EmptyDefault : new(tree, _comparer);

    private static InvalidOperationException EmptyError() => new("The sorted dictionary is empty.");
}
