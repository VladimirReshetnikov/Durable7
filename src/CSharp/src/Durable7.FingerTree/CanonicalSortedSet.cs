using System.Collections;
using System.Diagnostics;
using System.Numerics;
using System.Threading;

namespace Durable7.FingerTree;

/// <summary>Represents an immutable policy-canonical sorted set backed by a zip-zip-inspired tree.</summary>
/// <typeparam name="T">The item type.</typeparam>
/// <remarks>
/// Rank is derived from each item's equivalence-class hash and the retained policy key, so equal
/// stored representatives under one coherent policy produce the same topology regardless of update
/// history. Expected operation cost is O(log n); an invalid or colliding rank hash can degrade the
/// tree to O(n). All traversals and updates use explicit stacks and remain stack-safe in that case.
/// </remarks>
[DebuggerDisplay("Count = {Count}, Height = {Height}")]
public sealed partial class CanonicalSortedSet<T> : IReadOnlySet<T>
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
    /// <remarks>
    /// Within one coherent policy, digest inequality proves set inequality; equality still requires
    /// semantic comparison. Digests from different policy objects are not generally comparable.
    /// </remarks>
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

    /// <summary>Validates ordering, heap rank, rank reproducibility, and cached metadata.</summary>
    /// <returns>Statistics for the validated representation.</returns>
    public CanonicalSortedSetStatistics ValidateStructure()
    {
        if (_root is null)
            return new CanonicalSortedSetStatistics(0, 0, 0, 0);

        var stack = new Stack<ValidationFrame>();
        stack.Push(new ValidationFrame(_root, default, default, 1));
        var priorities = new HashSet<(int Geometric, ulong Secondary)>();
        var count = 0;
        var maximumDepth = 0;
        var maximumGeometricRank = 0;
        var priorityCollisions = 0;
        while (stack.TryPop(out var frame))
        {
            var node = frame.Node;
            if (frame.Lower.HasValue && Policy.Comparer.Compare(node.Item, frame.Lower.Value) <= 0)
                throw new InvalidOperationException("A canonical sorted-set node crosses its lower key bound.");
            if (frame.Upper.HasValue && Policy.Comparer.Compare(node.Item, frame.Upper.Value) >= 0)
                throw new InvalidOperationException("A canonical sorted-set node crosses its upper key bound.");
            if (node.Rank != Policy.GetRank(node.Item))
                throw new InvalidOperationException("A canonical sorted-set node has a non-reproducible rank.");
            if (node.Left is { } left && !Higher(node, left, Policy.Comparer))
                throw new InvalidOperationException("A canonical sorted-set left child outranks its parent.");
            if (node.Right is { } right && !Higher(node, right, Policy.Comparer))
                throw new InvalidOperationException("A canonical sorted-set right child outranks its parent.");

            var expectedCount = checked(1 + (node.Left?.Count ?? 0) + (node.Right?.Count ?? 0));
            var expectedHeight = checked(1 + Math.Max(node.Left?.Height ?? 0, node.Right?.Height ?? 0));
            if (node.Count != expectedCount || node.Height != expectedHeight)
                throw new InvalidOperationException("A canonical sorted-set node has invalid cached metadata.");

            count++;
            maximumDepth = Math.Max(maximumDepth, frame.Depth);
            maximumGeometricRank = Math.Max(maximumGeometricRank, node.Rank.Geometric);
            if (!priorities.Add((node.Rank.Geometric, node.Rank.Secondary)))
                priorityCollisions++;
            if (node.Right is not null)
            {
                stack.Push(new ValidationFrame(
                    node.Right,
                    new KeyBound(node.Item),
                    frame.Upper,
                    checked(frame.Depth + 1)));
            }
            if (node.Left is not null)
            {
                stack.Push(new ValidationFrame(
                    node.Left,
                    frame.Lower,
                    new KeyBound(node.Item),
                    checked(frame.Depth + 1)));
            }
        }
        if (count != Count || maximumDepth != Height)
            throw new InvalidOperationException("Canonical sorted-set root metadata disagrees with the validated tree.");
        return new CanonicalSortedSetStatistics(count, maximumDepth, maximumGeometricRank, priorityCollisions);
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
        var actualPolicy = policy ?? ZipTreeRankPolicy<T>.Default;
        var pending = items.Select((item, sequence) => new PendingItem(item, sequence)).ToArray();
        if (pending.Length == 0)
            return Create(actualPolicy);
        Array.Sort(pending, (left, right) =>
        {
            var comparison = actualPolicy.Comparer.Compare(left.Item, right.Item);
            return comparison != 0 ? comparison : left.Sequence.CompareTo(right.Sequence);
        });

        var unique = new List<BuilderNode>(pending.Length);
        for (var index = 0; index < pending.Length;)
        {
            var rank = actualPolicy.GetRank(pending[index].Item);
            var end = index + 1;
            while (end < pending.Length && actualPolicy.Comparer.Compare(pending[index].Item, pending[end].Item) == 0)
            {
                if (actualPolicy.GetRank(pending[end].Item) != rank)
                    throw InconsistentRankHash();
                end++;
            }
            unique.Add(new BuilderNode(pending[index].Item, rank));
            index = end;
        }
        return new CanonicalSortedSet<T>(BuildCanonical(unique, actualPolicy.Comparer), actualPolicy);
    }

    /// <summary>Determines whether an equivalent item is present.</summary>
    /// <param name="item">The probe item.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool Contains(T item) => FindNode(item) is not null;

    /// <summary>Recovers the stored representative for an equivalent item.</summary>
    /// <param name="equalItem">The probe item.</param>
    /// <param name="actualItem">The stored representative on success; otherwise, <paramref name="equalItem"/>.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool TryGetValue(T equalItem, out T actualItem)
    {
        var node = FindNode(equalItem);
        if (node is not null)
        {
            actualItem = node.Item;
            return true;
        }
        actualItem = equalItem;
        return false;
    }

    /// <summary>Adds an item, preserving identity when an equivalent item is present.</summary>
    /// <param name="item">The item.</param>
    /// <returns>The updated set.</returns>
    public CanonicalSortedSet<T> Add(T item)
    {
        var existing = FindNode(item);
        if (existing is not null)
        {
            if (existing.Rank != Policy.GetRank(item))
                throw InconsistentRankHash();
            return this;
        }
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
        if (ReferenceEquals(_root, other._root))
            return this;
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

    /// <summary>Determines semantic equality, using canonical lockstep shape for a shared policy.</summary>
    /// <param name="other">The set to compare.</param>
    /// <returns><see langword="true"/> when the contents are equal under this set's comparer.</returns>
    public bool SetEquals(CanonicalSortedSet<T>? other)
    {
        if (ReferenceEquals(this, other))
            return true;
        if (other is null)
            return false;
        if (ReferenceEquals(Policy, other.Policy))
        {
            if (Count != other.Count || ContentHash != other.ContentHash)
                return false;
            return NodesEqual(_root, other._root, Policy.Comparer);
        }
        return SemanticSetEquals(other);
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
        if (other is CanonicalSortedSet<T> set && ReferenceEquals(Policy, set.Policy))
            return SetEquals(set);
        return SemanticSetEquals(other);
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

    private Node? FindNode(T item)
    {
        var node = _root;
        while (node is not null)
        {
            var comparison = Policy.Comparer.Compare(item, node.Item);
            if (comparison == 0)
                return node;
            node = comparison < 0 ? node.Left : node.Right;
        }
        return null;
    }

    private bool SemanticSetEquals(IEnumerable<T> other)
    {
        var probe = new System.Collections.Generic.SortedSet<T>(other, Policy.Comparer);
        return Count == probe.Count && probe.All(Contains);
    }

    private void EnsureCompatible(CanonicalSortedSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Policy, other.Policy))
            throw new ArgumentException("Canonical set algebra requires the same rank-policy object.", nameof(other));
    }

    private static Node? BuildCanonical(IReadOnlyList<BuilderNode> nodes, IComparer<T> comparer)
    {
        if (nodes.Count == 0)
            return null;
        var spine = new Stack<BuilderNode>();
        foreach (var node in nodes)
        {
            BuilderNode? left = null;
            while (spine.TryPeek(out var previous)
                && Higher(node.Item, node.Rank, previous.Item, previous.Rank, comparer))
            {
                left = spine.Pop();
            }
            node.Left = left;
            if (spine.TryPeek(out var parent))
                parent.Right = node;
            spine.Push(node);
        }

        while (spine.Count > 1)
            _ = spine.Pop();
        var root = spine.Pop();
        var pending = new Stack<(BuilderNode Node, bool Expanded)>();
        pending.Push((root, false));
        while (pending.TryPop(out var frame))
        {
            if (frame.Expanded)
            {
                frame.Node.Frozen = new Node(
                    frame.Node.Item,
                    frame.Node.Rank,
                    frame.Node.Left?.Frozen,
                    frame.Node.Right?.Frozen);
                continue;
            }
            pending.Push((frame.Node, true));
            if (frame.Node.Right is not null)
                pending.Push((frame.Node.Right, false));
            if (frame.Node.Left is not null)
                pending.Push((frame.Node.Left, false));
        }
        return root.Frozen;
    }

    private static Node Insert(Node? root, Node item, IComparer<T> comparer)
    {
        if (root is null)
            return item;
        var path = new Stack<PathStep>();
        var node = root;
        while (node is not null && !Higher(item, node, comparer))
        {
            var wentLeft = comparer.Compare(item.Item, node.Item) < 0;
            path.Push(new PathStep(node, wentLeft));
            node = wentLeft ? node.Left : node.Right;
        }
        var (left, right) = Split(node, item.Item, comparer);
        Node result = new(item.Item, item.Rank, left, right);
        while (path.TryPop(out var step))
        {
            result = step.WentLeft
                ? new Node(step.Node.Item, step.Node.Rank, result, step.Node.Right)
                : new Node(step.Node.Item, step.Node.Rank, step.Node.Left, result);
        }
        return result;
    }

    private static (Node? Left, Node? Right) Split(Node? root, T item, IComparer<T> comparer)
    {
        var path = new Stack<PathStep>();
        var node = root;
        while (node is not null)
        {
            var wentLeft = comparer.Compare(item, node.Item) < 0;
            path.Push(new PathStep(node, wentLeft));
            node = wentLeft ? node.Left : node.Right;
        }

        Node? left = null;
        Node? right = null;
        while (path.TryPop(out var step))
        {
            if (step.WentLeft)
                right = new Node(step.Node.Item, step.Node.Rank, right, step.Node.Right);
            else
                left = new Node(step.Node.Item, step.Node.Rank, step.Node.Left, left);
        }
        return (left, right);
    }

    private static Node? Remove(Node? root, T item, IComparer<T> comparer, out bool removed)
    {
        var path = new Stack<PathStep>();
        var node = root;
        while (node is not null)
        {
            var comparison = comparer.Compare(item, node.Item);
            if (comparison == 0)
                break;
            var wentLeft = comparison < 0;
            path.Push(new PathStep(node, wentLeft));
            node = wentLeft ? node.Left : node.Right;
        }
        if (node is null)
        {
            removed = false;
            return root;
        }
        removed = true;
        var result = Merge(node.Left, node.Right, comparer);
        while (path.TryPop(out var step))
        {
            result = step.WentLeft
                ? new Node(step.Node.Item, step.Node.Rank, result, step.Node.Right)
                : new Node(step.Node.Item, step.Node.Rank, step.Node.Left, result);
        }
        return result;
    }

    private static Node? Merge(Node? left, Node? right, IComparer<T> comparer)
    {
        if (left is null)
            return right;
        if (right is null)
            return left;
        var path = new Stack<MergeStep>();
        while (left is not null && right is not null)
        {
            if (Higher(left, right, comparer))
            {
                path.Push(new MergeStep(left, ChoseLeft: true));
                left = left.Right;
            }
            else
            {
                path.Push(new MergeStep(right, ChoseLeft: false));
                right = right.Left;
            }
        }
        Node? result = left ?? right;
        while (path.TryPop(out var step))
        {
            result = step.ChoseLeft
                ? new Node(step.Node.Item, step.Node.Rank, step.Node.Left, result)
                : new Node(step.Node.Item, step.Node.Rank, result, step.Node.Right);
        }
        return result;
    }

    private static bool Higher(Node left, Node right, IComparer<T> comparer) =>
        Higher(left.Item, left.Rank, right.Item, right.Rank, comparer);

    private static bool Higher(
        T leftItem,
        ZipTreeRankPolicy<T>.Rank leftRank,
        T rightItem,
        ZipTreeRankPolicy<T>.Rank rightRank,
        IComparer<T> comparer)
    {
        var rank = leftRank.Geometric.CompareTo(rightRank.Geometric);
        if (rank != 0)
            return rank > 0;
        rank = leftRank.Secondary.CompareTo(rightRank.Secondary);
        if (rank != 0)
            return rank > 0;
        return comparer.Compare(leftItem, rightItem) < 0;
    }

    private static bool NodesEqual(Node? left, Node? right, IComparer<T> comparer)
    {
        var pending = new Stack<(Node? Left, Node? Right)>();
        pending.Push((left, right));
        while (pending.TryPop(out var pair))
        {
            if (ReferenceEquals(pair.Left, pair.Right))
                continue;
            if (pair.Left is null || pair.Right is null || comparer.Compare(pair.Left.Item, pair.Right.Item) != 0)
                return false;
            pending.Push((pair.Left.Right, pair.Right.Right));
            pending.Push((pair.Left.Left, pair.Right.Left));
        }
        return true;
    }

    private static InvalidOperationException InconsistentRankHash() => new(
        "The rank hash is not constant on the set comparer's equivalence classes.");

    private readonly record struct PendingItem(T Item, int Sequence);

    private readonly record struct PathStep(Node Node, bool WentLeft);

    private readonly record struct MergeStep(Node Node, bool ChoseLeft);

    private readonly record struct KeyBound
    {
        internal KeyBound(T value)
        {
            HasValue = true;
            Value = value;
        }

        internal bool HasValue { get; }

        internal T Value { get; }
    }

    private readonly record struct ValidationFrame(
        Node Node,
        KeyBound Lower,
        KeyBound Upper,
        int Depth);

    private sealed class BuilderNode(T item, ZipTreeRankPolicy<T>.Rank rank)
    {
        internal T Item { get; } = item;

        internal ZipTreeRankPolicy<T>.Rank Rank { get; } = rank;

        internal BuilderNode? Left { get; set; }

        internal BuilderNode? Right { get; set; }

        internal Node? Frozen { get; set; }
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
            var pending = new Stack<(Node Node, bool Expanded)>();
            pending.Push((this, false));
            while (pending.TryPop(out var frame))
            {
                if (Volatile.Read(ref frame.Node._digest) is ulong)
                    continue;
                if (!frame.Expanded)
                {
                    pending.Push((frame.Node, true));
                    if (frame.Node.Right is { } right && Volatile.Read(ref right._digest) is null)
                        pending.Push((right, false));
                    if (frame.Node.Left is { } left && Volatile.Read(ref left._digest) is null)
                        pending.Push((left, false));
                    continue;
                }

                var leftHash = frame.Node.Left is null
                    ? 0x243f6a8885a308d3UL
                    : (ulong)Volatile.Read(ref frame.Node.Left._digest)!;
                var rightHash = frame.Node.Right is null
                    ? 0x13198a2e03707344UL
                    : (ulong)Volatile.Read(ref frame.Node.Right._digest)!;
                var digest = ZipTreeRankPolicy<T>.Mix(
                    frame.Node.Rank.Hash
                    ^ BitOperations.RotateLeft(leftHash, 17)
                    ^ BitOperations.RotateLeft(rightHash, 43));
                Interlocked.CompareExchange(ref frame.Node._digest, digest, null);
            }
            return (ulong)Volatile.Read(ref _digest)!;
        }
    }
}

/// <summary>Describes a validated canonical zip-zip set representation.</summary>
/// <param name="Count">The item count.</param>
/// <param name="Height">The maximum root-to-node count.</param>
/// <param name="MaximumGeometricRank">The largest first-coordinate rank.</param>
/// <param name="PriorityCollisionCount">The number of repeated geometric/secondary rank pairs.</param>
public readonly record struct CanonicalSortedSetStatistics(
    int Count,
    int Height,
    int MaximumGeometricRank,
    int PriorityCollisionCount);
