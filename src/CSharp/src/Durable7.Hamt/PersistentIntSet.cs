// Represents an immutable set of signed 32-bit integers using a big-endian Patricia trie.

using System.Collections;

namespace Durable7.Hamt;

/// <summary>Represents an immutable set of signed 32-bit integers using a big-endian Patricia trie.</summary>
public sealed partial class PersistentIntSet : IReadOnlySet<int>
{
    private readonly PersistentIntMap<Unit> _map;

    private PersistentIntSet(PersistentIntMap<Unit> map) => _map = map;

    /// <summary>Gets the shared empty set.</summary>
    public static PersistentIntSet Empty { get; } = new(PersistentIntMap<Unit>.Empty);

    /// <summary>Gets the number of items.</summary>
    public int Count => _map.Count;

    /// <summary>Gets whether the set is empty.</summary>
    public bool IsEmpty => Count == 0;

    internal object? RootIdentity => _map.RootIdentity;

    /// <summary>Creates a set from a sequence.</summary>
    /// <param name="items">The items to add.</param>
    /// <returns>A set containing the distinct items.</returns>
    public static PersistentIntSet CreateRange(IEnumerable<int> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = Empty;
        foreach (var item in items)
            result = result.Add(item);
        return result;
    }

    /// <summary>Determines whether an item is present.</summary>
    /// <param name="item">The item.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool Contains(int item) => _map.ContainsKey(item);

    /// <summary>Adds an item, preserving identity when present.</summary>
    /// <param name="item">The item.</param>
    /// <returns>The updated set.</returns>
    public PersistentIntSet Add(int item) => WithMap(_map.SetItem(item, default));

    /// <summary>Removes an item, preserving identity when absent.</summary>
    /// <param name="item">The item.</param>
    /// <returns>The updated set.</returns>
    public PersistentIntSet Remove(int item) => WithMap(_map.Remove(item));

    /// <summary>Returns the shared empty set, or this set when empty.</summary>
    /// <returns>An empty set.</returns>
    public PersistentIntSet Clear() => IsEmpty ? this : Empty;

    /// <summary>Returns the union with another set.</summary>
    /// <param name="other">The other set.</param>
    /// <returns>The union.</returns>
    public PersistentIntSet Union(PersistentIntSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.Union(other._map));
    }

    /// <summary>Returns the intersection with another set.</summary>
    /// <param name="other">The other set.</param>
    /// <returns>The intersection.</returns>
    public PersistentIntSet Intersect(PersistentIntSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.Intersect(other._map));
    }

    /// <summary>Returns the difference from another set.</summary>
    /// <param name="other">The other set.</param>
    /// <returns>The difference.</returns>
    public PersistentIntSet Except(PersistentIntSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.Except(other._map));
    }

    /// <inheritdoc/>
    public bool IsSubsetOf(IEnumerable<int> other) => new HashSet<int>(other).IsSupersetOf(this);

    /// <inheritdoc/>
    public bool IsProperSubsetOf(IEnumerable<int> other) => new HashSet<int>(other).IsProperSupersetOf(this);

    /// <inheritdoc/>
    public bool IsSupersetOf(IEnumerable<int> other) => other.All(Contains);

    /// <inheritdoc/>
    public bool IsProperSupersetOf(IEnumerable<int> other)
    {
        var probe = new HashSet<int>(other);
        return Count > probe.Count && probe.All(Contains);
    }

    /// <inheritdoc/>
    public bool Overlaps(IEnumerable<int> other) => other.Any(Contains);

    /// <inheritdoc/>
    public bool SetEquals(IEnumerable<int> other)
    {
        var probe = new HashSet<int>(other);
        return Count == probe.Count && probe.All(Contains);
    }

    /// <summary>Returns items in ascending signed order.</summary>
    /// <returns>An enumerator.</returns>
    public IEnumerator<int> GetEnumerator() => _map.Keys.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private PersistentIntSet WithMap(PersistentIntMap<Unit> map) =>
        ReferenceEquals(map, _map) ? this : ReferenceEquals(map, PersistentIntMap<Unit>.Empty) ? Empty : new(map);

    private readonly struct Unit : IEquatable<Unit>
    {
        public bool Equals(Unit other) => true;

        public override bool Equals(object? obj) => obj is Unit;

        public override int GetHashCode() => 0;
    }
}
