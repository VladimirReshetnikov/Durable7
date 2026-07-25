using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

/// <summary>
/// An immutable, persistent catenable deque that additionally supports <see cref="Reverse"/> in O(1), backed
/// by a size-measured finger tree whose nodes carry a reversal bit.
/// </summary>
/// <typeparam name="T">Element type stored in the deque.</typeparam>
/// <remarks>
/// <para>
/// This is the reversible sibling of <see cref="FingerTreeDeque{T}"/>. It preserves all of that type's
/// asymptotic bounds — O(1) endpoint reads, O(log n) worst / O(1) amortized (linear-use) endpoint
/// insertion and removal, O(log(min(n, m))) concatenation, and indexing/splitting logarithmic in the
/// distance from the nearer end — and adds O(1) <see cref="Reverse"/>. Reversal even composes with
/// concatenation at no asymptotic cost: <c>a.Reverse().Concat(b)</c> is still O(log(min)).
/// </para>
/// <para>
/// The price is a constant-factor overhead relative to <see cref="FingerTreeDeque{T}"/>: every internal
/// access tests a reversal bit, and operations on a reversed portion allocate small orientation-adjusted
/// digit copies. The representation is strict (no memoized spine), so the amortized endpoint bound is for
/// single-threaded linear use; choose this type when O(1) reverse is needed and the plain deque otherwise.
/// </para>
/// <para>Instances are immutable snapshots and safe for concurrent reads.</para>
/// </remarks>
/// <example>
/// <code>
/// var deque = ReversibleDeque&lt;int&gt;.Create(1, 2, 3, 4);
/// var flipped = deque.Reverse();        // O(1); [4, 3, 2, 1]
/// var joined = deque.Concat(flipped);   // O(log n); [1, 2, 3, 4, 4, 3, 2, 1]
/// // deque is still [1, 2, 3, 4]
/// </code>
/// </example>
public sealed partial class ReversibleDeque<T> : IReadOnlyList<T>
{
    private static readonly ReversibleDeque<T> EmptyInstance = new(RevEmptyTree<T>.Instance);

    private readonly RevTree<T> _root;

    private ReversibleDeque(RevTree<T> root) => _root = root;

    /// <summary>Gets the empty deque.</summary>
    public static ReversibleDeque<T> Empty => EmptyInstance;

    /// <summary>Gets the number of elements. O(1).</summary>
    public int Count => _root.Size;

    /// <summary>Gets a value indicating whether the deque is empty. O(1).</summary>
    public bool IsEmpty => _root.IsEmpty;

    /// <summary>Gets the first element. O(1).</summary>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public T First => _root.IsEmpty ? throw EmptyError() : _root.First;

    /// <summary>Gets the last element. O(1).</summary>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public T Last => _root.IsEmpty ? throw EmptyError() : _root.Last;

    /// <summary>Gets the element at <paramref name="index"/>. O(log min(index + 1, Count - index)).</summary>
    /// <param name="index">Zero-based element index.</param>
    /// <returns>The element at <paramref name="index"/>.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public T this[int index]
    {
        get
        {
            if ((uint)index >= (uint)Count)
                throw IndexError(index);
            return _root.GetLeaf(index);
        }
    }

    /// <summary>Creates a deque from the given elements in order. O(n).</summary>
    /// <param name="items">Elements to store.</param>
    /// <returns>A deque containing <paramref name="items"/> in order.</returns>
    public static ReversibleDeque<T> Create(params ReadOnlySpan<T> items)
    {
        RevTree<T> root = RevEmptyTree<T>.Instance;
        foreach (var item in items)
            root = root.Snoc(new RevLeaf<T>(item));
        return Wrap(root);
    }

    /// <summary>Creates a deque from an enumerable source, enumerated once. O(n).</summary>
    /// <param name="items">Elements to store.</param>
    /// <returns>A deque containing the enumeration results in order.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static ReversibleDeque<T> CreateRange(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items is ReversibleDeque<T> existing)
            return existing;

        RevTree<T> root = RevEmptyTree<T>.Instance;
        foreach (var item in items)
            root = root.Snoc(new RevLeaf<T>(item));
        return Wrap(root);
    }

    /// <summary>Returns a deque with <paramref name="item"/> prepended. O(1) amortized.</summary>
    /// <param name="item">Element to prepend.</param>
    /// <exception cref="OverflowException">The deque already holds <see cref="int.MaxValue"/> elements.</exception>
    public ReversibleDeque<T> AddFirst(T item)
    {
        CheckRoom();
        return new(_root.Cons(new RevLeaf<T>(item)));
    }

    /// <summary>Returns a deque with <paramref name="item"/> appended. O(1) amortized.</summary>
    /// <param name="item">Element to append.</param>
    /// <exception cref="OverflowException">The deque already holds <see cref="int.MaxValue"/> elements.</exception>
    public ReversibleDeque<T> AddLast(T item)
    {
        CheckRoom();
        return new(_root.Snoc(new RevLeaf<T>(item)));
    }

    /// <summary>Removes the first element. O(log n) worst, O(1) amortized.</summary>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public ReversibleDeque<T> RemoveFirst()
    {
        if (!_root.ViewL(out _, out var rest))
            throw EmptyError();
        return Wrap(rest);
    }

    /// <summary>Removes the last element. O(log n) worst, O(1) amortized.</summary>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public ReversibleDeque<T> RemoveLast()
    {
        if (!_root.ViewR(out _, out var rest))
            throw EmptyError();
        return Wrap(rest);
    }

    /// <summary>Attempts to remove and return the first element. O(log n) worst, O(1) amortized.</summary>
    /// <param name="value">The removed first element when present; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining deque, or <see cref="Empty"/>.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public bool TryRemoveFirst([MaybeNullWhen(false)] out T value, out ReversibleDeque<T> rest)
    {
        if (_root.ViewL(out var head, out var remaining))
        {
            value = ((RevLeaf<T>)head).Value;
            rest = Wrap(remaining);
            return true;
        }

        value = default;
        rest = EmptyInstance;
        return false;
    }

    /// <summary>Attempts to remove and return the last element. O(log n) worst, O(1) amortized.</summary>
    /// <param name="value">The removed last element when present; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining deque, or <see cref="Empty"/>.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public bool TryRemoveLast([MaybeNullWhen(false)] out T value, out ReversibleDeque<T> rest)
    {
        if (_root.ViewR(out var last, out var remaining))
        {
            value = ((RevLeaf<T>)last).Value;
            rest = Wrap(remaining);
            return true;
        }

        value = default;
        rest = EmptyInstance;
        return false;
    }

    /// <summary>Concatenates this deque with <paramref name="other"/>. O(log(min(n, m))), reversal-aware.</summary>
    /// <param name="other">Deque whose elements follow this deque's elements.</param>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The combined count would exceed <see cref="int.MaxValue"/>.</exception>
    public ReversibleDeque<T> Concat(ReversibleDeque<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (other._root.IsEmpty)
            return this;
        if (_root.IsEmpty)
            return other;
        if (other.Count > int.MaxValue - Count)
            throw OverflowError();
        return new(RevTreeOps.Concat(_root, other._root));
    }

    /// <summary>Replaces the element at <paramref name="index"/>. O(log min(index + 1, Count - index)).</summary>
    /// <param name="index">Zero-based element index.</param>
    /// <param name="value">Replacement value.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public ReversibleDeque<T> SetItem(int index, T value)
    {
        if ((uint)index >= (uint)Count)
            throw IndexError(index);
        return new(_root.SetLeaf(index, value));
    }

    /// <summary>Inserts <paramref name="item"/> at <paramref name="index"/>. O(log n).</summary>
    /// <param name="index">Insertion index in <c>0..Count</c>.</param>
    /// <param name="item">Element to insert.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    /// <exception cref="OverflowException">The deque already holds <see cref="int.MaxValue"/> elements.</exception>
    public ReversibleDeque<T> InsertAt(int index, T item)
    {
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        if (index == 0)
            return AddFirst(item);
        if (index == Count)
            return AddLast(item);

        CheckRoom();
        _root.SplitTree(index, out var left, out var hit, out _, out var right);
        return new(RevTreeOps.Concat(left.Snoc(new RevLeaf<T>(item)), right.Cons(hit)));
    }

    /// <summary>Removes the element at <paramref name="index"/>. O(log min(index + 1, Count - index)).</summary>
    /// <param name="index">Zero-based element index.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public ReversibleDeque<T> RemoveAt(int index)
    {
        if ((uint)index >= (uint)Count)
            throw IndexError(index);
        _root.SplitTree(index, out var left, out _, out _, out var right);
        return Wrap(RevTreeOps.Concat(left, right));
    }

    /// <summary>Splits the deque at <paramref name="index"/>. O(log(min(index, Count - index) + 1)).</summary>
    /// <param name="index">Split index in <c>0..Count</c>.</param>
    /// <returns>The prefix before <paramref name="index"/> and the suffix from <paramref name="index"/>.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public (ReversibleDeque<T> Left, ReversibleDeque<T> Right) SplitAt(int index)
    {
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        if (index == 0)
            return (EmptyInstance, this);
        if (index == Count)
            return (this, EmptyInstance);

        _root.SplitTree(index, out var left, out var hit, out _, out var right);
        return (Wrap(left), Wrap(right.Cons(hit)));
    }

    /// <summary>Returns a deque with the elements in reverse order. O(1).</summary>
    /// <returns>The reversed deque. The original is unchanged.</returns>
    public ReversibleDeque<T> Reverse() => Wrap(_root.Mirror());

    /// <summary>Copies the elements to a new array in order. O(n).</summary>
    /// <returns>A new array of the elements in order.</returns>
    public T[] ToArray()
    {
        if (_root.IsEmpty)
            return [];
        var array = new T[Count];
        var offset = 0;
        _root.CopyLeaves(array, ref offset);
        return array;
    }

    /// <summary>Returns an enumerator over the elements in order.</summary>
    /// <returns>An enumerator yielding elements front to back.</returns>
    /// <remarks>Enumeration is O(n) total with O(1) amortized cost per yielded element and an O(log n) stack.</remarks>
    public Enumerator GetEnumerator() => new(_root);

    IEnumerator<T> IEnumerable<T>.GetEnumerator() => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>Validates internal cached measures and digit-length invariants. Test-only; O(n).</summary>
    internal void ValidateInvariants()
    {
        var computed = _root.ValidateAndCount();
        if (computed != Count)
            throw new InvalidOperationException($"Root size {Count} disagrees with recomputed total {computed}.");
    }

    private static ReversibleDeque<T> Wrap(RevTree<T> root) => root.IsEmpty ? EmptyInstance : new(root);

    private void CheckRoom()
    {
        if (Count == int.MaxValue)
            throw OverflowError();
    }

    private static InvalidOperationException EmptyError() => new("The deque is empty.");

    private static ArgumentOutOfRangeException IndexError(int index) =>
        new(nameof(index), index, "Index is outside the valid range of the deque.");

    private static OverflowException OverflowError() =>
        new("The operation would create a deque with more than Int32.MaxValue elements.");

    /// <summary>
    /// Enumerates a <see cref="ReversibleDeque{T}"/> without materializing the sequence.
    /// </summary>
    /// <remarks>
    /// The traversal carries an orientation bit on each stack frame, so reversed trees and nodes are read in
    /// logical order without allocating mirrored wrappers. The enumerator observes the immutable snapshot it
    /// was created from. Value copies of an in-progress enumerator share one traversal stack: advancing a
    /// copy invalidates every other copy, whose next <see cref="MoveNext"/> throws
    /// <see cref="InvalidOperationException"/> instead of silently skipping elements.
    /// </remarks>
    public struct Enumerator : IEnumerator<T>
    {
        private readonly TraversalState? _state;
        private int _cursor;
        private T _current;

        internal Enumerator(RevTree<T> root)
        {
            _state = root.IsEmpty ? null : new TraversalState(root);
            _cursor = 0;
            _current = default!;
        }

        /// <summary>Gets the current element.</summary>
        /// <remarks>Undefined before the first call to <see cref="MoveNext"/> and after enumeration ends.</remarks>
        public readonly T Current => _current;

        readonly object? IEnumerator.Current => Current;

        /// <summary>Releases resources held by the enumerator.</summary>
        public void Dispose()
        {
        }

        /// <summary>Advances to the next element.</summary>
        /// <returns><see langword="true"/> when the enumerator advanced to an element; otherwise <see langword="false"/>.</returns>
        public bool MoveNext()
        {
            var state = _state;
            if (state is null)
                return false;
            if (_cursor != state.Cursor)
                throw CopyDivergedError();

            while (state.Depth > 0)
            {
                ref var frame = ref state.Frames[state.Depth - 1];
                if (frame.NextChild == frame.ChildCount)
                {
                    frame = default;
                    state.Depth--;
                    continue;
                }

                // Advance the current frame before any Push: a Push may grow (and thus copy) the stack,
                // and the increment must already be recorded in the slot that gets copied.
                var childIndex = frame.NextChild++;
                if (frame.TryGetChild(
                    childIndex,
                    out var leaf,
                    out var tree,
                    out var node,
                    out var childMirrored))
                {
                    _current = leaf;
                    _cursor = ++state.Cursor;
                    return true;
                }

                if (tree is not null)
                    state.Push(tree, childMirrored);
                else
                    state.Push(node!, childMirrored);
            }

            _current = default!;
            return false;
        }

        /// <summary>Not supported; create a new enumerator instead.</summary>
        /// <exception cref="NotSupportedException">Always thrown.</exception>
        void IEnumerator.Reset() => throw new NotSupportedException();

        private static InvalidOperationException CopyDivergedError() =>
            new("Another value copy of this enumerator has been advanced. Copies share one traversal "
                + "stack and cannot be advanced independently; create a new enumerator instead.");

        /// <summary>
        /// Traversal state shared by all value copies of one enumerator. <see cref="Cursor"/> counts
        /// elements yielded from the shared stack; each enumerator copy tracks the cursor value it last
        /// observed, so a copy left behind by another copy's advance is detected and fails fast.
        /// </summary>
        private sealed class TraversalState
        {
            public Frame[] Frames = new Frame[8];
            public int Depth;
            public int Cursor;

            public TraversalState(RevTree<T> root) => Push(root, mirrored: false);

            public void Push(RevTree<T> tree, bool mirrored)
            {
                if (Depth == Frames.Length)
                    Array.Resize(ref Frames, Depth * 2);
                Frames[Depth++] = new Frame(tree, mirrored);
            }

            public void Push(RevNode<T> node, bool mirrored)
            {
                if (Depth == Frames.Length)
                    Array.Resize(ref Frames, Depth * 2);
                Frames[Depth++] = new Frame(node, mirrored);
            }
        }

        private struct Frame
        {
            private readonly RevTree<T>? _tree;
            private readonly RevNode<T>? _node;
            private readonly bool _mirrored;

            public int NextChild;

            public Frame(RevTree<T> tree, bool mirrored)
            {
                _tree = tree;
                _node = null;
                _mirrored = mirrored;
                NextChild = 0;
            }

            public Frame(RevNode<T> node, bool mirrored)
            {
                _tree = null;
                _node = node;
                _mirrored = mirrored;
                NextChild = 0;
            }

            public readonly int ChildCount => _tree?.EnumerationChildCount ?? _node!.EnumerationChildCount;

            public readonly bool TryGetChild(
                int index,
                out T leaf,
                out RevTree<T>? tree,
                out RevNode<T>? node,
                out bool childMirrored)
            {
                if (_tree is not null)
                    return _tree.TryGetEnumerationChild(index, _mirrored, out leaf, out tree, out node, out childMirrored);

                tree = null;
                return _node!.TryGetEnumerationChild(index, _mirrored, out leaf, out node, out childMirrored);
            }
        }
    }
}
