using System.Collections;

namespace Durable7.Hamt;

/// <summary>Represents an immutable set of signed 64-bit integers using a big-endian Patricia trie.</summary>
public sealed partial class PersistentLongSet : IReadOnlySet<long>
{
    private readonly PersistentLongMap<Unit> _map;

    private PersistentLongSet(PersistentLongMap<Unit> map) => _map = map;

    /// <summary>Gets the shared empty set.</summary>
    public static PersistentLongSet Empty { get; } = new(PersistentLongMap<Unit>.Empty);

    /// <summary>Gets the number of items.</summary>
    public int Count => _map.Count;

    /// <summary>Gets whether the set is empty.</summary>
    public bool IsEmpty => Count == 0;

    internal object? RootIdentity => _map.RootIdentity;

    /// <summary>Creates a set from a sequence.</summary>
    /// <param name="items">The items to add.</param>
    /// <returns>A set containing the distinct items.</returns>
    public static PersistentLongSet CreateRange(IEnumerable<long> items)
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
    public bool Contains(long item) => _map.ContainsKey(item);

    /// <summary>Adds an item.</summary>
    /// <param name="item">The item.</param>
    /// <returns>The updated set.</returns>
    public PersistentLongSet Add(long item) => WithMap(_map.SetItem(item, default));

    /// <summary>Removes an item.</summary>
    /// <param name="item">The item.</param>
    /// <returns>The updated set.</returns>
    public PersistentLongSet Remove(long item) => WithMap(_map.Remove(item));

    /// <summary>Returns an empty set.</summary>
    /// <returns>The shared empty set, or this set when empty.</returns>
    public PersistentLongSet Clear() => IsEmpty ? this : Empty;

    /// <summary>Returns the union.</summary>
    /// <param name="other">The other set.</param>
    /// <returns>The union.</returns>
    public PersistentLongSet Union(PersistentLongSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.Union(other._map));
    }

    /// <summary>Returns the intersection.</summary>
    /// <param name="other">The other set.</param>
    /// <returns>The intersection.</returns>
    public PersistentLongSet Intersect(PersistentLongSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.Intersect(other._map));
    }

    /// <summary>Returns the difference.</summary>
    /// <param name="other">The other set.</param>
    /// <returns>The difference.</returns>
    public PersistentLongSet Except(PersistentLongSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return WithMap(_map.Except(other._map));
    }

    /// <inheritdoc/>
    public bool IsSubsetOf(IEnumerable<long> other) => new HashSet<long>(other).IsSupersetOf(this);

    /// <inheritdoc/>
    public bool IsProperSubsetOf(IEnumerable<long> other) => new HashSet<long>(other).IsProperSupersetOf(this);

    /// <inheritdoc/>
    public bool IsSupersetOf(IEnumerable<long> other) => other.All(Contains);

    /// <inheritdoc/>
    public bool IsProperSupersetOf(IEnumerable<long> other)
    {
        var probe = new HashSet<long>(other);
        return Count > probe.Count && probe.All(Contains);
    }

    /// <inheritdoc/>
    public bool Overlaps(IEnumerable<long> other) => other.Any(Contains);

    /// <inheritdoc/>
    public bool SetEquals(IEnumerable<long> other)
    {
        var probe = new HashSet<long>(other);
        return Count == probe.Count && probe.All(Contains);
    }

    /// <summary>Returns items in ascending signed order.</summary>
    /// <returns>An enumerator.</returns>
    public IEnumerator<long> GetEnumerator() => _map.Keys.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private PersistentLongSet WithMap(PersistentLongMap<Unit> map) =>
        ReferenceEquals(map, _map) ? this : ReferenceEquals(map, PersistentLongMap<Unit>.Empty) ? Empty : new(map);

    private readonly struct Unit : IEquatable<Unit>
    {
        public bool Equals(Unit other) => true;

        public override bool Equals(object? obj) => obj is Unit;

        public override int GetHashCode() => 0;
    }
}
