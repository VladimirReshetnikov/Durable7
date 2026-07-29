using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree.Experimental;

/// <summary>
/// Defines the append-only level-ancestor service used by
/// <see cref="BilateralAncestralDeque{T}"/>.
/// </summary>
/// <typeparam name="T">The value stored in each non-bottom node.</typeparam>
/// <remarks>
/// <para>
/// Node handles are stable integers owned by one arena. The distinguished <see cref="Bottom"/>
/// node has depth -1 and no value. A leaf may be added below any previously published node, so
/// old deque versions can grow into independent branches. A successful <see cref="AddLeaf"/> must
/// publish a fresh, never-recycled handle whose immutable parent is the supplied handle and whose
/// depth is exactly one greater. Parent, depth, value, and ancestor answers for a published node
/// must never change. A failed addition must not publish a partially initialized node.
/// <see cref="GetDepth"/>, <see cref="GetParent"/>, and <see cref="GetValue"/> must take O(1)
/// time; the deque's parameterized bounds assume those primitive reads are constant-time.
/// </para>
/// <para>
/// Arena implementations determine the bounds inherited by the deque. An incremental
/// level-ancestor implementation with O(1) worst-case <see cref="AddLeaf"/> and
/// <see cref="AncestorAtDepth"/> gives the deque its optimal bound. Binary lifting and the
/// shipped Myers arena do not satisfy that stronger backend bound.
/// Integer handles are arena-relative; passing a handle from another arena violates the contract.
/// </para>
/// </remarks>
public interface IIncrementalLevelAncestorArena<T>
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

    /// <summary>Gets a labeled node's parent, or bottom for a depth-zero node.</summary>
    /// <param name="node">A labeled node owned by this arena.</param>
    /// <returns>The parent handle.</returns>
    int GetParent(int node);

    /// <summary>Gets the unique ancestor of a node at an absolute depth.</summary>
    /// <param name="node">A node owned by this arena.</param>
    /// <param name="depth">A depth from -1 through the node's own depth.</param>
    /// <returns>The ancestor handle.</returns>
    int AncestorAtDepth(int node, int depth);

    /// <summary>Gets the value stored in a labeled node's immutable slot.</summary>
    /// <param name="node">A non-bottom node owned by this arena.</param>
    /// <returns>The node value.</returns>
    T GetValue(int node);
}

/// <summary>
/// Implements an incremental level-ancestor arena with Myers's two-link applicative-stack scheme.
/// </summary>
/// <typeparam name="T">The value stored in each non-bottom node.</typeparam>
/// <remarks>
/// <para>
/// Adding a leaf performs O(1) link work. Nodes live in blocks of sizes 1, 3, 5, ...; square
/// boundaries give O(1) addressing and O(sqrt(M)) unused slots after M published nodes. Managed
/// block and directory allocation makes addition O(1) amortized rather than worst case. An
/// ancestor query follows O(log M) parent/jump links in the worst case. Total arena storage is
/// O(M), and every published value remains reachable until the arena is collected.
/// </para>
/// <para>
/// Operations are serialized by one private lock. Published nodes are immutable, so retained
/// deque handles remain semantically stable, but this implementation makes no lock-free progress
/// guarantee. Range validation cannot identify an in-range integer copied from another arena;
/// direct backend callers must preserve arena ownership.
/// </para>
/// </remarks>
public sealed class MyersLevelAncestorArena<T> : IIncrementalLevelAncestorArena<T>
{
    private readonly object _gate = new();
    private readonly OddBlockStore<Node> _nodes = new();
    private long _addLeafCount;
    private long _ancestorQueryCount;
    private long _totalAncestorHopCount;
    private int _lastAncestorHopCount;
    private int _maximumAncestorHopCount;

    /// <summary>Initializes an empty arena containing only its unlabeled bottom node.</summary>
    public MyersLevelAncestorArena()
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
    public MyersLevelAncestorStatistics GetStatistics()
    {
        lock (_gate)
        {
            return new MyersLevelAncestorStatistics(
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

/// <summary>Describes one snapshot of a Myers level-ancestor arena.</summary>
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
public readonly record struct MyersLevelAncestorStatistics(
    int PublishedNodeCount,
    int BlockCount,
    long AllocatedSlotCount,
    long AddLeafCount,
    long AncestorQueryCount,
    int LastAncestorHopCount,
    int MaximumAncestorHopCount,
    long TotalAncestorHopCount);

/// <summary>
/// Represents an immutable deque as two oppositely oriented intervals in an append-only ancestry
/// arena.
/// </summary>
/// <typeparam name="T">The element type.</typeparam>
/// <remarks>
/// <para>
/// Each handle stores a left interval (l-a, l-t] and a right interval (r-a, r-t]. Its logical
/// value is reverse((l-a, l-t]) followed by (r-a, r-t]. A front insertion appends a leaf to the
/// left tail; a back insertion appends one to the right tail. Reversal only exchanges the two
/// intervals. Every contiguous slice intersects each interval at most once, so it is again a
/// bilateral handle rather than a rebuilt tree.
/// </para>
/// <para>
/// Let U be arena leaf-add time and Q be arena level-ancestor query time. End insertion costs U;
/// removal costs O(1) on its own nonempty side and at most one Q when it crosses the center;
/// indexing costs at most one Q; slicing and splitting each cost at most two Q.
/// Endpoint reads, count, clear, and reverse are O(1). An Alstrup--Holm incremental-level-ancestor
/// backend makes all of those operations O(1) worst case with linear arena space. The shipped
/// <see cref="MyersLevelAncestorArena{T}"/> instead gives O(1)-amortized insertion and O(log M)
/// ancestor queries after M historical insertions.
/// </para>
/// <para>
/// The restricted algebra intentionally excludes arbitrary concatenation, point replacement, and
/// middle editing. Arena space is charged to all successful historical end insertions, not merely
/// to the values visible in one handle. Enumeration is Theta(Count) time and may retain
/// O(Count) temporary values. Handles are immutable; progress guarantees come from the arena.
/// </para>
/// </remarks>
[DebuggerDisplay("Count = {Count}, Left = {_left.Count}, Right = {_right.Count}")]
public sealed class BilateralAncestralDeque<T> : IReadOnlyList<T>
{
    private readonly IIncrementalLevelAncestorArena<T> _arena;
    private readonly Segment _left;
    private readonly Segment _right;

    private BilateralAncestralDeque(
        IIncrementalLevelAncestorArena<T> arena,
        Segment left,
        Segment right,
        int count)
    {
        _arena = arena;
        _left = left;
        _right = right;
        Count = count;
    }

    /// <summary>Gets the number of visible values.</summary>
    public int Count { get; }

    /// <summary>Gets whether this handle denotes an empty deque.</summary>
    public bool IsEmpty => Count == 0;

    /// <summary>Gets the first visible value in O(1) arena operations.</summary>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public T First
    {
        get
        {
            EnsureNotEmpty();
            return _arena.GetValue(_left.Count != 0 ? _left.Tail : _right.Base);
        }
    }

    /// <summary>Gets the last visible value in O(1) arena operations.</summary>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public T Last
    {
        get
        {
            EnsureNotEmpty();
            return _arena.GetValue(_right.Count != 0 ? _right.Tail : _left.Base);
        }
    }

    /// <summary>Gets a visible value with at most one level-ancestor query.</summary>
    /// <param name="index">The zero-based visible index.</param>
    /// <returns>The value at <paramref name="index"/>.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the deque.</exception>
    public T this[int index]
    {
        get
        {
            if ((uint)index >= (uint)Count)
                throw IndexError(index);

            int node;
            if (index < _left.Count)
            {
                if (index == 0)
                    node = _left.Tail;
                else if (index == _left.Count - 1)
                    node = _left.Base;
                else
                    node = _arena.AncestorAtDepth(_left.Tail, _arena.GetDepth(_left.Tail) - index);
            }
            else
            {
                var rightIndex = index - _left.Count;
                if (rightIndex == 0)
                    node = _right.Base;
                else if (rightIndex == _right.Count - 1)
                    node = _right.Tail;
                else
                    node = _arena.AncestorAtDepth(
                        _right.Tail,
                        checked(_arena.GetDepth(_right.Anchor) + rightIndex + 1));
            }

            return _arena.GetValue(node);
        }
    }

    /// <summary>Creates an empty deque owned by an explicit level-ancestor arena.</summary>
    /// <param name="arena">The manager that retains and navigates every inserted node.</param>
    /// <returns>An empty bilateral handle.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="arena"/> is null.</exception>
    /// <exception cref="ArgumentException">The arena's bottom node is not at depth -1.</exception>
    public static BilateralAncestralDeque<T> Create(IIncrementalLevelAncestorArena<T> arena)
    {
        ArgumentNullException.ThrowIfNull(arena);
        var bottom = arena.Bottom;
        if (arena.GetDepth(bottom) != -1)
            throw new ArgumentException("The arena bottom node must have depth -1.", nameof(arena));
        var empty = Segment.EmptyAt(bottom);
        return new BilateralAncestralDeque<T>(arena, empty, empty, 0);
    }

    /// <summary>Creates an empty deque backed by the shipped Myers jump-link arena.</summary>
    /// <returns>An empty deque with a fresh, independently collectible arena.</returns>
    public static BilateralAncestralDeque<T> CreateMyers() => Create(new MyersLevelAncestorArena<T>());

    /// <summary>Creates a Myers-backed deque from values in enumeration order.</summary>
    /// <param name="values">The values to append.</param>
    /// <returns>A deque containing the values.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="values"/> is null.</exception>
    public static BilateralAncestralDeque<T> CreateRange(IEnumerable<T> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        var result = CreateMyers();
        foreach (var value in values)
            result = result.AddLast(value);
        return result;
    }

    /// <summary>Returns a deque with one value added at the front.</summary>
    /// <param name="value">The value to prepend.</param>
    /// <returns>The extended persistent branch.</returns>
    /// <exception cref="OverflowException">The visible count or ancestry depth cannot grow further.</exception>
    public BilateralAncestralDeque<T> AddFirst(T value)
    {
        EnsureRoom();
        var left = AddToTail(_left, value);
        return new BilateralAncestralDeque<T>(_arena, left, _right, Count + 1);
    }

    /// <summary>Returns a deque with one value added at the back.</summary>
    /// <param name="value">The value to append.</param>
    /// <returns>The extended persistent branch.</returns>
    /// <exception cref="OverflowException">The visible count or ancestry depth cannot grow further.</exception>
    public BilateralAncestralDeque<T> AddLast(T value)
    {
        EnsureRoom();
        var right = AddToTail(_right, value);
        return new BilateralAncestralDeque<T>(_arena, _left, right, Count + 1);
    }

    /// <summary>
    /// Returns the deque without its first value, using at most one level-ancestor query.
    /// </summary>
    /// <returns>The remaining deque.</returns>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public BilateralAncestralDeque<T> RemoveFirst()
    {
        EnsureNotEmpty();
        if (Count == 1)
            return EmptyHandle();
        if (_left.Count != 0)
        {
            var left = RemoveTail(_left);
            return new BilateralAncestralDeque<T>(_arena, left, _right, Count - 1);
        }

        var right = RemoveBase(_right);
        return new BilateralAncestralDeque<T>(_arena, _left, right, Count - 1);
    }

    /// <summary>
    /// Returns the deque without its last value, using at most one level-ancestor query.
    /// </summary>
    /// <returns>The remaining deque.</returns>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public BilateralAncestralDeque<T> RemoveLast()
    {
        EnsureNotEmpty();
        if (Count == 1)
            return EmptyHandle();
        if (_right.Count != 0)
        {
            var right = RemoveTail(_right);
            return new BilateralAncestralDeque<T>(_arena, _left, right, Count - 1);
        }

        var left = RemoveBase(_left);
        return new BilateralAncestralDeque<T>(_arena, left, _right, Count - 1);
    }

    /// <summary>Attempts to remove and return the first value.</summary>
    /// <param name="value">The removed value on success; otherwise default.</param>
    /// <param name="result">The remaining deque on success; otherwise this deque.</param>
    /// <returns><see langword="true"/> when a value was removed.</returns>
    public bool TryRemoveFirst(
        [MaybeNullWhen(false)] out T value,
        out BilateralAncestralDeque<T> result)
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

    /// <summary>Attempts to remove and return the last value.</summary>
    /// <param name="value">The removed value on success; otherwise default.</param>
    /// <param name="result">The remaining deque on success; otherwise this deque.</param>
    /// <returns><see langword="true"/> when a value was removed.</returns>
    public bool TryRemoveLast(
        [MaybeNullWhen(false)] out T value,
        out BilateralAncestralDeque<T> result)
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

    /// <summary>Returns the first <paramref name="count"/> values.</summary>
    /// <param name="count">The prefix length.</param>
    /// <returns>A persistent bilateral slice.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="count"/> is outside zero through <see cref="Count"/>.
    /// </exception>
    public BilateralAncestralDeque<T> Take(int count)
    {
        ValidateBoundary(count, nameof(count));
        return count == Count ? this : Slice(0, count);
    }

    /// <summary>Returns the deque after omitting its first <paramref name="count"/> values.</summary>
    /// <param name="count">The number of values to omit.</param>
    /// <returns>A persistent bilateral slice.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="count"/> is outside zero through <see cref="Count"/>.
    /// </exception>
    public BilateralAncestralDeque<T> Drop(int count)
    {
        ValidateBoundary(count, nameof(count));
        return count == 0 ? this : Slice(count, Count - count);
    }

    /// <summary>Returns one contiguous slice using at most two level-ancestor queries.</summary>
    /// <param name="index">The zero-based first visible index.</param>
    /// <param name="count">The number of visible values in the result.</param>
    /// <returns>The selected bilateral ancestry intervals.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// The requested index/count pair is outside this deque.
    /// </exception>
    public BilateralAncestralDeque<T> Slice(int index, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (index > Count)
            throw new ArgumentOutOfRangeException(nameof(index));
        if (index > Count - count)
            throw new ArgumentOutOfRangeException(nameof(count));
        if (index == 0 && count == Count)
            return this;
        if (count == 0)
            return EmptyHandle();

        var end = index + count;
        var leftStart = Math.Min(index, _left.Count);
        var leftEnd = Math.Min(end, _left.Count);
        var rightStart = Math.Max(0, index - _left.Count);
        var rightEnd = Math.Max(0, end - _left.Count);

        var left = SliceLeft(_left, leftStart, leftEnd - leftStart);
        var right = SliceRight(_right, rightStart, rightEnd - rightStart);
        return new BilateralAncestralDeque<T>(_arena, left, right, count);
    }

    /// <summary>Splits this deque at a zero-based boundary.</summary>
    /// <param name="index">The number of values placed in the left result.</param>
    /// <returns>The prefix and suffix; together they enumerate the original value.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="index"/> is outside zero through <see cref="Count"/>.
    /// </exception>
    public (BilateralAncestralDeque<T> Left, BilateralAncestralDeque<T> Right) SplitAt(int index)
    {
        ValidateBoundary(index, nameof(index));
        return (Take(index), Drop(index));
    }

    /// <summary>Returns the logical reversal by exchanging the two oriented intervals.</summary>
    /// <returns>The reversed persistent handle.</returns>
    public BilateralAncestralDeque<T> Reverse()
    {
        if (Count <= 1)
            return this;
        return new BilateralAncestralDeque<T>(_arena, _right, _left, Count);
    }

    /// <summary>Returns an empty handle in the same arena.</summary>
    /// <returns>An independently appendable empty deque.</returns>
    public BilateralAncestralDeque<T> Clear()
    {
        if (IsEmpty)
            return this;
        return EmptyHandle();
    }

    /// <summary>Copies the values to a new array in logical order.</summary>
    /// <returns>A new array containing this deque's values.</returns>
    public T[] ToArray()
    {
        if (IsEmpty)
            return [];

        var values = new T[Count];
        var node = _left.Tail;
        for (var index = 0; index < _left.Count; index++)
        {
            values[index] = _arena.GetValue(node);
            if (index + 1 != _left.Count)
                node = _arena.GetParent(node);
        }

        node = _right.Tail;
        for (var index = Count - 1; index >= _left.Count; index--)
        {
            values[index] = _arena.GetValue(node);
            if (index != _left.Count)
                node = _arena.GetParent(node);
        }

        return values;
    }

    /// <summary>Returns a stable front-to-back enumerator over this immutable handle.</summary>
    /// <returns>An enumerator.</returns>
    /// <remarks>
    /// Enumeration takes Theta(Count) time. The forward-oriented right interval is reversed through
    /// a temporary array, so the enumerator can retain O(Count) values in the worst case.
    /// </remarks>
    public IEnumerator<T> GetEnumerator() => Enumerate().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>Validates cached interval endpoints and counts. Test-only; O(1) queries.</summary>
    internal void ValidateInvariants()
    {
        if (Count != checked(_left.Count + _right.Count))
            throw new InvalidOperationException("The total count disagrees with the interval counts.");
        ValidateSegment(_left);
        ValidateSegment(_right);
    }

    private IEnumerable<T> Enumerate()
    {
        var node = _left.Tail;
        for (var index = 0; index < _left.Count; index++)
        {
            yield return _arena.GetValue(node);
            if (index + 1 != _left.Count)
                node = _arena.GetParent(node);
        }

        if (_right.Count == 0)
            yield break;

        var rightValues = new T[_right.Count];
        node = _right.Tail;
        for (var index = _right.Count - 1; index >= 0; index--)
        {
            rightValues[index] = _arena.GetValue(node);
            if (index != 0)
                node = _arena.GetParent(node);
        }

        foreach (var value in rightValues)
            yield return value;
    }

    private Segment AddToTail(Segment segment, T value)
    {
        var tail = _arena.AddLeaf(segment.Tail, value);
        var @base = segment.Count == 0 ? tail : segment.Base;
        return new Segment(segment.Anchor, @base, tail, segment.Count + 1);
    }

    private Segment RemoveTail(Segment segment)
    {
        Debug.Assert(segment.Count != 0);
        var tail = _arena.GetParent(segment.Tail);
        if (segment.Count == 1)
            return Segment.EmptyAt(tail);
        return new Segment(segment.Anchor, segment.Base, tail, segment.Count - 1);
    }

    private Segment RemoveBase(Segment segment)
    {
        Debug.Assert(segment.Count != 0);
        if (segment.Count == 1)
            return Segment.EmptyAt(segment.Tail);

        var newAnchor = segment.Base;
        var newBase = segment.Count == 2
            ? segment.Tail
            : _arena.AncestorAtDepth(
                segment.Tail,
                checked(_arena.GetDepth(segment.Tail) - segment.Count + 2));
        return new Segment(newAnchor, newBase, segment.Tail, segment.Count - 1);
    }

    private Segment SliceLeft(Segment segment, int start, int count)
    {
        Debug.Assert(start >= 0 && count >= 0 && start <= segment.Count - count);
        if (count == 0)
            return Segment.EmptyAt(_arena.Bottom);
        if (start == 0 && count == segment.Count)
            return segment;

        var tailDepth = _arena.GetDepth(segment.Tail);
        var baseDepth = tailDepth - segment.Count + 1;
        var slicedTailDepth = tailDepth - start;
        var slicedBaseDepth = slicedTailDepth - count + 1;

        var tail = slicedTailDepth == tailDepth
            ? segment.Tail
            : _arena.AncestorAtDepth(segment.Tail, slicedTailDepth);
        int @base;
        if (slicedBaseDepth == slicedTailDepth)
            @base = tail;
        else if (slicedBaseDepth == baseDepth)
            @base = segment.Base;
        else
            @base = _arena.AncestorAtDepth(segment.Tail, slicedBaseDepth);

        var anchor = @base == segment.Base ? segment.Anchor : _arena.GetParent(@base);
        return new Segment(anchor, @base, tail, count);
    }

    private Segment SliceRight(Segment segment, int start, int count)
    {
        Debug.Assert(start >= 0 && count >= 0 && start <= segment.Count - count);
        if (count == 0)
            return Segment.EmptyAt(_arena.Bottom);
        if (start == 0 && count == segment.Count)
            return segment;

        var anchorDepth = _arena.GetDepth(segment.Anchor);
        var slicedBaseDepth = checked(anchorDepth + start + 1);
        var slicedTailDepth = checked(slicedBaseDepth + count - 1);
        var end = start + count;

        var @base = start == 0
            ? segment.Base
            : _arena.AncestorAtDepth(segment.Tail, slicedBaseDepth);
        int tail;
        if (slicedTailDepth == slicedBaseDepth)
            tail = @base;
        else if (end == segment.Count)
            tail = segment.Tail;
        else
            tail = _arena.AncestorAtDepth(segment.Tail, slicedTailDepth);

        var anchor = start == 0 ? segment.Anchor : _arena.GetParent(@base);
        return new Segment(anchor, @base, tail, count);
    }

    private void ValidateSegment(Segment segment)
    {
        if (segment.Count == 0)
        {
            if (segment.Anchor != segment.Base || segment.Base != segment.Tail)
                throw new InvalidOperationException("An empty interval does not have one shared endpoint.");
            return;
        }

        var anchorDepth = _arena.GetDepth(segment.Anchor);
        var tailDepth = _arena.GetDepth(segment.Tail);
        if (tailDepth - anchorDepth != segment.Count)
            throw new InvalidOperationException("An interval count disagrees with its endpoint depths.");
        if (_arena.GetParent(segment.Base) != segment.Anchor)
            throw new InvalidOperationException("The cached base is not the first node after the anchor.");
        if (_arena.AncestorAtDepth(segment.Tail, anchorDepth) != segment.Anchor)
            throw new InvalidOperationException("The interval anchor is not an ancestor of its tail.");
        if (_arena.AncestorAtDepth(segment.Tail, anchorDepth + 1) != segment.Base)
            throw new InvalidOperationException("The cached base is not on the tail's ancestry path.");
    }

    private void ValidateBoundary(int index, string parameterName)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index, parameterName);
        if (index > Count)
            throw new ArgumentOutOfRangeException(parameterName);
    }

    private void EnsureRoom()
    {
        if (Count == int.MaxValue)
            throw new OverflowException("The deque count exceeds Int32.MaxValue.");
    }

    private BilateralAncestralDeque<T> EmptyHandle()
    {
        var empty = Segment.EmptyAt(_arena.Bottom);
        return new BilateralAncestralDeque<T>(_arena, empty, empty, 0);
    }

    private void EnsureNotEmpty()
    {
        if (IsEmpty)
            throw new InvalidOperationException("The bilateral ancestral deque is empty.");
    }

    private static ArgumentOutOfRangeException IndexError(int index) =>
        new(nameof(index), index, "Index is outside the valid range of the deque.");

    private readonly record struct Segment(int Anchor, int Base, int Tail, int Count)
    {
        internal static Segment EmptyAt(int node) => new(node, node, node, 0);
    }
}
