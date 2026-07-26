// Represents an immutable bootstrapped skew-binomial min-heap.

using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

/// <summary>Represents an immutable bootstrapped skew-binomial min-heap.</summary>
/// <typeparam name="T">The element type.</typeparam>
/// <remarks>
/// This is the Brodal–Okasaki purely functional priority queue: insert, minimum, and meld are O(1)
/// worst-case; delete-min is O(log n) worst-case. Every update preserves prior heap versions.
/// </remarks>
[DebuggerDisplay("Count = {Count}, Minimum = {MinimumForDebugger}")]
public sealed class BrodalOkasakiHeap<T> : IEnumerable<T>
{
    private readonly Tree? _root;

    private BrodalOkasakiHeap(Tree? root, int count, IComparer<T> comparer)
    {
        _root = root;
        Count = count;
        Comparer = comparer;
    }

    /// <summary>Gets the shared empty heap using the default comparer.</summary>
    public static BrodalOkasakiHeap<T> Empty { get; } = new(null, 0, Comparer<T>.Default);

    /// <summary>Gets the number of elements.</summary>
    public int Count { get; }

    /// <summary>Gets whether the heap is empty.</summary>
    public bool IsEmpty => _root is null;

    /// <summary>Gets the retained priority comparer.</summary>
    public IComparer<T> Comparer { get; }

    /// <summary>Gets the minimum element.</summary>
    /// <exception cref="InvalidOperationException">The heap is empty.</exception>
    public T Minimum => _root is null
        ? throw new InvalidOperationException("The heap is empty.")
        : _root.Value;

    private object? MinimumForDebugger => _root is null ? null : _root.Value;

    internal object? RootIdentity => _root;

    /// <summary>
    /// Validates the global root, every fused primitive-child/embedded-forest boundary,
    /// skew-forest ranks, heap order, and logical count in O(n) time.
    /// </summary>
    /// <returns>Statistics for the validated bootstrapped skew-binomial representation.</returns>
    /// <remarks>The traversal uses an explicit stack requiring O(n) space in the worst case.</remarks>
    public BrodalOkasakiHeapStatistics ValidateStructure()
    {
        if (_root is null)
        {
            if (Count != 0)
                throw new InvalidOperationException("An empty Brodal-Okasaki heap has a nonzero count.");
            return new BrodalOkasakiHeapStatistics(0, 0, 0, 0);
        }
        if (_root.Rank != 0)
            throw new InvalidOperationException("The Brodal-Okasaki global root must have rank zero.");
        var rootForestLength = ValidateSkewForest(_root.Children);
        var pending = new Stack<TreeFrame>();
        pending.Push(new TreeFrame(_root, 1));
        var logicalCount = 0;
        var maximumRank = 0;
        var maximumDepth = 0;
        while (pending.TryPop(out var frame))
        {
            var tree = frame.Tree;
            if (tree.Rank < 0)
                throw new InvalidOperationException("A Brodal-Okasaki tree has a negative rank.");
            logicalCount = checked(logicalCount + 1);
            maximumRank = Math.Max(maximumRank, tree.Rank);
            maximumDepth = Math.Max(maximumDepth, frame.Depth);
            ValidateSkewForest(ValidateFusedChildren(tree));
            for (var forest = tree.Children; forest is not null; forest = forest.Tail)
            {
                if (!LessOrEqual(tree.Value, forest.Head.Value))
                    throw new InvalidOperationException("A Brodal-Okasaki child outranks its parent.");
                pending.Push(new TreeFrame(forest.Head, checked(frame.Depth + 1)));
            }
        }
        if (logicalCount != Count)
            throw new InvalidOperationException("Brodal-Okasaki logical count disagrees with its tree graph.");
        return new BrodalOkasakiHeapStatistics(Count, rootForestLength, maximumRank, maximumDepth);
    }

    /// <summary>Creates an empty heap with a retained comparer.</summary>
    /// <param name="comparer">The comparer, or <see langword="null"/> for the default.</param>
    /// <returns>An empty heap using the comparer.</returns>
    public static BrodalOkasakiHeap<T> Create(IComparer<T>? comparer = null)
    {
        comparer ??= Comparer<T>.Default;
        return ReferenceEquals(comparer, Comparer<T>.Default) ? Empty : new BrodalOkasakiHeap<T>(null, 0, comparer);
    }

    /// <summary>Creates a heap from a sequence in O(n) time by repeated worst-case-O(1) insertion.</summary>
    /// <param name="items">The items to insert.</param>
    /// <param name="comparer">The comparer, or <see langword="null"/> for the default.</param>
    /// <returns>A heap containing the items.</returns>
    public static BrodalOkasakiHeap<T> CreateRange(IEnumerable<T> items, IComparer<T>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = Create(comparer);
        foreach (var item in items)
            result = result.Insert(item);
        return result;
    }

    /// <summary>Tries to get the minimum element in O(1) time.</summary>
    /// <param name="minimum">The minimum on success; otherwise, the default value.</param>
    /// <returns><see langword="true"/> when nonempty.</returns>
    public bool TryGetMinimum([MaybeNullWhen(false)] out T minimum)
    {
        if (_root is null)
        {
            minimum = default;
            return false;
        }
        minimum = _root.Value;
        return true;
    }

    /// <summary>Inserts an element in O(1) worst-case time.</summary>
    /// <param name="item">The item to insert.</param>
    /// <returns>The updated heap.</returns>
    public BrodalOkasakiHeap<T> Insert(T item)
    {
        if (_root is null)
            return new BrodalOkasakiHeap<T>(new Tree(0, item, null), 1, Comparer);
        var root = LessOrEqual(item, _root.Value)
            ? new Tree(0, item, new Forest(_root, null))
            : new Tree(0, _root.Value, SkewInsert(new Tree(0, item, null), _root.Children));
        return new BrodalOkasakiHeap<T>(root, checked(Count + 1), Comparer);
    }

    /// <summary>Melds two heaps in O(1) worst-case time.</summary>
    /// <param name="other">The other heap.</param>
    /// <returns>The melded heap.</returns>
    /// <exception cref="ArgumentException">The heaps do not retain the same comparer object.</exception>
    public BrodalOkasakiHeap<T> Meld(BrodalOkasakiHeap<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Comparer, other.Comparer))
            throw new ArgumentException("Heap melding requires the same comparer object.", nameof(other));
        if (_root is null)
            return other;
        if (other._root is null)
            return this;

        var root = LessOrEqual(_root.Value, other._root.Value)
            ? new Tree(0, _root.Value, SkewInsert(other._root, _root.Children))
            : new Tree(0, other._root.Value, SkewInsert(_root, other._root.Children));
        return new BrodalOkasakiHeap<T>(root, checked(Count + other.Count), Comparer);
    }

    /// <summary>Removes the minimum element in O(log n) worst-case time.</summary>
    /// <returns>The remaining heap.</returns>
    /// <exception cref="InvalidOperationException">The heap is empty.</exception>
    public BrodalOkasakiHeap<T> DeleteMinimum()
    {
        if (_root is null)
            throw new InvalidOperationException("The heap is empty.");
        if (_root.Children is null)
            return Create(Comparer);

        var (minimum, remainder) = GetMinimum(_root.Children);
        var (zeros, trees, forest) = SplitForest(minimum.Rank, null, null, minimum.Children);
        var merged = SkewMeld(SkewMeld(trees, remainder), forest);
        foreach (var zero in ToReverseArray(zeros))
            merged = SkewInsert(zero, merged);
        return new BrodalOkasakiHeap<T>(new Tree(0, minimum.Value, merged), checked(Count - 1), Comparer);
    }

    /// <summary>Tries to remove the minimum element in O(log n) worst-case time.</summary>
    /// <param name="minimum">The removed minimum on success.</param>
    /// <param name="remainder">The remaining heap on success; otherwise, this heap.</param>
    /// <returns><see langword="true"/> when nonempty.</returns>
    public bool TryDeleteMinimum(
        [MaybeNullWhen(false)] out T minimum,
        out BrodalOkasakiHeap<T> remainder)
    {
        if (_root is null)
        {
            minimum = default;
            remainder = this;
            return false;
        }
        minimum = _root.Value;
        remainder = DeleteMinimum();
        return true;
    }

    /// <summary>Returns an O(n)-time enumerator over the heap in unspecified structural order.</summary>
    /// <returns>An enumerator.</returns>
    /// <remarks>The enumerator uses an explicit stack and does not compare elements.</remarks>
    public IEnumerator<T> GetEnumerator() => Enumerate().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private IEnumerable<T> Enumerate()
    {
        if (_root is null)
            yield break;
        var stack = new Stack<Tree>();
        stack.Push(_root);
        while (stack.TryPop(out var tree))
        {
            yield return tree.Value;
            for (var forest = tree.Children; forest is not null; forest = forest.Tail)
                stack.Push(forest.Head);
        }
    }

    private bool LessOrEqual(T left, T right) => Comparer.Compare(left, right) <= 0;

    private Forest SkewInsert(Tree tree, Forest? forest)
    {
        if (forest is { Tail: { } tail } && forest.Head.Rank == tail.Head.Rank)
            return new Forest(SkewLink(tree, forest.Head, tail.Head), tail.Tail);
        return new Forest(tree, forest);
    }

    private Tree SkewLink(Tree zero, Tree first, Tree second)
    {
        Debug.Assert(first.Rank == second.Rank);
        if (LessOrEqual(first.Value, zero.Value) && LessOrEqual(first.Value, second.Value))
            return new Tree(first.Rank + 1, first.Value, new Forest(zero, new Forest(second, first.Children)));
        if (LessOrEqual(second.Value, zero.Value) && LessOrEqual(second.Value, first.Value))
            return new Tree(second.Rank + 1, second.Value, new Forest(zero, new Forest(first, second.Children)));
        return new Tree(first.Rank + 1, zero.Value, new Forest(first, new Forest(second, zero.Children)));
    }

    private Tree Link(Tree first, Tree second)
    {
        Debug.Assert(first.Rank == second.Rank);
        return LessOrEqual(first.Value, second.Value)
            ? new Tree(first.Rank + 1, first.Value, new Forest(second, first.Children))
            : new Tree(second.Rank + 1, second.Value, new Forest(first, second.Children));
    }

    private (Tree Minimum, Forest? Remainder) GetMinimum(Forest forest)
    {
        if (forest.Tail is null)
            return (forest.Head, null);
        var (minimum, remainder) = GetMinimum(forest.Tail);
        return LessOrEqual(forest.Head.Value, minimum.Value)
            ? (forest.Head, forest.Tail)
            : (minimum, new Forest(forest.Head, remainder));
    }

    private (Forest? Zeros, Forest? Trees, Forest? Forest) SplitForest(
        int rank,
        Forest? zeros,
        Forest? trees,
        Forest? forest)
    {
        if (rank == 0)
            return (zeros, trees, forest);
        if (forest is null)
            throw new InvalidOperationException("Invalid skew-binomial forest invariant.");
        if (rank == 1 && forest.Tail is null)
            return (zeros, new Forest(forest.Head, trees), null);
        if (forest.Tail is null)
            throw new InvalidOperationException("Invalid skew-binomial forest invariant.");

        var first = forest.Head;
        var second = forest.Tail.Head;
        var rest = forest.Tail.Tail;
        if (rank == 1)
        {
            return second.Rank == 0
                ? (new Forest(first, zeros), new Forest(second, trees), rest)
                : (zeros, new Forest(first, trees), new Forest(second, rest));
        }
        if (first.Rank == second.Rank)
            return (zeros, new Forest(first, new Forest(second, trees)), rest);
        return first.Rank == 0
            ? SplitForest(rank - 1, new Forest(first, zeros), new Forest(second, trees), rest)
            : SplitForest(rank - 1, zeros, new Forest(first, trees), new Forest(second, rest));
    }

    private Forest? SkewMeld(Forest? left, Forest? right) =>
        UnionUnique(Uniquify(left), Uniquify(right));

    private Forest? Uniquify(Forest? forest) =>
        forest is null ? null : InsertRanked(forest.Head, Uniquify(forest.Tail));

    private Forest InsertRanked(Tree tree, Forest? forest)
    {
        if (forest is null)
            return new Forest(tree, null);
        if (tree.Rank < forest.Head.Rank)
            return new Forest(tree, forest);
        return InsertRanked(Link(tree, forest.Head), forest.Tail);
    }

    private Forest? UnionUnique(Forest? left, Forest? right)
    {
        if (left is null)
            return right;
        if (right is null)
            return left;
        if (left.Head.Rank < right.Head.Rank)
            return new Forest(left.Head, UnionUnique(left.Tail, right));
        if (left.Head.Rank > right.Head.Rank)
            return new Forest(right.Head, UnionUnique(left, right.Tail));
        return InsertRanked(Link(left.Head, right.Head), UnionUnique(left.Tail, right.Tail));
    }

    private static Tree[] ToReverseArray(Forest? forest)
    {
        var trees = new List<Tree>();
        for (; forest is not null; forest = forest.Tail)
            trees.Add(forest.Head);
        trees.Reverse();
        return [.. trees];
    }

    private static Forest? ValidateFusedChildren(Tree tree)
    {
        var rank = tree.Rank;
        var forest = tree.Children;
        while (rank > 0)
        {
            if (forest is null)
                throw new InvalidOperationException("A ranked Brodal-Okasaki tree is missing structural children.");

            var first = forest.Head;
            if (rank == 1)
            {
                if (first.Rank != 0)
                    throw new InvalidOperationException("A rank-one Brodal-Okasaki tree must begin with a rank-zero child.");
                if (forest.Tail is null)
                    return null;

                // The fused representation stores primitive children c followed by the embedded
                // heap forest f. When f also starts at rank zero, the paper permits that zero to
                // be decoded as the second primitive child; SplitForest makes the same choice.
                return forest.Tail.Head.Rank == 0 ? forest.Tail.Tail : forest.Tail;
            }
            if (forest.Tail is null)
                throw new InvalidOperationException("A ranked Brodal-Okasaki tree has an incomplete child encoding.");

            var second = forest.Tail.Head;
            if (first.Rank == second.Rank)
            {
                if (first.Rank != rank - 1)
                    throw new InvalidOperationException("A skew-linked Brodal-Okasaki tree has invalid child ranks.");
                return forest.Tail.Tail;
            }

            if (first.Rank == 0)
            {
                if (second.Rank != rank - 1)
                    throw new InvalidOperationException("A skew-linked Brodal-Okasaki tree has an invalid ranked child.");
                forest = forest.Tail.Tail;
            }
            else
            {
                if (first.Rank != rank - 1)
                    throw new InvalidOperationException("A linked Brodal-Okasaki tree has an invalid ranked child.");
                forest = forest.Tail;
            }
            rank--;
        }
        return forest;
    }

    private static int ValidateSkewForest(Forest? forest)
    {
        var length = 0;
        var previousRank = -1;
        var duplicate = false;
        for (; forest is not null; forest = forest.Tail)
        {
            var rank = forest.Head.Rank;
            if (rank < 0)
                throw new InvalidOperationException("A Brodal-Okasaki forest contains a negative rank.");
            if (rank < previousRank)
                throw new InvalidOperationException("Brodal-Okasaki root-forest ranks are not nondecreasing.");
            if (rank == previousRank)
            {
                if (length != 1 || duplicate)
                    throw new InvalidOperationException("Only the first two skew-forest ranks may be equal.");
                duplicate = true;
            }
            previousRank = rank;
            length++;
        }
        return length;
    }

    private readonly record struct TreeFrame(Tree Tree, int Depth);

    private sealed class Tree(int rank, T value, Forest? children)
    {
        internal int Rank { get; } = rank;
        internal T Value { get; } = value;
        internal Forest? Children { get; } = children;
    }

    private sealed class Forest(Tree head, Forest? tail)
    {
        internal Tree Head { get; } = head;
        internal Forest? Tail { get; } = tail;
    }
}

/// <summary>Describes a validated bootstrapped skew-binomial heap representation.</summary>
/// <param name="Count">The logical element count.</param>
/// <param name="RootForestLength">The number of trees directly below the global minimum root.</param>
/// <param name="MaximumRank">The largest skew-binomial rank.</param>
/// <param name="MaximumDepth">The deepest global-root-to-tree path.</param>
public readonly record struct BrodalOkasakiHeapStatistics(
    int Count,
    int RootForestLength,
    int MaximumRank,
    int MaximumDepth);
