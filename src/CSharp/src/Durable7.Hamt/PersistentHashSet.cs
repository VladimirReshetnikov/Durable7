// Represents an immutable unordered set backed by a persistent hash-array mapped trie.

using System.Collections;
using System.Diagnostics;

namespace Durable7.Hamt;

/// <summary>
/// Represents an immutable unordered set backed by a persistent hash-array mapped trie.
/// </summary>
/// <typeparam name="T">The type of items stored in the set.</typeparam>
/// <remarks>
/// This type is a thin set layer over <see cref="PersistentHashMap{TKey, TValue}"/> and implements
/// <see cref="IReadOnlySet{T}"/>. Updates return new set versions, old versions remain unchanged,
/// and untouched HAMT subtrees are shared by reference across versions. Single-item operations visit
/// at most seven trie levels plus any equal-hash collision-bucket scan.
/// </remarks>
[DebuggerDisplay("Count = {Count}")]
[DebuggerTypeProxy(typeof(PersistentHashSetDebugView<>))]
public sealed partial class PersistentHashSet<T> : IReadOnlySet<T>
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

    /// <summary>
    /// Gets the root node, so a test can assert on the structure rather than only on the contents.
    /// </summary>
    internal object? RootForTesting => _map.RootForTesting;

    /// <summary>
    /// Checks that the shape is a function of the contents, so different edit histories converge on one structure.
    /// </summary>
    internal PersistentHashMapCanonicalityDiagnostics ValidateCanonicalityForDiagnostics() =>
        _map.ValidateCanonicalityForDiagnostics();

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
    /// <returns>
    /// A set containing the distinct supplied items. When items are equivalent under the comparer,
    /// the first item object is retained.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <remarks>
    /// Runs in O(n (w + c)), where w is the bounded trie depth and c is the applicable equal-hash
    /// collision scan. A mutable unpublished trie is frozen once after enumeration.
    /// </remarks>
    public static PersistentHashSet<T> CreateRange(IEnumerable<T> items, IEqualityComparer<T>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var builder = PersistentHashMap<T, Unit>.CreateBulkBuilder(comparer);
        foreach (var item in items)
            builder.SetItem(item, default);

        return Wrap(builder.ToImmutable());
    }

    /// <summary>
    /// Determines whether the set contains the specified item.
    /// </summary>
    /// <param name="item">The item to locate.</param>
    /// <returns><see langword="true"/> when the item is present; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan and allocates
    /// nothing.
    /// </remarks>
    public bool Contains(T item) => _map.ContainsKey(item);

    /// <summary>
    /// Searches for the stored item equivalent to the specified value.
    /// </summary>
    /// <param name="equalValue">The value to search for.</param>
    /// <param name="actualValue">
    /// When this method returns, contains the originally stored item object when an equivalent item
    /// is present, or <paramref name="equalValue"/> otherwise.
    /// </param>
    /// <returns><see langword="true"/> when an equivalent item is present; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// <para>
    /// Because adding an equivalent item retains the originally stored item object, this method is
    /// the O(trie-depth) way to recover that canonical object, for example when interning.
    /// </para>
    /// <para>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan and allocates
    /// nothing.
    /// </para>
    /// </remarks>
    public bool TryGetValue(T equalValue, out T actualValue) => _map.TryGetKey(equalValue, out actualValue);

    /// <summary>
    /// Adds an item to the set.
    /// </summary>
    /// <param name="item">The item to add.</param>
    /// <returns>
    /// A set containing <paramref name="item"/>. If an equivalent item is already present, this
    /// method returns the current set instance and preserves the originally stored item object.
    /// </returns>
    /// <remarks>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan; allocates only the
    /// rebuilt search path and shares every untouched subtree with the current version.
    /// </remarks>
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
    /// <remarks>Hashes the item once and walks the trie once.</remarks>
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
    /// <remarks>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan; allocates only the
    /// rebuilt search path and shares every untouched subtree with the current version.
    /// </remarks>
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
    /// <returns>
    /// An empty set with the same item comparer. If the set is already empty, this method returns the
    /// current set instance.
    /// </returns>
    public PersistentHashSet<T> Clear() => WithMap(_map.Clear());

    /// <summary>Returns the structural union with another set retaining the same comparer object.</summary>
    public PersistentHashSet<T> Union(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.Union(other._map));
    }

    /// <summary>Returns the structural intersection with another set retaining the same comparer object.</summary>
    public PersistentHashSet<T> Intersect(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.Intersect(other._map));
    }

    /// <summary>Returns the structural difference from another set retaining the same comparer object.</summary>
    public PersistentHashSet<T> Except(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.Except(other._map));
    }

    /// <summary>Returns the structural symmetric difference with another compatible set.</summary>
    public PersistentHashSet<T> SymmetricExcept(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.SymmetricExcept(other._map));
    }

    /// <summary>
    /// Returns the union of this set and the specified items.
    /// </summary>
    /// <param name="items">The items to include in the result.</param>
    /// <returns>
    /// A set containing every item from this set and <paramref name="items"/>. For items equivalent
    /// under this set's comparer, the first stored item object is retained.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <remarks>Runs in O(m) single-item updates over <paramref name="items"/>.</remarks>
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
    /// <returns>
    /// A set containing items present in both collections under this set's comparer. The stored item
    /// objects of this set are retained in the result.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <remarks>
    /// Materializes <paramref name="items"/> into a probe <see cref="HashSet{T}"/> — O(m) time and
    /// space — and then rebuilds the result from this set's matching items.
    /// </remarks>
    public PersistentHashSet<T> Intersect(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var probe = new HashSet<T>(items, Comparer);
        var builder = PersistentHashMap<T, Unit>.CreateBulkBuilder(Comparer);
        foreach (var item in this)
        {
            if (probe.Contains(item))
                builder.SetItem(item, default);
        }

        return Wrap(builder.ToImmutable());
    }

    /// <summary>
    /// Returns this set with all specified items removed.
    /// </summary>
    /// <param name="items">The items to remove from this set.</param>
    /// <returns>A set containing items from this set that are not present in <paramref name="items"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <remarks>Runs in O(m) single-item removals over <paramref name="items"/>.</remarks>
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
    /// <remarks>
    /// Materializes <paramref name="items"/> into a probe <see cref="HashSet{T}"/> — O(m) time and
    /// space — before toggling membership.
    /// </remarks>
    public PersistentHashSet<T> SymmetricExcept(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var toggles = new HashSet<T>(items, Comparer);
        var result = this;
        foreach (var item in toggles)
            result = result.Contains(item) ? result.Remove(item) : result.Add(item);

        return result;
    }

    /// <summary>Determines structurally whether this set is a subset of another compatible set.</summary>
    public bool IsSubsetOf(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Comparer, other.Comparer))
            return IsSubsetOf((IEnumerable<T>)other);
        return Count <= other.Count && ReferenceEquals(_map.Intersect(other._map), _map);
    }

    /// <summary>Determines structurally whether this set is a proper subset of another compatible set.</summary>
    public bool IsProperSubsetOf(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Comparer, other.Comparer))
            return IsProperSubsetOf((IEnumerable<T>)other);
        return Count < other.Count && IsSubsetOf(other);
    }

    /// <summary>Determines structurally whether this set is a superset of another compatible set.</summary>
    public bool IsSupersetOf(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Comparer, other.Comparer))
            return IsSupersetOf((IEnumerable<T>)other);
        return other.IsSubsetOf(this);
    }

    /// <summary>Determines structurally whether this set is a proper superset of another compatible set.</summary>
    public bool IsProperSupersetOf(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Comparer, other.Comparer))
            return IsProperSupersetOf((IEnumerable<T>)other);
        return Count > other.Count && other.IsSubsetOf(this);
    }

    /// <summary>Determines structurally whether this set overlaps another compatible set.</summary>
    public bool Overlaps(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Comparer, other.Comparer))
            return Overlaps((IEnumerable<T>)other);
        if (ReferenceEquals(_map.RootForTesting, other._map.RootForTesting))
            return Count != 0;
        return _map.Intersect(other._map).Count != 0;
    }

    /// <summary>Determines structurally whether this set equals another compatible set.</summary>
    public bool SetEquals(PersistentHashSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Comparer, other.Comparer))
            return SetEquals((IEnumerable<T>)other);
        return _map.MapEquals(other._map);
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
    /// <remarks>
    /// Materializes <paramref name="items"/> into a probe <see cref="HashSet{T}"/> — O(m) time and
    /// space — before testing membership.
    /// </remarks>
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
    /// Determines whether this set is a proper subset of the specified items.
    /// </summary>
    /// <param name="items">The items to compare against.</param>
    /// <returns>
    /// <see langword="true"/> when every item in this set is present in <paramref name="items"/> and
    /// <paramref name="items"/> contains at least one additional distinct item; otherwise,
    /// <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <remarks>
    /// Materializes <paramref name="items"/> into a probe <see cref="HashSet{T}"/> — O(m) time and
    /// space — before testing membership.
    /// </remarks>
    public bool IsProperSubsetOf(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var probe = new HashSet<T>(items, Comparer);
        if (Count >= probe.Count)
            return false;

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
    /// <remarks>Streams <paramref name="items"/> and exits early on the first missing item.</remarks>
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
    /// Determines whether this set is a proper superset of the specified items.
    /// </summary>
    /// <param name="items">The items to compare against.</param>
    /// <returns>
    /// <see langword="true"/> when every distinct item in <paramref name="items"/> is present in this
    /// set and this set contains at least one additional item; otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <remarks>
    /// Materializes <paramref name="items"/> into a probe <see cref="HashSet{T}"/> — O(m) time and
    /// space — to count its distinct items under this set's comparer.
    /// </remarks>
    public bool IsProperSupersetOf(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var probe = new HashSet<T>(items, Comparer);
        if (probe.Count >= Count)
            return false;

        foreach (var item in probe)
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
    /// <remarks>Streams <paramref name="items"/> and exits early on the first shared item.</remarks>
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
    /// <remarks>
    /// Materializes <paramref name="items"/> into a probe <see cref="HashSet{T}"/> — O(m) time and
    /// space — before comparing memberships.
    /// </remarks>
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
    /// <remarks>
    /// Enumeration is O(n) with O(1) amortized cost per item. The enumerator keeps its whole
    /// traversal state inline, so obtaining and draining it allocates nothing.
    /// </remarks>
    public Enumerator GetEnumerator() => new(_map.GetEnumerator());

    IEnumerator<T> IEnumerable<T>.GetEnumerator() => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private static PersistentHashSet<T> Wrap(PersistentHashMap<T, Unit> map)
    {
        if (ReferenceEquals(map, PersistentHashMap<T, Unit>.Empty))
            return Empty;

        return new PersistentHashSet<T>(map);
    }

    private PersistentHashSet<T> WithMap(PersistentHashMap<T, Unit> map) =>
        ReferenceEquals(map, _map) ? this : Wrap(map);

    /// <summary>
    /// Enumerates the items in a <see cref="PersistentHashSet{T}"/>.
    /// </summary>
    /// <remarks>
    /// The enumerator keeps its entire traversal state inline, so obtaining and draining one
    /// allocates nothing, and a copied enumerator advances independently of the original.
    /// </remarks>
    public struct Enumerator : IEnumerator<T>
    {
        private PersistentHashMap<T, Unit>.Enumerator _inner;

        /// <summary>Creates a new enumerator.</summary>
        internal Enumerator(PersistentHashMap<T, Unit>.Enumerator inner)
        {
            _inner = inner;
        }

        /// <summary>
        /// Gets the item at the current enumerator position.
        /// </summary>
        public readonly T Current => _inner.Current.Key;

        readonly object? IEnumerator.Current => Current;

        /// <summary>
        /// Advances the enumerator to the next item.
        /// </summary>
        /// <returns>
        /// <see langword="true"/> when the enumerator advanced to an item; otherwise,
        /// <see langword="false"/> after the final item.
        /// </returns>
        public bool MoveNext() => _inner.MoveNext();

        /// <summary>
        /// Releases resources held by the enumerator.
        /// </summary>
        public readonly void Dispose()
        {
        }

        readonly void IEnumerator.Reset() =>
            throw new NotSupportedException("Resetting this enumerator is not supported; create a new enumerator instead.");
    }

    /// <summary>Gets the struct unit.</summary>
    internal readonly struct Unit : IEquatable<Unit>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(Unit other) => true;

        /// <summary>Determines whether both values hold the same elements.</summary>
        public override bool Equals(object? obj) => obj is Unit;

        /// <summary>Returns a hash consistent with <see cref="Equals(Unit)"/>.</summary>
        public override int GetHashCode() => 0;
    }
}

/// <summary>The debugger's view of a set: its elements, rather than its trie nodes.</summary>
internal sealed class PersistentHashSetDebugView<T>(PersistentHashSet<T> set)
{
    /// <summary>Gets the elements.</summary>
    [DebuggerBrowsable(DebuggerBrowsableState.RootHidden)]
    public T[] Items => [.. set];
}
