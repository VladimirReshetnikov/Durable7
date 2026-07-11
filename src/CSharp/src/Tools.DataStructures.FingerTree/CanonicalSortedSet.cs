using System.Collections;
using System.Diagnostics;
using System.Numerics;
using System.Threading;

namespace Tools.DataStructures.FingerTree;

/// <summary>Represents an immutable uniquely shaped sorted set backed by a keyed zip-zip tree.</summary>
/// <typeparam name="T">The item type.</typeparam>
/// <remarks>
/// Rank is derived from item content and the retained policy seed, so equal contents under the same
/// policy produce the same shape regardless of insertion or deletion history. Expected operation
/// cost is O(log n); an invalid or adversarially colliding rank hash can degrade the tree to O(n).
/// </remarks>
[DebuggerDisplay("Count = {Count}, Height = {Height}")]
public sealed class CanonicalSortedSet<T> : IReadOnlySet<T>
{
    private readonly Node? _root;

    private CanonicalSortedSet(Node? root, ZipTreeRankPolicy<T> policy)
    {
        _root = root;
        Policy = policy;
    }

    /// <summary>Gets the shared empty set using <see cref="ZipTreeRankPolicy{T}.Default"/>.</summary>
    public static CanonicalSortedSet<T> Empty { get; } = new(root: null, ZipTreeRankPolicy<T>.Default);

    /// <summary>Gets the retained rank and comparison policy.</summary>
    public ZipTreeRankPolicy<T> Policy { get; }

    /// <summary>Gets the number of items.</summary>
    public int Count => _root?.Count ?? 0;

    /// <summary>Gets whether the set is empty.</summary>
    public bool IsEmpty => _root is null;

    /// <summary>Gets the tree height for diagnostics and benchmark validation.</summary>
    public int Height => _root?.Height ?? 0;

    /// <summary>
    /// Gets a memoized non-cryptographic digest of the canonical tree, or zero for an empty set.
    /// </summary>
    /// <remarks>Digest inequality proves set inequality; equality still requires semantic comparison.</remarks>
    public ulong ContentHash => _root?.GetDigest() ?? 0;

    internal object? RootIdentity => _root;

    internal (T Item, int LeftCount, int RightCount)[] ShapeForTesting()
    {
        if (_root is null)
            return [];
        var result = new List<(T, int, int)>(Count);
        var stack = new Stack<Node>();
        stack.Push(_root);
        while (stack.TryPop(out var node))
        {
            result.Add((node.Item, node.Left?.Count ?? 0, node.Right?.Count ?? 0));
            if (node.Right is not null)
                stack.Push(node.Right);
            if (node.Left is not null)
                stack.Push(node.Left);
        }
        return [.. result];
    }

    /// <summary>Creates an empty set with a retained rank policy.</summary>
    /// <param name="policy">The rank/comparison policy.</param>
    /// <returns>An empty set using <paramref name="policy"/>.</returns>
    public static CanonicalSortedSet<T> Create(ZipTreeRankPolicy<T> policy)
    {
        ArgumentNullException.ThrowIfNull(policy);
        return ReferenceEquals(policy, ZipTreeRankPolicy<T>.Default) ? Empty : new CanonicalSortedSet<T>(null, policy);
    }

    /// <summary>Creates a set from a sequence.</summary>
    /// <param name="items">The items to add.</param>
    /// <param name="policy">The retained policy, or <see langword="null"/> for the shared default.</param>
    /// <returns>A set containing one representative per comparison-equivalence class.</returns>
    public static CanonicalSortedSet<T> CreateRange(
        IEnumerable<T> items,
        ZipTreeRankPolicy<T>? policy = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = Create(policy ?? ZipTreeRankPolicy<T>.Default);
        foreach (var item in items)
            result = result.Add(item);
        return result;
    }

    /// <summary>Determines whether an equivalent item is present.</summary>
    /// <param name="item">The probe item.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool Contains(T item) => TryGetValue(item, out _);

    /// <summary>Recovers the stored representative for an equivalent item.</summary>
    /// <param name="equalItem">The probe item.</param>
    /// <param name="actualItem">The stored representative on success; otherwise, <paramref name="equalItem"/>.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool TryGetValue(T equalItem, out T actualItem)
    {
        var node = _root;
        while (node is not null)
        {
            var comparison = Policy.Comparer.Compare(equalItem, node.Item);
            if (comparison == 0)
            {
                actualItem = node.Item;
                return true;
            }
            node = comparison < 0 ? node.Left : node.Right;
        }
        actualItem = equalItem;
        return false;
    }

    /// <summary>Adds an item, preserving identity when an equivalent item is present.</summary>
    /// <param name="item">The item.</param>
    /// <returns>The updated set.</returns>
    public CanonicalSortedSet<T> Add(T item)
    {
        if (Contains(item))
            return this;
        var root = Insert(_root, new Node(item, Policy.GetRank(item), null, null), Policy.Comparer);
        return new CanonicalSortedSet<T>(root, Policy);
    }

    /// <summary>Removes an equivalent item, preserving identity when absent.</summary>
    /// <param name="item">The item to remove.</param>
    /// <returns>The updated set.</returns>
    public CanonicalSortedSet<T> Remove(T item)
    {
        var root = Remove(_root, item, Policy.Comparer, out var removed);
        if (!removed)
            return this;
        return root is null ? Create(Policy) : new CanonicalSortedSet<T>(root, Policy);
    }

    /// <summary>Returns an empty set retaining the current policy.</summary>
    /// <returns>An empty set, or this set when already empty.</returns>
    public CanonicalSortedSet<T> Clear() => IsEmpty ? this : Create(Policy);

    /// <summary>Returns the union with another set using the same policy object.</summary>
    /// <param name="other">The other set.</param>
    /// <returns>The union.</returns>
    public CanonicalSortedSet<T> Union(CanonicalSortedSet<T> other)
    {
        EnsureCompatible(other);
        var result = this;
        foreach (var item in other)
            result = result.Add(item);
        return result;
    }

    /// <summary>Returns the intersection with another set using the same policy object.</summary>
    /// <param name="other">The other set.</param>
    /// <returns>The intersection.</returns>
    public CanonicalSortedSet<T> Intersect(CanonicalSortedSet<T> other)
    {
        EnsureCompatible(other);
        if (ReferenceEquals(_root, other._root))
            return this;
        var result = Create(Policy);
        foreach (var item in this)
        {
            if (other.Contains(item))
                result = result.Add(item);
        }
        return result;
    }

    /// <summary>Returns the difference from another set using the same policy object.</summary>
    /// <param name="other">The other set.</param>
    /// <returns>The difference.</returns>
    public CanonicalSortedSet<T> Except(CanonicalSortedSet<T> other)
    {
        EnsureCompatible(other);
        if (ReferenceEquals(_root, other._root))
            return Create(Policy);
        var result = this;
        foreach (var item in other)
            result = result.Remove(item);
        return result;
    }

    /// <summary>Determines semantic equality using canonical lockstep shape.</summary>
    /// <param name="other">The set to compare.</param>
    /// <returns><see langword="true"/> when policy and contents are equal.</returns>
    public bool SetEquals(CanonicalSortedSet<T>? other)
    {
        if (ReferenceEquals(this, other))
            return true;
        if (other is null || !ReferenceEquals(Policy, other.Policy) || Count != other.Count)
            return false;
        if (ContentHash != other.ContentHash)
            return false;
        return NodesEqual(_root, other._root, Policy.Comparer);
    }

    /// <inheritdoc/>
    public bool IsSubsetOf(IEnumerable<T> other) => new System.Collections.Generic.SortedSet<T>(other, Policy.Comparer).IsSupersetOf(this);

    /// <inheritdoc/>
    public bool IsProperSubsetOf(IEnumerable<T> other) => new System.Collections.Generic.SortedSet<T>(other, Policy.Comparer).IsProperSupersetOf(this);

    /// <inheritdoc/>
    public bool IsSupersetOf(IEnumerable<T> other) => other.All(Contains);

    /// <inheritdoc/>
    public bool IsProperSupersetOf(IEnumerable<T> other)
    {
        var probe = new System.Collections.Generic.SortedSet<T>(other, Policy.Comparer);
        return Count > probe.Count && probe.All(Contains);
    }

    /// <inheritdoc/>
    public bool Overlaps(IEnumerable<T> other) => other.Any(Contains);

    /// <inheritdoc/>
    public bool SetEquals(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (other is CanonicalSortedSet<T> set)
            return SetEquals(set);
        var probe = new System.Collections.Generic.SortedSet<T>(other, Policy.Comparer);
        return Count == probe.Count && probe.All(Contains);
    }

    /// <summary>Returns items in comparer order.</summary>
    /// <returns>An enumerator.</returns>
    public IEnumerator<T> GetEnumerator() => Enumerate().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private IEnumerable<T> Enumerate()
    {
        var stack = new Stack<Node>();
        var node = _root;
        while (node is not null || stack.Count != 0)
        {
            while (node is not null)
            {
                stack.Push(node);
                node = node.Left;
            }
            node = stack.Pop();
            yield return node.Item;
            node = node.Right;
        }
    }

    private void EnsureCompatible(CanonicalSortedSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Policy, other.Policy))
            throw new ArgumentException("Canonical set algebra requires the same rank-policy object.", nameof(other));
    }

    private static Node Insert(Node? root, Node item, IComparer<T> comparer)
    {
        if (root is null)
            return item;
        if (Higher(item, root, comparer))
        {
            var (left, right) = Split(root, item.Item, comparer);
            return new Node(item.Item, item.Rank, left, right);
        }
        var comparison = comparer.Compare(item.Item, root.Item);
        return comparison < 0
            ? new Node(root.Item, root.Rank, Insert(root.Left, item, comparer), root.Right)
            : new Node(root.Item, root.Rank, root.Left, Insert(root.Right, item, comparer));
    }

    private static (Node? Left, Node? Right) Split(Node? root, T item, IComparer<T> comparer)
    {
        if (root is null)
            return (null, null);
        if (comparer.Compare(item, root.Item) < 0)
        {
            var (left, right) = Split(root.Left, item, comparer);
            return (left, new Node(root.Item, root.Rank, right, root.Right));
        }
        else
        {
            var (left, right) = Split(root.Right, item, comparer);
            return (new Node(root.Item, root.Rank, root.Left, left), right);
        }
    }

    private static Node? Remove(Node? root, T item, IComparer<T> comparer, out bool removed)
    {
        if (root is null)
        {
            removed = false;
            return null;
        }
        var comparison = comparer.Compare(item, root.Item);
        if (comparison == 0)
        {
            removed = true;
            return Merge(root.Left, root.Right, comparer);
        }
        if (comparison < 0)
        {
            var left = Remove(root.Left, item, comparer, out removed);
            return removed ? new Node(root.Item, root.Rank, left, root.Right) : root;
        }
        var right = Remove(root.Right, item, comparer, out removed);
        return removed ? new Node(root.Item, root.Rank, root.Left, right) : root;
    }

    private static Node? Merge(Node? left, Node? right, IComparer<T> comparer)
    {
        if (left is null)
            return right;
        if (right is null)
            return left;
        if (Higher(left, right, comparer))
            return new Node(left.Item, left.Rank, left.Left, Merge(left.Right, right, comparer));
        return new Node(right.Item, right.Rank, Merge(left, right.Left, comparer), right.Right);
    }

    private static bool Higher(Node left, Node right, IComparer<T> comparer)
    {
        var rank = left.Rank.Geometric.CompareTo(right.Rank.Geometric);
        if (rank != 0)
            return rank > 0;
        rank = left.Rank.Secondary.CompareTo(right.Rank.Secondary);
        if (rank != 0)
            return rank > 0;
        return comparer.Compare(left.Item, right.Item) < 0;
    }

    private static bool NodesEqual(Node? left, Node? right, IComparer<T> comparer)
    {
        if (ReferenceEquals(left, right))
            return true;
        if (left is null || right is null || comparer.Compare(left.Item, right.Item) != 0)
            return false;
        return NodesEqual(left.Left, right.Left, comparer) && NodesEqual(left.Right, right.Right, comparer);
    }

    private sealed class Node(
        T item,
        ZipTreeRankPolicy<T>.Rank rank,
        Node? left,
        Node? right)
    {
        private object? _digest;

        internal T Item { get; } = item;

        internal ZipTreeRankPolicy<T>.Rank Rank { get; } = rank;

        internal Node? Left { get; } = left;

        internal Node? Right { get; } = right;

        internal int Count { get; } = checked(1 + (left?.Count ?? 0) + (right?.Count ?? 0));

        internal int Height { get; } = checked(1 + Math.Max(left?.Height ?? 0, right?.Height ?? 0));

        internal ulong GetDigest()
        {
            if (Volatile.Read(ref _digest) is ulong cached)
                return cached;
            var leftHash = Left?.GetDigest() ?? 0x243f6a8885a308d3UL;
            var rightHash = Right?.GetDigest() ?? 0x13198a2e03707344UL;
            var digest = ZipTreeRankPolicy<T>.Mix(Rank.Hash ^ BitOperations.RotateLeft(leftHash, 17) ^ BitOperations.RotateLeft(rightHash, 43));
            Interlocked.CompareExchange(ref _digest, digest, null);
            return (ulong)_digest!;
        }
    }
}
