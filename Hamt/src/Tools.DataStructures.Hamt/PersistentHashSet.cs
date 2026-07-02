using System.Collections;

namespace Tools.DataStructures.Hamt;

/// <summary>
/// Represents an immutable unordered set backed by a persistent hash-array mapped trie.
/// </summary>
/// <typeparam name="T">The type of items stored in the set.</typeparam>
/// <remarks>
/// This type is a thin set layer over <see cref="PersistentHashMap{TKey, TValue}"/>. Updates return
/// new set versions, old versions remain unchanged, and untouched HAMT subtrees are shared by
/// reference across versions.
/// </remarks>
public sealed class PersistentHashSet<T> : IReadOnlyCollection<T>
{
    /// <summary>
    /// Gets the shared empty set that uses <see cref="EqualityComparer{T}.Default"/>.
    /// </summary>
    public static PersistentHashSet<T> Empty { get; } = new(PersistentHashMap<T, Unit>.Empty);

    private readonly PersistentHashMap<T, Unit> _map;

    private PersistentHashSet(PersistentHashMap<T, Unit> map)
    {
        _map = map;
    }

    /// <summary>
    /// Gets the number of items in the set.
    /// </summary>
    public int Count => _map.Count;

    /// <summary>
    /// Gets whether the set contains no items.
    /// </summary>
    public bool IsEmpty => _map.IsEmpty;

    /// <summary>
    /// Gets the equality comparer that defines item hashing and equality.
    /// </summary>
    public IEqualityComparer<T> Comparer => _map.Comparer;

    internal object? RootForTesting => _map.RootForTesting;

    /// <summary>
    /// Creates an empty set with the specified item comparer.
    /// </summary>
    /// <param name="comparer">
    /// The comparer that defines item hashing and equality, or <see langword="null"/> to use
    /// <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>An empty set using <paramref name="comparer"/>.</returns>
    public static PersistentHashSet<T> Create(IEqualityComparer<T>? comparer = null) =>
        Wrap(PersistentHashMap<T, Unit>.Create(comparer));

    /// <summary>
    /// Creates a set from an enumerable sequence of items.
    /// </summary>
    /// <param name="items">The items to add in enumeration order.</param>
    /// <param name="comparer">
    /// The comparer that defines item hashing and equality, or <see langword="null"/> to use
    /// <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>A set containing the distinct supplied items.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static PersistentHashSet<T> CreateRange(IEnumerable<T> items, IEqualityComparer<T>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var set = Create(comparer);
        foreach (var item in items)
            set = set.Add(item);

        return set;
    }

    /// <summary>
    /// Determines whether the set contains the specified item.
    /// </summary>
    /// <param name="item">The item to locate.</param>
    /// <returns><see langword="true"/> when the item is present; otherwise, <see langword="false"/>.</returns>
    public bool Contains(T item) => _map.ContainsKey(item);

    /// <summary>
    /// Adds an item to the set.
    /// </summary>
    /// <param name="item">The item to add.</param>
    /// <returns>
    /// A set containing <paramref name="item"/>. If an equivalent item is already present, this
    /// method returns the current set instance and preserves the originally stored item object.
    /// </returns>
    public PersistentHashSet<T> Add(T item) => WithMap(_map.SetItem(item, default));

    /// <summary>
    /// Tries to add an item to the set without changing an existing equivalent item.
    /// </summary>
    /// <param name="item">The item to add.</param>
    /// <param name="result">
    /// When this method returns, contains the updated set on success or the current set when an
    /// equivalent item already exists.
    /// </param>
    /// <returns><see langword="true"/> when the item was added; otherwise, <see langword="false"/>.</returns>
    public bool TryAdd(T item, out PersistentHashSet<T> result)
    {
        if (_map.TryAdd(item, default, out var map))
        {
            result = WithMap(map);
            return true;
        }

        result = this;
        return false;
    }

    /// <summary>
    /// Removes an item from the set when present.
    /// </summary>
    /// <param name="item">The item to remove.</param>
    /// <returns>
    /// A set without <paramref name="item"/>. If the item is absent, this method returns the current
    /// set instance.
    /// </returns>
    public PersistentHashSet<T> Remove(T item) => WithMap(_map.Remove(item));

    /// <summary>
    /// Tries to remove an item from the set.
    /// </summary>
    /// <param name="item">The item to remove.</param>
    /// <param name="result">
    /// When this method returns, contains the updated set on success or the current set when the item
    /// was absent.
    /// </param>
    /// <returns><see langword="true"/> when the item was present; otherwise, <see langword="false"/>.</returns>
    public bool TryRemove(T item, out PersistentHashSet<T> result)
    {
        if (_map.TryRemove(item, out var map, out _))
        {
            result = WithMap(map);
            return true;
        }

        result = this;
        return false;
    }

    /// <summary>
    /// Returns an empty set that preserves this set's comparer.
    /// </summary>
    /// <returns>An empty set with the same item comparer.</returns>
    public PersistentHashSet<T> Clear() => _map.IsEmpty ? this : Wrap(_map.Clear());

    /// <summary>
    /// Returns the union of this set and the specified items.
    /// </summary>
    /// <param name="items">The items to include in the result.</param>
    /// <returns>A set containing every item from this set and <paramref name="items"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public PersistentHashSet<T> Union(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var result = this;
        foreach (var item in items)
            result = result.Add(item);

        return result;
    }

    /// <summary>
    /// Returns the intersection of this set and the specified items.
    /// </summary>
    /// <param name="items">The items to intersect with this set.</param>
    /// <returns>A set containing items present in both collections under this set's comparer.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public PersistentHashSet<T> Intersect(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var probe = new HashSet<T>(items, Comparer);
        var result = Create(Comparer);
        foreach (var item in this)
        {
            if (probe.Contains(item))
                result = result.Add(item);
        }

        return result;
    }

    /// <summary>
    /// Returns this set with all specified items removed.
    /// </summary>
    /// <param name="items">The items to remove from this set.</param>
    /// <returns>A set containing items from this set that are not present in <paramref name="items"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public PersistentHashSet<T> Except(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var result = this;
        foreach (var item in items)
            result = result.Remove(item);

        return result;
    }

    /// <summary>
    /// Returns the symmetric difference of this set and the specified items.
    /// </summary>
    /// <param name="items">The items to combine with this set.</param>
    /// <returns>
    /// A set containing items present in exactly one side. Duplicate values in <paramref name="items"/>
    /// are treated as one item under this set's comparer.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public PersistentHashSet<T> SymmetricExcept(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var toggles = new HashSet<T>(items, Comparer);
        var result = this;
        foreach (var item in toggles)
            result = result.Contains(item) ? result.Remove(item) : result.Add(item);

        return result;
    }

    /// <summary>
    /// Determines whether this set is a subset of the specified items.
    /// </summary>
    /// <param name="items">The items to compare against.</param>
    /// <returns>
    /// <see langword="true"/> when every item in this set is present in <paramref name="items"/>;
    /// otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public bool IsSubsetOf(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var probe = new HashSet<T>(items, Comparer);
        foreach (var item in this)
        {
            if (!probe.Contains(item))
                return false;
        }

        return true;
    }

    /// <summary>
    /// Determines whether this set is a superset of the specified items.
    /// </summary>
    /// <param name="items">The items to compare against.</param>
    /// <returns>
    /// <see langword="true"/> when every distinct item in <paramref name="items"/> is present in this
    /// set; otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public bool IsSupersetOf(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        foreach (var item in items)
        {
            if (!Contains(item))
                return false;
        }

        return true;
    }

    /// <summary>
    /// Determines whether this set and the specified items share at least one item.
    /// </summary>
    /// <param name="items">The items to compare against.</param>
    /// <returns>
    /// <see langword="true"/> when any item in <paramref name="items"/> is present in this set;
    /// otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public bool Overlaps(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        foreach (var item in items)
        {
            if (Contains(item))
                return true;
        }

        return false;
    }

    /// <summary>
    /// Determines whether this set and the specified items contain the same distinct items.
    /// </summary>
    /// <param name="items">The items to compare against.</param>
    /// <returns>
    /// <see langword="true"/> when both sides contain the same distinct items under this set's
    /// comparer; otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public bool SetEquals(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var probe = new HashSet<T>(items, Comparer);
        if (probe.Count != Count)
            return false;

        foreach (var item in this)
        {
            if (!probe.Contains(item))
                return false;
        }

        return true;
    }

    /// <summary>
    /// Returns an enumerator over the set's items in trie order.
    /// </summary>
    /// <returns>An enumerator over the set.</returns>
    public IEnumerator<T> GetEnumerator() => _map.Keys.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private static PersistentHashSet<T> Wrap(PersistentHashMap<T, Unit> map)
    {
        if (ReferenceEquals(map, PersistentHashMap<T, Unit>.Empty))
            return Empty;

        return new PersistentHashSet<T>(map);
    }

    private PersistentHashSet<T> WithMap(PersistentHashMap<T, Unit> map) =>
        ReferenceEquals(map, _map) ? this : Wrap(map);

    private readonly struct Unit;
}
