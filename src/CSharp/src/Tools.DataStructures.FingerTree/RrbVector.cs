using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

/// <summary>Represents an immutable relaxed radix-balanced vector with 32-way branching.</summary>
/// <typeparam name="T">The element type.</typeparam>
/// <remarks>
/// Packed branches omit size tables and select children with five-bit radix arithmetic. Only
/// branches whose child spans have been relaxed by split or concatenation retain cumulative sizes.
/// Updates copy one root-to-leaf path. Concatenation merges and rebalances only the two boundary
/// spines and is O(log32(n + m)); it does not impose a global minimum-occupancy invariant on nodes
/// away from that seam. Endpoint operations do not currently use a dedicated tail buffer; use
/// <see cref="Builder"/> for append-heavy bulk construction.
/// </remarks>
[DebuggerDisplay("Count = {Count}, Height = {Height}")]
public sealed class RrbVector<T> : IReadOnlyList<T>
{
    private const int RadixBits = 5;
    private const int BranchFactor = 32;
    private const int MaximumHeight = (sizeof(int) * 8 - 1) / RadixBits;
    private readonly Node? _root;

    private RrbVector(Node? root) => _root = root;

    /// <summary>Gets the shared empty vector.</summary>
    public static RrbVector<T> Empty { get; } = new(root: null);

    /// <summary>Gets the number of elements.</summary>
    public int Count => _root?.Count ?? 0;

    /// <summary>Gets whether the vector is empty.</summary>
    public bool IsEmpty => _root is null;

    /// <summary>Gets the internal tree height, where a leaf has height zero.</summary>
    public int Height => _root?.Height ?? 0;

    /// <summary>Gets the element at a zero-based index.</summary>
    /// <param name="index">The zero-based index.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the vector.</exception>
    public T this[int index]
    {
        get
        {
            ArgumentOutOfRangeException.ThrowIfNegative(index);
            if (index >= Count)
                throw new ArgumentOutOfRangeException(nameof(index));
            return Get(_root!, index);
        }
    }

    internal object? RootIdentity => _root;

    /// <summary>Creates an empty mutable append-only builder.</summary>
    /// <returns>A builder whose first immutable snapshot is <see cref="Empty"/>.</returns>
    public static Builder CreateBuilder() => new(Empty);

    /// <summary>Creates a mutable append-only builder with this vector as an O(1) frozen prefix.</summary>
    /// <returns>A builder containing this vector's elements.</returns>
    public Builder ToBuilder() => new(this);

    /// <summary>Creates a vector from a sequence in enumeration order.</summary>
    /// <param name="items">The elements to store.</param>
    /// <returns>A vector containing <paramref name="items"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static RrbVector<T> CreateRange(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items is IReadOnlyCollection<T> collection && collection.Count == 0)
            return Empty;

        var builder = CreateBuilder();
        builder.AddRange(items);
        return builder.ToImmutable();
    }

    /// <summary>Returns a vector with one element appended.</summary>
    /// <param name="value">The element to append.</param>
    /// <returns>The updated vector.</returns>
    public RrbVector<T> AddLast(T value) => Concat(new RrbVector<T>(new Leaf([value])));

    /// <summary>Returns a vector with one element prepended.</summary>
    /// <param name="value">The element to prepend.</param>
    /// <returns>The updated vector.</returns>
    public RrbVector<T> AddFirst(T value) => new RrbVector<T>(new Leaf([value])).Concat(this);

    /// <summary>Replaces the element at an index.</summary>
    /// <param name="index">The zero-based index.</param>
    /// <param name="value">The replacement.</param>
    /// <returns>The updated vector, or this vector when the value compares equal.</returns>
    public RrbVector<T> SetItem(int index, T value)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        if (index >= Count)
            throw new ArgumentOutOfRangeException(nameof(index));
        var root = Set(_root!, index, value);
        return ReferenceEquals(root, _root) ? this : new RrbVector<T>(root);
    }

    /// <summary>Concatenates another vector by rebalancing only the boundary spines.</summary>
    /// <param name="other">The suffix vector.</param>
    /// <returns>The concatenated vector.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public RrbVector<T> Concat(RrbVector<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (_root is null)
            return other;
        if (other._root is null)
            return this;
        _ = checked(Count + other.Count);

        var roots = ConcatNodes(_root, other._root);
        return FromRoot(roots.Length == 1 ? roots[0] : new Branch(roots));
    }

    /// <summary>Splits the vector at a zero-based boundary.</summary>
    /// <param name="index">The number of elements placed in the left result.</param>
    /// <returns>The prefix and suffix.</returns>
    public (RrbVector<T> Left, RrbVector<T> Right) SplitAt(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        if (index > Count)
            throw new ArgumentOutOfRangeException(nameof(index));
        if (index == 0)
            return (Empty, this);
        if (index == Count)
            return (this, Empty);

        var (left, right) = Split(_root!, index);
        return (FromRoot(left), FromRoot(right));
    }

    /// <summary>Inserts elements at a zero-based boundary.</summary>
    /// <param name="index">The insertion boundary.</param>
    /// <param name="items">The elements to insert.</param>
    /// <returns>The updated vector.</returns>
    public RrbVector<T> InsertRange(int index, IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        var (left, right) = SplitAt(index);
        var middle = CreateRange(items);
        return middle.IsEmpty ? this : left.Concat(middle).Concat(right);
    }

    /// <summary>Removes a contiguous range.</summary>
    /// <param name="index">The first index to remove.</param>
    /// <param name="count">The number of elements to remove.</param>
    /// <returns>The updated vector.</returns>
    public RrbVector<T> RemoveRange(int index, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (index > Count)
            throw new ArgumentOutOfRangeException(nameof(index));
        if (index > Count - count)
            throw new ArgumentOutOfRangeException(nameof(count));
        if (count == 0)
            return this;
        var (left, tail) = SplitAt(index);
        var (_, right) = tail.SplitAt(count);
        return left.Concat(right);
    }

    /// <summary>Removes and returns the last element.</summary>
    /// <param name="result">The remaining vector on success; otherwise, this vector.</param>
    /// <param name="value">The removed element on success.</param>
    /// <returns><see langword="true"/> when an element was removed.</returns>
    public bool TryRemoveLast(out RrbVector<T> result, [MaybeNullWhen(false)] out T value)
    {
        if (_root is null)
        {
            result = this;
            value = default;
            return false;
        }
        value = this[^1];
        result = RemoveRange(Count - 1, 1);
        return true;
    }

    /// <summary>Returns a stable enumerator in element order.</summary>
    /// <returns>An enumerator.</returns>
    public IEnumerator<T> GetEnumerator() => Enumerate().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>Validates cached counts, heights, branch layouts, and relaxed size tables.</summary>
    /// <param name="statistics">Representation statistics collected during validation.</param>
    /// <returns><see langword="true"/> when every internal invariant holds.</returns>
    internal bool ValidateStructure(out RrbVectorStatistics statistics)
    {
        if (_root is null)
        {
            statistics = new RrbVectorStatistics(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
            return true;
        }

        var accumulator = new ValidationAccumulator();
        var valid = ValidateNode(_root, isRoot: true, accumulator, out var count, out var height);
        statistics = accumulator.ToStatistics(count, height);
        return valid && count == Count && height == Height && height <= MaximumHeight;
    }

    /// <summary>Returns leaf-node identities in enumeration order for structural-sharing tests.</summary>
    internal object[] LeafIdentitiesForTesting()
    {
        if (_root is null)
            return [];

        var result = new List<object>();
        var stack = new Stack<Node>();
        stack.Push(_root);
        while (stack.TryPop(out var node))
        {
            if (node is Leaf leaf)
            {
                result.Add(leaf);
                continue;
            }

            var branch = (Branch)node;
            for (var i = branch.Children.Length - 1; i >= 0; i--)
                stack.Push(branch.Children[i]);
        }

        return [.. result];
    }

    private IEnumerable<T> Enumerate()
    {
        if (_root is null)
            yield break;
        var stack = new Stack<Node>();
        stack.Push(_root);
        while (stack.TryPop(out var node))
        {
            if (node is Leaf leaf)
            {
                foreach (var item in leaf.Items)
                    yield return item;
                continue;
            }
            var branch = (Branch)node;
            for (var i = branch.Children.Length - 1; i >= 0; i--)
                stack.Push(branch.Children[i]);
        }
    }

    private static RrbVector<T> FromRoot(Node? root)
    {
        while (root is Branch { Children.Length: 1 } branch)
            root = branch.Children[0];
        return root is null ? Empty : new RrbVector<T>(root);
    }

    private static T Get(Node node, int index)
    {
        while (node is Branch branch)
        {
            var childIndex = branch.FindChild(index, out var before);
            index -= before;
            node = branch.Children[childIndex];
        }
        return ((Leaf)node).Items[index];
    }

    private static Node Set(Node node, int index, T value)
    {
        if (node is Leaf leaf)
        {
            if (EqualityComparer<T>.Default.Equals(leaf.Items[index], value))
                return leaf;
            var items = (T[])leaf.Items.Clone();
            items[index] = value;
            return new Leaf(items);
        }

        var branch = (Branch)node;
        var childIndex = branch.FindChild(index, out var before);
        var localIndex = index - before;
        var child = Set(branch.Children[childIndex], localIndex, value);
        if (ReferenceEquals(child, branch.Children[childIndex]))
            return branch;
        var children = (Node[])branch.Children.Clone();
        children[childIndex] = child;
        return new Branch(children);
    }

    private static Node[] ConcatNodes(Node left, Node right)
    {
        if (left.Height == right.Height)
            return ConcatSameHeight(left, right);
        if (left.Height > right.Height)
        {
            var branch = (Branch)left;
            var boundary = ConcatNodes(branch.Children[^1], right);
            return Partition(branch.Children[..^1].Concat(boundary).ToArray());
        }

        var rightBranch = (Branch)right;
        var leading = ConcatNodes(left, rightBranch.Children[0]);
        return Partition(leading.Concat(rightBranch.Children[1..]).ToArray());
    }

    private static Node[] ConcatSameHeight(Node left, Node right)
    {
        if (left is Leaf leftLeaf)
        {
            var rightLeaf = (Leaf)right;
            if (leftLeaf.Count == BranchFactor && rightLeaf.Count == BranchFactor)
                return [leftLeaf, rightLeaf];

            var combined = new T[leftLeaf.Count + rightLeaf.Count];
            Array.Copy(leftLeaf.Items, combined, leftLeaf.Count);
            Array.Copy(rightLeaf.Items, 0, combined, leftLeaf.Count, rightLeaf.Count);
            if (combined.Length <= BranchFactor)
                return [new Leaf(combined)];
            var split = combined.Length / 2;
            return [new Leaf(combined[..split]), new Leaf(combined[split..])];
        }

        var l = (Branch)left;
        var r = (Branch)right;
        var boundary = ConcatSameHeight(l.Children[^1], r.Children[0]);
        return Partition(l.Children[..^1].Concat(boundary).Concat(r.Children[1..]).ToArray());
    }

    private static Node[] Partition(Node[] children)
    {
        if (children.Length <= BranchFactor)
            return [new Branch(children)];
        var split = children.Length / 2;
        return [new Branch(children[..split]), new Branch(children[split..])];
    }

    private static (Node? Left, Node? Right) Split(Node node, int index)
    {
        if (index == 0)
            return (null, node);
        if (index == node.Count)
            return (node, null);

        if (node is Leaf leaf)
        {
            return (new Leaf(leaf.Items[..index]), new Leaf(leaf.Items[index..]));
        }

        var branch = (Branch)node;
        var childIndex = branch.FindChild(index, out var before);
        var local = index - before;
        var (childLeft, childRight) = Split(branch.Children[childIndex], local);

        var left = branch.Children[..childIndex].ToList();
        if (childLeft is not null)
            left.Add(childLeft);
        var right = new List<Node>();
        if (childRight is not null)
            right.Add(childRight);
        right.AddRange(branch.Children[(childIndex + 1)..]);
        return (BuildSameHeight(left), BuildSameHeight(right));
    }

    private static Node? BuildSameHeight(List<Node> nodes) => nodes.Count switch
    {
        0 => null,
        _ => new Branch([.. nodes]),
    };

    private static Node BuildLevel(List<Node> nodes)
    {
        while (nodes.Count > 1)
        {
            var parents = new List<Node>((nodes.Count + BranchFactor - 1) / BranchFactor);
            for (var i = 0; i < nodes.Count; i += BranchFactor)
                parents.Add(new Branch([.. nodes.GetRange(i, Math.Min(BranchFactor, nodes.Count - i))]));
            nodes = parents;
        }
        return nodes[0];
    }

    private static bool ValidateNode(
        Node node,
        bool isRoot,
        ValidationAccumulator accumulator,
        out int count,
        out int height)
    {
        if (node is Leaf leaf)
        {
            count = leaf.Items.Length;
            height = 0;
            accumulator.AddLeaf(count);
            return count is > 0 and <= BranchFactor && leaf.Count == count && leaf.Height == 0;
        }

        var branch = (Branch)node;
        var children = branch.Children;
        accumulator.AddBranch(children.Length, branch.IsRegular);
        if (children.Length is < 1 or > BranchFactor || isRoot && children.Length == 1)
        {
            count = branch.Count;
            height = branch.Height;
            return false;
        }

        var valid = true;
        var total = 0L;
        var childHeight = -1;
        for (var i = 0; i < children.Length; i++)
        {
            valid &= ValidateNode(children[i], isRoot: false, accumulator, out var childCount, out var actualHeight);
            if (i == 0)
                childHeight = actualHeight;
            else if (actualHeight != childHeight)
                valid = false;
            total += childCount;
        }

        height = childHeight + 1;
        count = total <= int.MaxValue ? (int)total : int.MaxValue;
        valid &= total <= int.MaxValue && branch.Count == total && branch.Height == height;

        var regularLayout = Branch.HasRegularLayout(children, height);
        valid &= branch.IsRegular == regularLayout;
        if (branch.CumulativeSizes is null)
        {
            valid &= regularLayout;
        }
        else
        {
            valid &= !regularLayout && branch.CumulativeSizes.Length == children.Length;
            var cumulative = 0L;
            for (var i = 0; i < children.Length; i++)
            {
                cumulative += children[i].Count;
                valid &= branch.CumulativeSizes[i] == cumulative;
            }
        }

        return valid;
    }

    /// <summary>Mutable append-only staging surface with immutable cached snapshots.</summary>
    /// <remarks>
    /// The builder owns every mutable tail array. Full leaves are transferred only after the builder
    /// stops mutating their arrays; a partial tail is copied when frozen. Mutating the builder after
    /// <see cref="ToImmutable"/> therefore cannot change an earlier vector.
    /// </remarks>
    public sealed class Builder : IReadOnlyCollection<T>
    {
        private readonly List<Node> _leaves = [];
        private RrbVector<T> _prefix;
        private T[] _tail = new T[BranchFactor];
        private int _tailCount;
        private int _stagedCount;
        private int _version;

        internal Builder(RrbVector<T> prefix) => _prefix = prefix;

        /// <summary>Gets the number of elements in the frozen prefix and staged tail.</summary>
        public int Count => _prefix.Count + _stagedCount;

        /// <summary>Appends one element in amortized O(1).</summary>
        public void Add(T item)
        {
            EnsureCanAdd(1);
            AddCore(item);
            _version++;
        }

        /// <summary>Appends a span in O(m).</summary>
        public void AddRange(ReadOnlySpan<T> items)
        {
            if (items.IsEmpty)
                return;
            EnsureCanAdd(items.Length);
            while (!items.IsEmpty)
            {
                var copied = Math.Min(BranchFactor - _tailCount, items.Length);
                items[..copied].CopyTo(_tail.AsSpan(_tailCount));
                _tailCount += copied;
                _stagedCount += copied;
                items = items[copied..];
                if (_tailCount == BranchFactor)
                    FreezeFullTail();
            }
            _version++;
        }

        /// <summary>Appends an enumerable source once in O(m).</summary>
        public void AddRange(IEnumerable<T> items)
        {
            ArgumentNullException.ThrowIfNull(items);
            if (items is T[] array)
            {
                AddRange(array.AsSpan());
                return;
            }

            foreach (var item in items)
                Add(item);
        }

        /// <summary>Removes the frozen prefix and all staged elements.</summary>
        public void Clear()
        {
            if (Count == 0)
                return;
            _prefix = Empty;
            _leaves.Clear();
            Array.Clear(_tail, 0, _tailCount);
            _tailCount = 0;
            _stagedCount = 0;
            _version++;
        }

        /// <summary>Freezes staged leaves; clean repeated calls return the same vector instance.</summary>
        public RrbVector<T> ToImmutable()
        {
            if (_stagedCount == 0)
                return _prefix;

            var nodes = new List<Node>(_leaves.Count + (_tailCount == 0 ? 0 : 1));
            nodes.AddRange(_leaves);
            if (_tailCount != 0)
            {
                var tail = new T[_tailCount];
                Array.Copy(_tail, tail, _tailCount);
                nodes.Add(new Leaf(tail));
                Array.Clear(_tail, 0, _tailCount);
            }

            var staged = new RrbVector<T>(BuildLevel(nodes));
            _prefix = _prefix.IsEmpty ? staged : _prefix.Concat(staged);
            _leaves.Clear();
            _tailCount = 0;
            _stagedCount = 0;
            return _prefix;
        }

        /// <summary>Returns a fail-fast enumerator over a frozen snapshot.</summary>
        public IEnumerator<T> GetEnumerator()
        {
            var version = _version;
            return Enumerate(ToImmutable(), version);
        }

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        private IEnumerator<T> Enumerate(RrbVector<T> snapshot, int version)
        {
            foreach (var item in snapshot)
            {
                ThrowIfVersionChanged(version);
                yield return item;
            }
            ThrowIfVersionChanged(version);
        }

        private void AddCore(T item)
        {
            _tail[_tailCount++] = item;
            _stagedCount++;
            if (_tailCount == BranchFactor)
                FreezeFullTail();
        }

        private void FreezeFullTail()
        {
            Debug.Assert(_tailCount == BranchFactor);
            _leaves.Add(new Leaf(_tail));
            _tail = new T[BranchFactor];
            _tailCount = 0;
        }

        private void EnsureCanAdd(int count)
        {
            if (count > int.MaxValue - Count)
                throw new OverflowException("The RRB vector builder cannot contain more than Int32.MaxValue elements.");
        }

        private void ThrowIfVersionChanged(int capturedVersion)
        {
            if (_version != capturedVersion)
                throw new InvalidOperationException("The RRB vector builder was modified during enumeration.");
        }
    }

    private sealed class ValidationAccumulator
    {
        private int _minimumLeafLength = int.MaxValue;
        private int _maximumLeafLength;
        private int _minimumBranchingFactor = int.MaxValue;
        private int _maximumBranchingFactor;

        internal int LeafCount { get; private set; }
        internal int BranchCount { get; private set; }
        internal int RegularBranchCount { get; private set; }
        internal int RelaxedBranchCount { get; private set; }

        internal void AddLeaf(int length)
        {
            LeafCount++;
            _minimumLeafLength = Math.Min(_minimumLeafLength, length);
            _maximumLeafLength = Math.Max(_maximumLeafLength, length);
        }

        internal void AddBranch(int branchingFactor, bool regular)
        {
            BranchCount++;
            if (regular)
                RegularBranchCount++;
            else
                RelaxedBranchCount++;
            _minimumBranchingFactor = Math.Min(_minimumBranchingFactor, branchingFactor);
            _maximumBranchingFactor = Math.Max(_maximumBranchingFactor, branchingFactor);
        }

        internal RrbVectorStatistics ToStatistics(int count, int height) => new(
            count,
            height,
            LeafCount,
            BranchCount,
            RegularBranchCount,
            RelaxedBranchCount,
            LeafCount == 0 ? 0 : _minimumLeafLength,
            _maximumLeafLength,
            BranchCount == 0 ? 0 : _minimumBranchingFactor,
            _maximumBranchingFactor);
    }

    private abstract class Node
    {
        internal abstract int Count { get; }

        internal abstract int Height { get; }
    }

    private sealed class Leaf(T[] items) : Node
    {
        internal T[] Items { get; } = items;

        internal override int Count => Items.Length;

        internal override int Height => 0;
    }

    private sealed class Branch : Node
    {
        internal Branch(Node[] children)
        {
            Debug.Assert(children is { Length: > 0 and <= BranchFactor });
            Children = children;
            Height = checked(children[0].Height + 1);

            var count = 0;
            var childHeight = children[0].Height;
            for (var i = 0; i < children.Length; i++)
            {
                Debug.Assert(children[i].Height == childHeight);
                count = checked(count + children[i].Count);
            }
            Count = count;
            CumulativeSizes = HasRegularLayout(children, Height) ? null : BuildSizes(children);
        }

        internal Node[] Children { get; }

        internal int[]? CumulativeSizes { get; }

        internal bool IsRegular => CumulativeSizes is null;

        internal override int Count { get; }

        internal override int Height { get; }

        internal int FindChild(int index, out int before)
        {
            Debug.Assert((uint)index < (uint)Count);
            if (CumulativeSizes is null)
            {
                var shift = Height * RadixBits;
                var childIndex = (int)((long)index >> shift);
                before = (int)((long)childIndex << shift);
                Debug.Assert((uint)childIndex < (uint)Children.Length);
                return childIndex;
            }

            for (var i = 0; i < CumulativeSizes.Length; i++)
            {
                if (index >= CumulativeSizes[i])
                    continue;
                before = i == 0 ? 0 : CumulativeSizes[i - 1];
                return i;
            }

            throw new UnreachableException();
        }

        internal static bool HasRegularLayout(Node[] children, int height)
        {
            if (children.Length == 0 || height is < 1 or > MaximumHeight)
                return false;

            var childCapacity = 1L << (height * RadixBits);
            for (var i = 0; i < children.Length - 1; i++)
            {
                if (children[i].Count != childCapacity)
                    return false;
            }

            return children[^1].Count <= childCapacity;
        }

        private static int[] BuildSizes(Node[] children)
        {
            var sizes = new int[children.Length];
            var count = 0;
            for (var i = 0; i < children.Length; i++)
            {
                count = checked(count + children[i].Count);
                sizes[i] = count;
            }
            return sizes;
        }
    }
}

internal readonly record struct RrbVectorStatistics(
    int Count,
    int Height,
    int LeafCount,
    int BranchCount,
    int RegularBranchCount,
    int RelaxedBranchCount,
    int MinimumLeafLength,
    int MaximumLeafLength,
    int MinimumBranchingFactor,
    int MaximumBranchingFactor);
