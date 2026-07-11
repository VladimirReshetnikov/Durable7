using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

/// <summary>Represents an immutable relaxed radix-balanced vector with 32-way branching.</summary>
/// <typeparam name="T">The element type.</typeparam>
/// <remarks>
/// Branches store cumulative size tables, so indexing is O(log32 n) even after concatenation has
/// produced relaxed nodes. Updates copy one root-to-leaf path. Concatenation merges and rebalances
/// only the two boundary spines and is O(log32(n + m)).
/// </remarks>
[DebuggerDisplay("Count = {Count}, Height = {Height}")]
public sealed class RrbVector<T> : IReadOnlyList<T>
{
    private const int BranchFactor = 32;
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

    /// <summary>Creates a vector from a sequence in enumeration order.</summary>
    /// <param name="items">The elements to store.</param>
    /// <returns>A vector containing <paramref name="items"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static RrbVector<T> CreateRange(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items is IReadOnlyCollection<T> collection && collection.Count == 0)
            return Empty;

        var leaves = new List<Node>();
        var chunk = new List<T>(BranchFactor);
        foreach (var item in items)
        {
            chunk.Add(item);
            if (chunk.Count != BranchFactor)
                continue;
            leaves.Add(new Leaf([.. chunk]));
            chunk.Clear();
        }
        if (chunk.Count != 0)
            leaves.Add(new Leaf([.. chunk]));
        if (leaves.Count == 0)
            return Empty;

        return new RrbVector<T>(BuildLevel(leaves));
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
        return new RrbVector<T>(roots.Length == 1 ? roots[0] : new Branch(roots));
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
            var childIndex = branch.FindChild(index);
            if (childIndex != 0)
                index -= branch.Sizes[childIndex - 1];
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
        var childIndex = branch.FindChild(index);
        var localIndex = childIndex == 0 ? index : index - branch.Sizes[childIndex - 1];
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
            var combined = leftLeaf.Items.Concat(((Leaf)right).Items).ToArray();
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
        if (node is Leaf leaf)
        {
            return (
                index == 0 ? null : new Leaf(leaf.Items[..index]),
                index == leaf.Count ? null : new Leaf(leaf.Items[index..]));
        }

        var branch = (Branch)node;
        var childIndex = branch.FindChild(index == branch.Count ? index - 1 : index);
        var before = childIndex == 0 ? 0 : branch.Sizes[childIndex - 1];
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

    private sealed class Branch(Node[] children) : Node
    {
        internal Node[] Children { get; } = children;

        internal int[] Sizes { get; } = BuildSizes(children);

        internal override int Count => Sizes[^1];

        internal override int Height { get; } = checked(children[0].Height + 1);

        internal int FindChild(int index)
        {
            for (var i = 0; i < Sizes.Length; i++)
            {
                if (index < Sizes[i])
                    return i;
            }
            return Sizes.Length - 1;
        }

        private static int[] BuildSizes(Node[] children)
        {
            Debug.Assert(children is { Length: > 0 and <= BranchFactor });
            var sizes = new int[children.Length];
            var count = 0;
            var height = children[0].Height;
            for (var i = 0; i < children.Length; i++)
            {
                Debug.Assert(children[i].Height == height);
                count = checked(count + children[i].Count);
                sizes[i] = count;
            }
            return sizes;
        }
    }
}
