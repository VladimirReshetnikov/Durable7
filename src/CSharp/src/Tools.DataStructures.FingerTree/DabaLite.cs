using System.Diagnostics;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// Maintains a FIFO sliding-window aggregate with worst-case constant work per operation.
/// </summary>
/// <typeparam name="T">The monoid value stored in and aggregated across the window.</typeparam>
/// <typeparam name="TMonoid">The static monoid operations.</typeparam>
/// <remarks>
/// This is Tangwongsan, Hirzel, and Schneider's DABA Lite algorithm. It is deliberately mutable:
/// each call performs one bounded incremental-reversal fixup. The backing queue is a linked sequence
/// of fixed-size chunks, so insertion, eviction, cursor movement, and queue growth are worst-case O(1).
/// </remarks>
[DebuggerDisplay("Count = {Count}, Aggregate = {Aggregate}")]
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

    /// <summary>Gets the aggregate in FIFO order.</summary>
    /// <remarks>Invokes the monoid combine operation exactly once, including for an empty window.</remarks>
    public T Aggregate => TMonoid.Combine(ProductF(), _aggregateB);

    /// <summary>Inserts a value at the back of the window.</summary>
    /// <param name="value">The value to insert.</param>
    /// <remarks>Invokes the monoid combine operation at most three times.</remarks>
    public void Insert(T value)
    {
        if (Count == int.MaxValue)
            throw new OverflowException("The DABA Lite window cannot contain more than Int32.MaxValue items.");
        _queue.Write(_e, value);
        _e = _queue.AdvanceEnd(_e);
        Count++;
        _aggregateB = TMonoid.Combine(_aggregateB, value);
        Fixup();
    }

    /// <summary>Evicts the oldest value.</summary>
    /// <exception cref="InvalidOperationException">The window is empty.</exception>
    /// <remarks>Invokes the monoid combine operation at most two times.</remarks>
    public void Evict()
    {
        if (!TryEvict())
            throw new InvalidOperationException("Cannot evict from an empty DABA Lite window.");
    }

    /// <summary>Attempts to evict the oldest value.</summary>
    /// <returns><see langword="true"/> when a value was evicted; otherwise, <see langword="false"/>.</returns>
    public bool TryEvict()
    {
        if (Count == 0)
            return false;
        _f = _queue.Advance(_f);
        Count--;
        Fixup();
        _queue.TrimBefore(_f);
        return true;
    }

    /// <summary>Evicts every value and resets the aggregate to the monoid identity.</summary>
    public void Clear()
    {
        while (TryEvict())
        {
        }
    }

    private T ProductF() => _f == _b ? TMonoid.Empty : _queue.Read(_f);

    private T ProductL() => _l == _r ? TMonoid.Empty : _queue.Read(_l);

    private T ProductA() => _a == _b ? TMonoid.Empty : _queue.Read(_a);

    private void Fixup()
    {
        if (_f == _b)
        {
            _b = _a = _r = _l = _e;
            _aggregateRA = TMonoid.Empty;
            _aggregateB = TMonoid.Empty;
            return;
        }

        if (_l == _b)
        {
            _l = _f;
            _a = _b = _e;
            _aggregateRA = _aggregateB;
            _aggregateB = TMonoid.Empty;
        }

        if (_l == _r)
        {
            _a = _queue.Advance(_a);
            _r = _queue.Advance(_r);
            _l = _queue.Advance(_l);
            return;
        }

        _queue.Write(_l, TMonoid.Combine(ProductL(), _aggregateRA));
        _l = _queue.Advance(_l);
        var beforeA = _queue.Retreat(_a);
        _queue.Write(beforeA, TMonoid.Combine(_queue.Read(beforeA), ProductA()));
        _a = beforeA;
    }

    private readonly record struct Cursor(Block Block, int Index);

    private sealed class Block
    {
        internal const int Capacity = 64;
        internal readonly T[] Items = new T[Capacity];
        internal Block? Previous;
        internal Block? Next;
    }

    private sealed class ChunkedQueue
    {
        private Block _first;

        internal ChunkedQueue()
        {
            _first = new Block();
            Begin = new Cursor(_first, 0);
        }

        internal Cursor Begin { get; }

        internal T Read(Cursor cursor) => cursor.Block.Items[cursor.Index];

        internal void Write(Cursor cursor, T value) => cursor.Block.Items[cursor.Index] = value;

        internal Cursor Advance(Cursor cursor)
        {
            if (cursor.Index + 1 < Block.Capacity)
                return new Cursor(cursor.Block, cursor.Index + 1);
            Debug.Assert(cursor.Block.Next is not null);
            return new Cursor(cursor.Block.Next!, 0);
        }

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

        internal Cursor Retreat(Cursor cursor)
        {
            if (cursor.Index != 0)
                return new Cursor(cursor.Block, cursor.Index - 1);
            Debug.Assert(cursor.Block.Previous is not null);
            return new Cursor(cursor.Block.Previous!, Block.Capacity - 1);
        }

        internal void TrimBefore(Cursor front)
        {
            if (ReferenceEquals(_first, front.Block))
                return;
            _first = front.Block;
            _first.Previous = null;
        }
    }
}
