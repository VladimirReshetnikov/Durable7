using System.Diagnostics;

namespace Durable7.FingerTree;

/// <summary>
/// Defines the append-only incremental level-ancestor service shared by the ancestry-backed
/// collections, including <see cref="AncestralSliceQueue{T}"/> and
/// <see cref="BilateralAncestralDeque{T}"/>.
/// </summary>
/// <typeparam name="T">The value stored in each non-bottom node.</typeparam>
/// <remarks>
/// <para>
/// An incremental level-ancestor arena is a monotone forest that grows one leaf at a time and
/// answers "which ancestor of this node sits at absolute depth d?". Node handles are stable
/// integers owned by one arena. The distinguished <see cref="Bottom"/> node has depth -1 and no
/// value; every labeled node has a depth of zero or more. A leaf may be added below any previously
/// published node, not only below the most recently added node, so old consumer versions can grow
/// into independent branches.
/// </para>
/// <para>
/// Implementations must guarantee that a successful <see cref="AddLeaf"/> publishes a fresh,
/// never-recycled handle whose immutable parent is the supplied handle and whose depth is exactly
/// one greater; that <see cref="GetDepth"/>, <see cref="GetParent"/>, <see cref="GetValue"/>, and
/// ancestor answers for a published node never change; and that a failed addition publishes no
/// partially initialized node.
/// </para>
/// <para>
/// <see cref="GetDepth"/>, <see cref="GetParent"/>, and <see cref="GetValue"/> must take O(1) time:
/// the parameterized bounds documented by the consuming collections assume those primitive reads
/// are constant-time. Implementations determine the bounds those collections inherit. Let U be
/// leaf-add time and Q be level-ancestor query time after M published nodes. An implementation with
/// U = Q = O(1) worst case gives the consumers their optimal bound; neither logarithmic binary
/// lifting nor the shipped <see cref="MyersIncrementalAncestorArena{T}"/> satisfies that stronger
/// backend bound.
/// </para>
/// <para>
/// Integer handles are arena-relative capabilities: passing a handle obtained from a different
/// arena violates this interface's precondition even if the same integer happens to be valid
/// locally, because range validation cannot identify an in-range integer copied from another arena.
/// Callers must preserve arena ownership themselves.
/// </para>
/// </remarks>
public interface IIncrementalAncestorArena<T>
{
    /// <summary>Gets the distinguished unlabeled node at depth -1.</summary>
    int Bottom { get; }

    /// <summary>Gets the number of labeled nodes published by this arena.</summary>
    int PublishedNodeCount { get; }

    /// <summary>Adds a labeled leaf below a previously published node.</summary>
    /// <param name="parent">The bottom node or a labeled node owned by this arena.</param>
    /// <param name="value">The new node's value.</param>
    /// <returns>A stable handle for the new leaf.</returns>
    int AddLeaf(int parent, T value);

    /// <summary>Gets a node's immutable absolute depth; the bottom node has depth -1.</summary>
    /// <param name="node">A node owned by this arena.</param>
    /// <returns>The node's depth.</returns>
    int GetDepth(int node);

    /// <summary>Gets a labeled node's parent, or bottom when the node has depth zero.</summary>
    /// <param name="node">A labeled node owned by this arena.</param>
    /// <returns>The parent handle.</returns>
    int GetParent(int node);

    /// <summary>Gets the unique ancestor of a node at an absolute depth.</summary>
    /// <param name="node">A node owned by this arena.</param>
    /// <param name="depth">A depth from -1 through the node's own depth.</param>
    /// <returns>The ancestor handle.</returns>
    int AncestorAtDepth(int node, int depth);

    /// <summary>Gets the immutable value associated with a labeled node.</summary>
    /// <param name="node">A non-bottom node owned by this arena.</param>
    /// <returns>The node value.</returns>
    T GetValue(int node);
}

/// <summary>
/// Implements an incremental ancestor arena with the two-link applicative-stack scheme of Myers.
/// </summary>
/// <typeparam name="T">The value stored in each non-bottom node.</typeparam>
/// <remarks>
/// <para>
/// Each immutable node keeps one parent link and one coalesced jump link. Adding a leaf performs
/// O(1) link work. Nodes are retained in blocks of sizes 1, 3, 5, ..., whose square boundaries
/// provide O(1) addressing and O(sqrt(M)) unused slots after M published nodes. Managed block and
/// directory allocation makes insertion O(1) amortized rather than worst-case: a single addition at a square boundary pays Θ(√M) to allocate the next odd block,
/// the accepted contract of the shared odd-block representation across every port (deamortizing it
/// via real-time table migration was considered and declined; the Haskell port's arena-free design
/// exceeds this profile at O(1) worst case). An ancestor query
/// follows O(log M) parent/jump links in the worst case. Total arena storage is O(M), and every
/// published value remains reachable until the arena is collected.
/// </para>
/// <para>
/// Operations are serialized by one private lock. Published nodes are immutable, so retained
/// collection handles remain semantically stable; this implementation makes no lock-free progress
/// guarantee. Handle validation is range-based and cannot identify an in-range integer copied from
/// another arena; callers using the backend directly must preserve arena ownership.
/// </para>
/// </remarks>
public sealed class MyersIncrementalAncestorArena<T> : IIncrementalAncestorArena<T>
{
    private readonly object _gate = new();
    private readonly OddBlockStore<Node> _nodes = new();
    private long _addLeafCount;
    private long _ancestorQueryCount;
    private long _totalAncestorHopCount;
    private int _lastAncestorHopCount;
    private int _maximumAncestorHopCount;

    /// <summary>Initializes an empty arena containing only its unlabeled bottom node.</summary>
    public MyersIncrementalAncestorArena()
    {
        var bottom = _nodes.Append(new Node(
            Value: default!,
            Parent: 0,
            Depth: -1,
            Skip: 0,
            SkipDistance: 0));
        Debug.Assert(bottom == Bottom);
    }

    /// <inheritdoc />
    /// <remarks>This read is O(1).</remarks>
    public int Bottom => 0;

    /// <inheritdoc />
    /// <remarks>This read is O(1).</remarks>
    public int PublishedNodeCount
    {
        get
        {
            lock (_gate)
                return _nodes.Count - 1;
        }
    }

    /// <inheritdoc />
    /// <remarks>This addition is O(1) amortized over the block and directory allocations; a
    /// block-boundary call pays Θ(√M) for the new odd block.</remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="parent"/> was never published by this arena.
    /// </exception>
    /// <exception cref="OverflowException">
    /// The ancestry depth, the node count, or a coalesced jump distance cannot grow further.
    /// </exception>
    public int AddLeaf(int parent, T value)
    {
        lock (_gate)
        {
            var parentNode = GetNode(parent);
            if (parentNode.Depth == int.MaxValue)
                throw new OverflowException("The ancestry depth exceeds Int32.MaxValue.");

            int skip;
            int skipDistance;
            if (parent == Bottom || parentNode.Skip == Bottom)
            {
                skip = parent;
                skipDistance = 1;
            }
            else
            {
                var skippedNode = _nodes[parentNode.Skip];
                if (skippedNode.SkipDistance <= parentNode.SkipDistance)
                {
                    skip = skippedNode.Skip;
                    skipDistance = checked(1 + parentNode.SkipDistance + skippedNode.SkipDistance);
                }
                else
                {
                    skip = parent;
                    skipDistance = 1;
                }
            }

            var node = new Node(
                value,
                parent,
                parentNode.Depth + 1,
                skip,
                skipDistance);
            var handle = _nodes.Append(node);
            _addLeafCount++;
            return handle;
        }
    }

    /// <inheritdoc />
    /// <remarks>This read is O(1).</remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="node"/> was never published by this arena.
    /// </exception>
    public int GetDepth(int node)
    {
        lock (_gate)
            return GetNode(node).Depth;
    }

    /// <inheritdoc />
    /// <remarks>This read is O(1).</remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="node"/> is the bottom node, which has no parent, or was never published by
    /// this arena.
    /// </exception>
    public int GetParent(int node)
    {
        lock (_gate)
        {
            if (node == Bottom)
                throw new ArgumentOutOfRangeException(nameof(node), "The bottom node has no parent.");
            return GetNode(node).Parent;
        }
    }

    /// <inheritdoc />
    /// <remarks>
    /// This query follows O(log M) parent/jump links in the worst case after M published nodes.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="node"/> was never published by this arena, or <paramref name="depth"/> is
    /// outside -1 through the node's own depth.
    /// </exception>
    public int AncestorAtDepth(int node, int depth)
    {
        lock (_gate)
        {
            var current = GetNode(node);
            if (depth < -1 || depth > current.Depth)
                throw new ArgumentOutOfRangeException(nameof(depth));

            var hops = 0;
            while (current.Depth > depth)
            {
                var remaining = current.Depth - depth;
                if (current.SkipDistance > 0 && current.SkipDistance <= remaining)
                    node = current.Skip;
                else
                    node = current.Parent;

                current = _nodes[node];
                hops++;
            }

            if (_ancestorQueryCount != long.MaxValue)
                _ancestorQueryCount++;
            _lastAncestorHopCount = hops;
            _totalAncestorHopCount = SaturatingAdd(_totalAncestorHopCount, hops);
            if (hops > _maximumAncestorHopCount)
                _maximumAncestorHopCount = hops;
            return node;
        }
    }

    /// <inheritdoc />
    /// <remarks>This read is O(1).</remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="node"/> is the bottom node, which has no value, or was never published by
    /// this arena.
    /// </exception>
    public T GetValue(int node)
    {
        lock (_gate)
        {
            if (node == Bottom)
                throw new ArgumentOutOfRangeException(nameof(node), "The bottom node has no value.");
            return GetNode(node).Value;
        }
    }

    /// <summary>Returns deterministic representation and traversal counters in O(1) time.</summary>
    /// <returns>A snapshot of the arena statistics.</returns>
    public MyersIncrementalAncestorStatistics GetStatistics()
    {
        lock (_gate)
        {
            return new MyersIncrementalAncestorStatistics(
                _nodes.Count - 1,
                _nodes.BlockCount,
                _nodes.AllocatedSlotCount,
                _addLeafCount,
                _ancestorQueryCount,
                _lastAncestorHopCount,
                _maximumAncestorHopCount,
                _totalAncestorHopCount);
        }
    }

    private Node GetNode(int node)
    {
        if ((uint)node >= (uint)_nodes.Count)
            throw new ArgumentOutOfRangeException(nameof(node));
        return _nodes[node];
    }

    private static long SaturatingAdd(long value, int increment) =>
        value > long.MaxValue - increment ? long.MaxValue : value + increment;

    private readonly record struct Node(
        T Value,
        int Parent,
        int Depth,
        int Skip,
        int SkipDistance);

    private sealed class OddBlockStore<TItem>
    {
        private readonly List<TItem[]> _blocks = [];

        internal int Count { get; private set; }

        internal int BlockCount => _blocks.Count;

        internal long AllocatedSlotCount { get; private set; }

        internal TItem this[int index]
        {
            get
            {
                if ((uint)index >= (uint)Count)
                    throw new ArgumentOutOfRangeException(nameof(index));
                var block = IntegerSquareRoot(index);
                return _blocks[block][index - block * block];
            }
        }

        internal int Append(TItem item)
        {
            if (Count == int.MaxValue)
                throw new OverflowException("The arena contains Int32.MaxValue nodes.");

            var index = Count;
            var block = IntegerSquareRoot(index);
            if (block == _blocks.Count)
            {
                var blockLength = checked(2 * block + 1);
                _blocks.Add(new TItem[blockLength]);
                AllocatedSlotCount += blockLength;
            }

            _blocks[block][index - block * block] = item;
            Count = index + 1;
            return index;
        }

        private static int IntegerSquareRoot(int value)
        {
            var root = (int)Math.Sqrt(value);
            while ((long)(root + 1) * (root + 1) <= value)
                root++;
            while ((long)root * root > value)
                root--;
            return root;
        }
    }
}

/// <summary>Describes one snapshot of a Myers incremental-ancestor arena.</summary>
/// <param name="PublishedNodeCount">The number of labeled nodes added to the arena.</param>
/// <param name="BlockCount">The number of allocated odd-sized blocks, including the bottom block.</param>
/// <param name="AllocatedSlotCount">The total slots in the odd-sized blocks.</param>
/// <param name="AddLeafCount">The number of successful leaf additions.</param>
/// <param name="AncestorQueryCount">
/// The number of completed ancestor queries, saturated at <see cref="Int64.MaxValue"/>.
/// </param>
/// <param name="LastAncestorHopCount">The parent/jump hops made by the most recent query.</param>
/// <param name="MaximumAncestorHopCount">The greatest parent/jump hop count observed.</param>
/// <param name="TotalAncestorHopCount">
/// The total parent/jump hops across all queries, saturated at <see cref="Int64.MaxValue"/>.
/// </param>
public readonly record struct MyersIncrementalAncestorStatistics(
    int PublishedNodeCount,
    int BlockCount,
    long AllocatedSlotCount,
    long AddLeafCount,
    long AncestorQueryCount,
    int LastAncestorHopCount,
    int MaximumAncestorHopCount,
    long TotalAncestorHopCount);
