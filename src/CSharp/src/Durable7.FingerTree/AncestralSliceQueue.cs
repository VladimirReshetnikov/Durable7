using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree.Experimental;

/// <summary>
/// Defines the append-only ancestry service used by <see cref="AncestralSliceQueue{T}"/>.
/// </summary>
/// <typeparam name="T">The value stored in each non-bottom node.</typeparam>
/// <remarks>
/// Node handles are stable integers owned by one arena. The distinguished <see cref="Bottom"/>
/// node has depth -1 and no value. A leaf may be added below any previously published node, not
/// only below the most recently added node. Implementations define the time bounds inherited by
/// <see cref="AncestralSliceQueue{T}"/>; in particular, logarithmic binary lifting or Myers links
/// do not provide the optimal constant-time level-ancestor bound.
/// Integer handles are arena-relative: passing a handle obtained from a different arena violates
/// this interface's precondition even if the same integer happens to be valid locally.
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
/// Adding a leaf performs O(1) link work. Nodes are retained in blocks of sizes 1, 3, 5, ...,
/// whose square boundaries provide O(1) addressing and O(sqrt(M)) unused slots after M published
/// nodes. Managed block and directory allocation makes insertion O(1) amortized rather than
/// worst-case. An ancestor query follows O(log M) parent/jump links in the worst case. Total arena
/// storage is O(M), but every published value remains reachable until the arena is collected.
/// </para>
/// <para>
/// Operations are serialized by one private lock. Published nodes are immutable, so retained queue
/// handles remain semantically stable; this implementation makes no lock-free progress guarantee.
/// Handle validation is range-based and cannot identify an in-range integer copied from another
/// arena; callers using the backend directly must preserve arena ownership.
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
    public int Bottom => 0;

    /// <inheritdoc />
    public int PublishedNodeCount
    {
        get
        {
            lock (_gate)
                return _nodes.Count - 1;
        }
    }

    /// <inheritdoc />
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
    public int GetDepth(int node)
    {
        lock (_gate)
            return GetNode(node).Depth;
    }

    /// <inheritdoc />
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
    public T GetValue(int node)
    {
        lock (_gate)
        {
            if (node == Bottom)
                throw new ArgumentOutOfRangeException(nameof(node), "The bottom node has no value.");
            return GetNode(node).Value;
        }
    }

    /// <summary>Returns deterministic representation and traversal counters.</summary>
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

/// <summary>
/// Represents an immutable, fully persistent queue slice as an interval on an append ancestry path.
/// </summary>
/// <typeparam name="T">The element type.</typeparam>
/// <remarks>
/// <para>
/// A value stores one arena, a tail node, the absolute depth of its first visible element, and a
/// redundant count cache.
/// Empty values retain the node immediately before their window as an anchor, so appending to any
/// empty slice yields exactly the new value without exposing a discarded prefix. All handles are
/// immutable; adding below an old handle creates a sibling branch in the shared monotone arena.
/// </para>
/// <para>
/// Let U be the arena's leaf-add time and Q its level-ancestor query time. <see cref="AddLast"/>
/// costs U; <see cref="First"/>, the indexer, <see cref="Take"/>, <see cref="Slice"/>, and
/// nontrivial <see cref="SplitAt"/> cost Q; <see cref="Last"/>, <see cref="RemoveFirst"/>,
/// <see cref="RemoveLast"/>, and <see cref="Drop"/> are O(1). Alstrup--Holm incremental ancestry
/// gives U = Q = O(1) worst case with linear arena space. <see cref="CreateMyers"/> instead uses
/// O(1)-amortized addition and O(log M) ancestor queries after M historical appends.
/// </para>
/// <para>
/// The operation set intentionally excludes prepend, point replacement, arbitrary middle edits,
/// and concatenation of unrelated histories. Arena space is charged to all successful historical
/// appends, not only to the visible length of one handle.
/// </para>
/// </remarks>
[DebuggerDisplay("Count = {Count}, LowDepth = {_lowDepth}, Tail = {_tail}")]
public sealed class AncestralSliceQueue<T> : IReadOnlyList<T>
{
    private readonly IIncrementalAncestorArena<T> _arena;
    private readonly int _tail;
    private readonly int _lowDepth;

    private AncestralSliceQueue(
        IIncrementalAncestorArena<T> arena,
        int tail,
        int lowDepth,
        int count)
    {
        _arena = arena;
        _tail = tail;
        _lowDepth = lowDepth;
        Count = count;
    }

    /// <summary>Gets the number of visible values.</summary>
    public int Count { get; }

    /// <summary>Gets whether this handle denotes an empty ancestry interval.</summary>
    public bool IsEmpty => Count == 0;

    /// <summary>Gets the first visible value.</summary>
    /// <exception cref="InvalidOperationException">The queue is empty.</exception>
    public T First
    {
        get
        {
            EnsureNotEmpty();
            var node = Count == 1 ? _tail : _arena.AncestorAtDepth(_tail, _lowDepth);
            return _arena.GetValue(node);
        }
    }

    /// <summary>Gets the last visible value.</summary>
    /// <exception cref="InvalidOperationException">The queue is empty.</exception>
    public T Last
    {
        get
        {
            EnsureNotEmpty();
            return _arena.GetValue(_tail);
        }
    }

    /// <summary>Gets the visible value at a zero-based index.</summary>
    /// <param name="index">The zero-based visible index.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the queue.</exception>
    public T this[int index]
    {
        get
        {
            if ((uint)index >= (uint)Count)
                throw new ArgumentOutOfRangeException(nameof(index));
            var node = index == Count - 1
                ? _tail
                : _arena.AncestorAtDepth(_tail, checked(_lowDepth + index));
            return _arena.GetValue(node);
        }
    }

    /// <summary>Creates an empty queue owned by an explicit incremental-ancestor arena.</summary>
    /// <param name="arena">The manager that will retain and navigate every appended node.</param>
    /// <returns>An empty anchored queue.</returns>
    /// <exception cref="ArgumentException">The arena's bottom node is not at depth -1.</exception>
    public static AncestralSliceQueue<T> Create(IIncrementalAncestorArena<T> arena)
    {
        ArgumentNullException.ThrowIfNull(arena);
        var bottom = arena.Bottom;
        if (arena.GetDepth(bottom) != -1)
            throw new ArgumentException("The arena bottom node must have depth -1.", nameof(arena));
        return new AncestralSliceQueue<T>(arena, bottom, lowDepth: 0, count: 0);
    }

    /// <summary>Creates an empty queue backed by the shipped Myers jump-link arena.</summary>
    /// <returns>An empty queue with a fresh, independently collectible arena.</returns>
    public static AncestralSliceQueue<T> CreateMyers() => Create(new MyersIncrementalAncestorArena<T>());

    /// <summary>Creates a Myers-backed queue from values in enumeration order.</summary>
    /// <param name="values">The values to append.</param>
    /// <returns>A queue containing the values.</returns>
    public static AncestralSliceQueue<T> CreateRange(IEnumerable<T> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        var result = CreateMyers();
        foreach (var value in values)
            result = result.AddLast(value);
        return result;
    }

    /// <summary>Returns a queue with one value appended to this handle's current tail or empty anchor.</summary>
    /// <param name="value">The value to append.</param>
    /// <returns>The extended persistent branch.</returns>
    /// <exception cref="OverflowException">The visible count or ancestry depth cannot grow further.</exception>
    public AncestralSliceQueue<T> AddLast(T value)
    {
        if (Count == int.MaxValue)
            throw new OverflowException("The queue count exceeds Int32.MaxValue.");
        var tail = _arena.AddLeaf(_tail, value);
        return new AncestralSliceQueue<T>(_arena, tail, _lowDepth, Count + 1);
    }

    /// <summary>Returns the queue without its first visible value.</summary>
    /// <returns>The remaining ancestry interval.</returns>
    /// <exception cref="InvalidOperationException">The queue is empty.</exception>
    public AncestralSliceQueue<T> RemoveFirst()
    {
        EnsureNotEmpty();
        return new AncestralSliceQueue<T>(_arena, _tail, checked(_lowDepth + 1), Count - 1);
    }

    /// <summary>Returns the queue without its last visible value.</summary>
    /// <returns>The remaining ancestry interval.</returns>
    /// <exception cref="InvalidOperationException">The queue is empty.</exception>
    public AncestralSliceQueue<T> RemoveLast()
    {
        EnsureNotEmpty();
        return new AncestralSliceQueue<T>(_arena, _arena.GetParent(_tail), _lowDepth, Count - 1);
    }

    /// <summary>Attempts to remove and return the first visible value.</summary>
    /// <param name="value">The first value on success.</param>
    /// <param name="result">The remaining queue on success; otherwise, this queue.</param>
    /// <returns><see langword="true"/> when a value was removed.</returns>
    public bool TryRemoveFirst(
        [MaybeNullWhen(false)] out T value,
        out AncestralSliceQueue<T> result)
    {
        if (IsEmpty)
        {
            value = default;
            result = this;
            return false;
        }

        value = First;
        result = RemoveFirst();
        return true;
    }

    /// <summary>Attempts to remove and return the last visible value.</summary>
    /// <param name="value">The last value on success.</param>
    /// <param name="result">The remaining queue on success; otherwise, this queue.</param>
    /// <returns><see langword="true"/> when a value was removed.</returns>
    public bool TryRemoveLast(
        [MaybeNullWhen(false)] out T value,
        out AncestralSliceQueue<T> result)
    {
        if (IsEmpty)
        {
            value = default;
            result = this;
            return false;
        }

        value = Last;
        result = RemoveLast();
        return true;
    }

    /// <summary>Returns the first <paramref name="count"/> visible values.</summary>
    /// <param name="count">The prefix length.</param>
    /// <returns>An appendable ancestry prefix.</returns>
    public AncestralSliceQueue<T> Take(int count)
    {
        ValidateBoundary(count, nameof(count));
        return count == Count ? this : Slice(0, count);
    }

    /// <summary>Returns the queue after omitting its first <paramref name="count"/> visible values.</summary>
    /// <param name="count">The number of values to omit.</param>
    /// <returns>An appendable ancestry suffix.</returns>
    public AncestralSliceQueue<T> Drop(int count)
    {
        ValidateBoundary(count, nameof(count));
        if (count == 0)
            return this;
        return new AncestralSliceQueue<T>(
            _arena,
            _tail,
            checked(_lowDepth + count),
            Count - count);
    }

    /// <summary>Returns one contiguous, appendable ancestry slice.</summary>
    /// <param name="index">The zero-based first visible index.</param>
    /// <param name="count">The number of visible values in the result.</param>
    /// <returns>The selected ancestry interval.</returns>
    public AncestralSliceQueue<T> Slice(int index, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (index > Count)
            throw new ArgumentOutOfRangeException(nameof(index));
        if (index > Count - count)
            throw new ArgumentOutOfRangeException(nameof(count));
        if (index == 0 && count == Count)
            return this;

        var lowDepth = checked(_lowDepth + index);
        var targetDepth = checked(lowDepth + count - 1);
        var currentTailDepth = checked(_lowDepth + Count - 1);
        var tail = targetDepth == currentTailDepth
            ? _tail
            : _arena.AncestorAtDepth(_tail, targetDepth);
        return new AncestralSliceQueue<T>(_arena, tail, lowDepth, count);
    }

    /// <summary>Splits this queue at a zero-based boundary.</summary>
    /// <param name="index">The number of visible values placed in the left result.</param>
    /// <returns>The appendable prefix and suffix.</returns>
    public (AncestralSliceQueue<T> Left, AncestralSliceQueue<T> Right) SplitAt(int index)
    {
        ValidateBoundary(index, nameof(index));
        return (Take(index), Drop(index));
    }

    /// <summary>Returns a stable front-to-back enumerator over this immutable ancestry interval.</summary>
    /// <returns>An enumerator.</returns>
    /// <remarks>
    /// The shipped implementation follows parent links into a temporary array, taking Theta(Count)
    /// time and Theta(Count) transient element slots. Concurrent appends cannot change the captured path.
    /// </remarks>
    public IEnumerator<T> GetEnumerator() => Enumerate().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private IEnumerable<T> Enumerate()
    {
        if (Count == 0)
            yield break;

        var values = new T[Count];
        var node = _tail;
        for (var index = Count - 1; index >= 0; index--)
        {
            values[index] = _arena.GetValue(node);
            if (index != 0)
                node = _arena.GetParent(node);
        }

        foreach (var value in values)
            yield return value;
    }

    private void ValidateBoundary(int index, string parameterName)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index, parameterName);
        if (index > Count)
            throw new ArgumentOutOfRangeException(parameterName);
    }

    private void EnsureNotEmpty()
    {
        if (IsEmpty)
            throw new InvalidOperationException("The ancestral slice queue is empty.");
    }
}
