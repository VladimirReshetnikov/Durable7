// Maintains a FIFO sliding-window aggregate with worst-case constant work per operation.

using System.Diagnostics;
using System.Runtime.CompilerServices;

namespace Durable7.FingerTree;

/// <summary>
/// Maintains a FIFO sliding-window aggregate with worst-case constant work per operation.
/// </summary>
/// <typeparam name="T">The monoid value stored in and aggregated across the window.</typeparam>
/// <typeparam name="TMonoid">The static monoid operations.</typeparam>
/// <remarks>
/// This is Tangwongsan, Hirzel, and Schneider's DABA Lite algorithm. It is deliberately mutable:
/// each call performs one bounded incremental-reversal fixup. The backing queue is a linked sequence
/// of fixed-size chunks, so insertion, eviction, cursor movement, and queue growth are worst-case O(1).
/// The total bound assumes that the monoid identity and combine operations are themselves O(1); the
/// algorithm unconditionally bounds how many times they are invoked. The type does not support
/// overlapping access to one instance; callers must provide external synchronization.
/// Callback exception guarantees cover this object's own state publication; side effects performed
/// inside caller-supplied callbacks cannot be rolled back.
/// </remarks>
[DebuggerDisplay("Count = {Count}")]
public sealed class DabaLite<T, TMonoid>
    where TMonoid : IMonoid<T>
{
    private readonly ChunkedQueue _queue = new();
    private Cursor _f;
    private Cursor _l;
    private Cursor _r;
    private Cursor _a;
    private Cursor _b;
    private Cursor _e;
    private T _aggregateRA = TMonoid.Empty;
    private T _aggregateB = TMonoid.Empty;

    /// <summary>Initializes an empty aggregator.</summary>
    public DabaLite()
    {
        _f = _l = _r = _a = _b = _e = _queue.Begin;
    }

    /// <summary>Gets the number of values in the current window.</summary>
    public int Count { get; private set; }

    /// <summary>Gets whether the current window is empty.</summary>
    public bool IsEmpty => Count == 0;

    /// <summary>Validates the chunk links, cursor order, and DABA Lite size invariants.</summary>
    /// <returns>Statistics for the validated representation.</returns>
    /// <remarks>
    /// This structural validation takes O(c) time and space for c active chunks (and therefore O(n)
    /// in the window size) and does not invoke the monoid. DABA Lite overwrites original values with
    /// partial aggregates, so an arbitrary non-invertible monoid does not provide enough information
    /// to reconstruct and validate every content invariant. Aggregate correctness is therefore
    /// validated against an external FIFO model.
    /// </remarks>
    public DabaLiteStatistics ValidateStructure()
    {
        ValidateCursorIndex(_f, "F");
        ValidateCursorIndex(_l, "L");
        ValidateCursorIndex(_r, "R");
        ValidateCursorIndex(_a, "A");
        ValidateCursorIndex(_b, "B");
        ValidateCursorIndex(_e, "E");

        var first = _queue.FirstBlock;
        if (first.Previous is not null)
            throw new InvalidOperationException("The first DABA Lite chunk has a predecessor.");
        if (!ReferenceEquals(first, _f.Block))
            throw new InvalidOperationException("The DABA Lite front cursor is not in the first active chunk.");

        var seen = new HashSet<Block>();
        Block? previous = null;
        var block = first;
        var blockBase = 0L;
        var blockCount = 0;
        var f = -1L;
        var l = -1L;
        var r = -1L;
        var a = -1L;
        var b = -1L;
        var e = -1L;
        while (true)
        {
            if (!seen.Add(block))
                throw new InvalidOperationException("The DABA Lite chunk chain contains a cycle.");
            if (!ReferenceEquals(block.Previous, previous))
                throw new InvalidOperationException("A DABA Lite chunk has an invalid predecessor link.");
            if (previous is not null && !ReferenceEquals(previous.Next, block))
                throw new InvalidOperationException("A DABA Lite chunk has an invalid successor link.");

            RecordCursorPosition(block, blockBase, _f, ref f);
            RecordCursorPosition(block, blockBase, _l, ref l);
            RecordCursorPosition(block, blockBase, _r, ref r);
            RecordCursorPosition(block, blockBase, _a, ref a);
            RecordCursorPosition(block, blockBase, _b, ref b);
            RecordCursorPosition(block, blockBase, _e, ref e);
            blockCount = checked(blockCount + 1);

            if (ReferenceEquals(block, _e.Block))
            {
                if (block.Next is not null)
                    throw new InvalidOperationException("The DABA Lite end cursor is not in the last active chunk.");
                break;
            }

            previous = block;
            block = block.Next
                ?? throw new InvalidOperationException("The DABA Lite end cursor is not reachable from the front.");
            blockBase = checked(blockBase + Block.Capacity);
        }

        if (f < 0 || l < 0 || r < 0 || a < 0 || b < 0 || e < 0)
            throw new InvalidOperationException("A DABA Lite cursor is outside the active chunk chain.");
        if (!(f <= l && l <= r && r <= a && a <= b && b <= e))
            throw new InvalidOperationException("DABA Lite cursors do not satisfy F <= L <= R <= A <= B <= E.");
        if (e - f != Count)
            throw new InvalidOperationException("The DABA Lite count disagrees with the F-to-E cursor distance.");

        var frontCount = b - f;
        var backCount = e - b;
        var leftCount = r - l;
        var rightCount = a - r;
        var accumulatorCount = b - a;
        if (Count == 0)
        {
            if (!(_f == _l && _l == _r && _r == _a && _a == _b && _b == _e))
                throw new InvalidOperationException("An empty DABA Lite window does not have coincident cursors.");
        }
        else
        {
            if (leftCount != rightCount)
                throw new InvalidOperationException("The DABA Lite left and right work regions have different sizes.");
            if (checked(leftCount + rightCount + accumulatorCount + 1) != frontCount - backCount)
                throw new InvalidOperationException("The DABA Lite incremental-reversal size equation is invalid.");
            if (l - f != backCount + 1)
                throw new InvalidOperationException("The DABA Lite processed-prefix distance is invalid.");
        }

        var expectedBlockCount = checked((_f.Index + (long)Count) / Block.Capacity + 1);
        if (blockCount != expectedBlockCount)
            throw new InvalidOperationException("The DABA Lite active chunk count is invalid.");
        var allocatedSlotCount = checked((long)blockCount * Block.Capacity);
        var slackSlotCount = allocatedSlotCount - Count;
        if (slackSlotCount is < 1 or > (2 * Block.Capacity - 1))
            throw new InvalidOperationException("The DABA Lite chunk slack exceeds its two-chunk bound.");

        return new DabaLiteStatistics(
            Count,
            checked((int)frontCount),
            checked((int)backCount),
            checked((int)leftCount),
            checked((int)rightCount),
            checked((int)accumulatorCount),
            blockCount,
            allocatedSlotCount,
            slackSlotCount);
    }

    /// <summary>Gets the aggregate in FIFO order.</summary>
    /// <remarks>
    /// Returns the monoid identity without invoking <see cref="IMonoid{TMeasure}.Combine"/> for an
    /// empty window; otherwise, invokes the combine operation exactly once.
    /// </remarks>
    public T Aggregate => Count == 0 ? TMonoid.Empty : TMonoid.Combine(_queue.Read(_f), _aggregateB);

    /// <summary>Inserts a value at the back of the window.</summary>
    /// <param name="value">The value to insert.</param>
    /// <exception cref="OverflowException">The window already contains <see cref="int.MaxValue"/> values.</exception>
    /// <remarks>
    /// Invokes the monoid combine operation at most three times. If a monoid callback throws, the
    /// aggregator's published window state is unchanged.
    /// </remarks>
    public void Insert(T value)
    {
        if (Count == int.MaxValue)
            throw new OverflowException("The DABA Lite window cannot contain more than Int32.MaxValue items.");

        var oldEnd = _e;
        var oldCount = Count;
        var oldAggregateB = _aggregateB;
        var oldValue = _queue.Read(oldEnd);
        var oldSuccessor = oldEnd.Block.Next;
        var newAggregateB = TMonoid.Combine(oldAggregateB, value);
        try
        {
            var newEnd = _queue.AdvanceEnd(oldEnd);
            _queue.Write(oldEnd, value);
            _e = newEnd;
            Count = oldCount + 1;
            _aggregateB = newAggregateB;
            Fixup();
        }
        catch
        {
            _queue.Write(oldEnd, oldValue);
            _queue.RestoreSuccessor(oldEnd.Block, oldSuccessor);
            _e = oldEnd;
            Count = oldCount;
            _aggregateB = oldAggregateB;
            throw;
        }
    }

    /// <summary>Evicts the oldest value.</summary>
    /// <exception cref="InvalidOperationException">The window is empty.</exception>
    /// <remarks>
    /// Invokes the monoid combine operation at most two times. If a monoid callback throws, the
    /// aggregator's published window state is unchanged.
    /// </remarks>
    public void Evict()
    {
        if (!TryEvict())
            throw new InvalidOperationException("Cannot evict from an empty DABA Lite window.");
    }

    /// <summary>Attempts to evict the oldest value.</summary>
    /// <returns><see langword="true"/> when a value was evicted; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// Invokes the monoid combine operation at most two times. If a monoid callback throws, the
    /// aggregator's published window state is unchanged.
    /// </remarks>
    public bool TryEvict()
    {
        if (Count == 0)
            return false;

        var oldFront = _f;
        var oldCount = Count;
        _f = _queue.Advance(oldFront);
        Count = oldCount - 1;
        try
        {
            Fixup();
        }
        catch
        {
            _f = oldFront;
            Count = oldCount;
            throw;
        }

        _queue.ClearSlot(oldFront);
        _queue.TrimBefore(_f);
        return true;
    }

    /// <summary>Evicts every value and resets the aggregate to the monoid identity in O(1) time.</summary>
    /// <remarks>If the monoid identity callback throws, the window is unchanged.</remarks>
    public void Clear()
    {
        if (Count == 0)
            return;

        var empty = TMonoid.Empty;
        var begin = _queue.Reset();
        _f = _l = _r = _a = _b = _e = begin;
        _aggregateRA = empty;
        _aggregateB = empty;
        Count = 0;
    }

    private void Fixup()
    {
        var l = _l;
        var r = _r;
        var a = _a;
        var b = _b;
        var aggregateRA = _aggregateRA;
        var aggregateB = _aggregateB;
        var empty = default(T)!;
        var hasEmpty = false;

        if (_f == b)
        {
            empty = TMonoid.Empty;
            l = r = a = b = _e;
            PublishFixup(l, r, a, b, empty, empty);
            return;
        }

        if (l == b)
        {
            empty = TMonoid.Empty;
            hasEmpty = true;
            l = _f;
            a = b = _e;
            aggregateRA = aggregateB;
            aggregateB = empty;
        }

        if (l == r)
        {
            var nextL = _queue.Advance(l);
            var nextR = _queue.Advance(r);
            var nextA = _queue.Advance(a);
            if (!hasEmpty)
                empty = TMonoid.Empty;
            PublishFixup(nextL, nextR, nextA, b, empty, aggregateB);
            return;
        }

        var nextLeft = _queue.Advance(l);
        var beforeA = _queue.Retreat(a);
        var newLeftValue = TMonoid.Combine(_queue.Read(l), aggregateRA);
        var productA = a == b ? TMonoid.Empty : _queue.Read(a);
        var newAccumulatorValue = TMonoid.Combine(_queue.Read(beforeA), productA);

        _queue.Write(l, newLeftValue);
        _queue.Write(beforeA, newAccumulatorValue);
        PublishFixup(nextLeft, r, beforeA, b, aggregateRA, aggregateB);
    }

    private void PublishFixup(
        Cursor l,
        Cursor r,
        Cursor a,
        Cursor b,
        T aggregateRA,
        T aggregateB)
    {
        _l = l;
        _r = r;
        _a = a;
        _b = b;
        _aggregateRA = aggregateRA;
        _aggregateB = aggregateB;
    }

    private static void ValidateCursorIndex(Cursor cursor, string name)
    {
        if ((uint)cursor.Index >= Block.Capacity)
            throw new InvalidOperationException($"The DABA Lite {name} cursor has an invalid chunk index.");
    }

    private static void RecordCursorPosition(Block block, long blockBase, Cursor cursor, ref long position)
    {
        if (ReferenceEquals(block, cursor.Block))
            position = checked(blockBase + cursor.Index);
    }

    private readonly record struct Cursor(Block Block, int Index);

    private sealed class Block
    {
        /// <summary>Gets the capacity.</summary>
        internal const int Capacity = 64;
        /// <summary>Gets the elements.</summary>
        internal readonly T[] Items = new T[Capacity];
        /// <summary>Gets the previous.</summary>
        internal Block? Previous;
        /// <summary>Gets the next.</summary>
        internal Block? Next;
    }

    private sealed class ChunkedQueue
    {
        private Block _first;

        /// <summary>Creates a new chunked queue.</summary>
        internal ChunkedQueue()
        {
            _first = new Block();
        }

        /// <summary>Gets the position the queue currently begins at.</summary>
        internal Cursor Begin => new(_first, 0);

        /// <summary>Gets the first block.</summary>
        internal Block FirstBlock => _first;

        /// <summary>Reads the next value from the input.</summary>
        internal T Read(Cursor cursor) => cursor.Block.Items[cursor.Index];

        /// <summary>Writes the value at the current position.</summary>
        internal void Write(Cursor cursor, T value) => cursor.Block.Items[cursor.Index] = value;

        /// <summary>Clears the slot.</summary>
        internal void ClearSlot(Cursor cursor)
        {
            if (RuntimeHelpers.IsReferenceOrContainsReferences<T>())
                cursor.Block.Items[cursor.Index] = default!;
        }

        /// <summary>Returns the value to its initial state.</summary>
        internal Cursor Reset()
        {
            var first = new Block();
            _first = first;
            return new Cursor(first, 0);
        }

        /// <summary>Advances past the given number of elements.</summary>
        internal Cursor Advance(Cursor cursor)
        {
            if (cursor.Index + 1 < Block.Capacity)
                return new Cursor(cursor.Block, cursor.Index + 1);
            Debug.Assert(cursor.Block.Next is not null);
            return new Cursor(cursor.Block.Next!, 0);
        }

        /// <summary>Advances the end.</summary>
        internal Cursor AdvanceEnd(Cursor cursor)
        {
            if (cursor.Index + 1 < Block.Capacity)
                return new Cursor(cursor.Block, cursor.Index + 1);
            var next = cursor.Block.Next;
            if (next is null)
            {
                next = new Block { Previous = cursor.Block };
                cursor.Block.Next = next;
            }
            return new Cursor(next, 0);
        }

        /// <summary>Steps back over the given number of elements.</summary>
        internal Cursor Retreat(Cursor cursor)
        {
            if (cursor.Index != 0)
                return new Cursor(cursor.Block, cursor.Index - 1);
            Debug.Assert(cursor.Block.Previous is not null);
            return new Cursor(cursor.Block.Previous!, Block.Capacity - 1);
        }

        /// <summary>Restores the successor a rollback removed.</summary>
        internal void RestoreSuccessor(Block block, Block? successor)
        {
            var current = block.Next;
            if (ReferenceEquals(current, successor))
                return;
            block.Next = successor;
            if (successor is not null)
                successor.Previous = block;
            if (current is not null)
                current.Previous = null;
        }

        /// <summary>
        /// Drops everything before the position, so the queue's backing store does not grow without bound.
        /// </summary>
        internal void TrimBefore(Cursor front)
        {
            if (ReferenceEquals(_first, front.Block))
                return;
            var predecessor = front.Block.Previous;
            _first = front.Block;
            _first.Previous = null;
            if (predecessor is not null)
                predecessor.Next = null;
        }
    }
}

/// <summary>Describes a validated DABA Lite chunk and incremental-reversal state.</summary>
/// <param name="Count">The number of values in the FIFO window.</param>
/// <param name="FrontLength">The F-to-B front-region length.</param>
/// <param name="BackLength">The B-to-E back-region length.</param>
/// <param name="LeftLength">The L-to-R work-region length.</param>
/// <param name="RightLength">The R-to-A work-region length.</param>
/// <param name="AccumulatorLength">The A-to-B work-region length.</param>
/// <param name="BlockCount">The number of active fixed-size chunks.</param>
/// <param name="AllocatedSlotCapacity">The number of slots in the active chunks.</param>
/// <param name="SlackSlotCount">The allocated slots not occupied by window values.</param>
public readonly record struct DabaLiteStatistics(
    int Count,
    int FrontLength,
    int BackLength,
    int LeftLength,
    int RightLength,
    int AccumulatorLength,
    int BlockCount,
    long AllocatedSlotCapacity,
    long SlackSlotCount);
