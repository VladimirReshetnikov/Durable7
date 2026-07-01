using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// An immutable, persistent, general <em>measured</em> finger tree: a sequence of
/// <typeparamref name="TElement"/> annotated by a monoidal <typeparamref name="TMeasure"/> supplied through
/// <typeparamref name="TMeasureOps"/>, supporting the full Hinze–Paterson operation set parameterized by
/// that measure.
/// </summary>
/// <typeparam name="TElement">Stored element type.</typeparam>
/// <typeparam name="TMeasure">Cached annotation type; the <see cref="Measure"/> of a sequence is the
/// monoidal combination of its elements' measures, in order.</typeparam>
/// <typeparam name="TMeasureOps">A type providing the static measure algebra (identity, combine, and the
/// per-element measure). It is a phantom strategy type — no instance is ever created.</typeparam>
/// <remarks>
/// <para>
/// This is the general structure that <see cref="FingerTreeDeque{T}"/> specializes: choosing the measure
/// selects the behavior. With <see cref="SizeMeasure{TElement}"/> the tree is a positional sequence; with a
/// maximum-priority measure it is a mergeable priority queue; with a maximum-key measure it is an ordered
/// search tree; with a count-plus-key product measure it is an order-statistic tree. The one operation that
/// expresses all of these is <see cref="Split(Func{TMeasure, bool})"/> — splitting at the point where a
/// monotone predicate over the accumulated measure first becomes true.
/// </para>
/// <para>
/// Complexity (with O(1) measure operations): <see cref="IsEmpty"/>, <see cref="First"/>,
/// <see cref="Last"/> are O(1) worst-case; <see cref="Measure"/> is O(1) amortized (the first read of a fresh
/// node may force an O(log n) chain of suspended spine repairs before memoizing);
/// <see cref="Prepend"/>/<see cref="Append"/> and the view operations are O(1) amortized, O(log n) worst-case;
/// <see cref="Concat"/> is O(log(min(n, m))) amortized; <see cref="Split(Func{TMeasure, bool})"/> and
/// <see cref="TrySplitFind"/> are O(log(min(k, n − k))) amortized for a boundary at distance k from the
/// nearer end. The middle subtree of each deep node is held behind a memoized suspension and each node's
/// measure is lazily memoized — Hinze and Paterson's lazy finger tree realized in a strict language — so these
/// amortized bounds hold under fully persistent (branching) histories, not merely ephemeral linear use. Because
/// a general monoid has no inverse, a popped subtree's measure cannot be recovered by subtraction (the deque's
/// trick for its size measure), which is why <see cref="Measure"/> is amortized rather than worst-case O(1).
/// </para>
/// <para>Instances are immutable and safe for concurrent reads.</para>
/// </remarks>
/// <example>
/// A positional sequence and an index split. With <see cref="SizeMeasure{TElement}"/> the accumulated
/// measure is the prefix length, so <c>measured &gt; k</c> first holds once the element at zero-based index
/// <c>k</c> is included — that element heads <c>Right</c>, leaving exactly <c>k</c> elements in <c>Left</c>:
/// <code>
/// var seq = FingerTree&lt;int, int, SizeMeasure&lt;int&gt;&gt;.Create(10, 20, 30, 40);
/// var (left, right) = seq.Split(measured =&gt; measured &gt; 2);   // Left keeps the first 2 elements
/// // left  is [10, 20]
/// // right is [30, 40]   (the element at index 2 begins Right)
/// </code>
/// </example>
public sealed class FingerTree<TElement, TMeasure, TMeasureOps>
    : IEnumerable<TElement>
    where TMeasureOps : IMeasure<TElement, TMeasure>
{
    private static readonly FingerTree<TElement, TMeasure, TMeasureOps> EmptyInstance =
        new(MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps>.Empty);

    private readonly MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps> _root;

    private FingerTree(MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps> root) =>
        _root = root;

    /// <summary>Gets the empty tree.</summary>
    public static FingerTree<TElement, TMeasure, TMeasureOps> Empty => EmptyInstance;

    /// <summary>Gets a value indicating whether the tree has no elements. O(1).</summary>
    public bool IsEmpty => _root.IsEmpty;

    /// <summary>
    /// Gets the combined measure of all elements, or <c>TMeasureOps.Empty</c> when the tree is empty. O(1).
    /// </summary>
    public TMeasure Measure => _root.Measure;

    /// <summary>Gets the first element. O(1).</summary>
    /// <exception cref="InvalidOperationException">The tree is empty.</exception>
    public TElement First => _root.IsEmpty ? throw EmptyError() : _root.First.Value;

    /// <summary>Gets the last element. O(1).</summary>
    /// <exception cref="InvalidOperationException">The tree is empty.</exception>
    public TElement Last => _root.IsEmpty ? throw EmptyError() : _root.Last.Value;

    /// <summary>Creates a tree from the given elements in order. O(n).</summary>
    /// <param name="values">Elements to store.</param>
    /// <returns>A tree containing <paramref name="values"/> in order.</returns>
    public static FingerTree<TElement, TMeasure, TMeasureOps> Create(params ReadOnlySpan<TElement> values)
    {
        var root = MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps>.Empty;
        foreach (var value in values)
            root = root.Snoc(new MeasuredLeaf<TElement, TMeasure, TMeasureOps>(value));
        return Wrap(root);
    }

    /// <summary>Creates a tree from an enumerable source, enumerated once. O(n).</summary>
    /// <param name="values">Elements to store.</param>
    /// <returns>A tree containing the enumeration results in order.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="values"/> is <see langword="null"/>.</exception>
    public static FingerTree<TElement, TMeasure, TMeasureOps> CreateRange(IEnumerable<TElement> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        if (values is FingerTree<TElement, TMeasure, TMeasureOps> existing)
            return existing;

        var root = MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps>.Empty;
        foreach (var value in values)
            root = root.Snoc(new MeasuredLeaf<TElement, TMeasure, TMeasureOps>(value));
        return Wrap(root);
    }

    /// <summary>Returns a tree with <paramref name="value"/> prepended. O(1) amortized.</summary>
    /// <param name="value">Element to prepend.</param>
    public FingerTree<TElement, TMeasure, TMeasureOps> Prepend(TElement value) =>
        new(_root.Cons(new MeasuredLeaf<TElement, TMeasure, TMeasureOps>(value)));

    /// <summary>Returns a tree with <paramref name="value"/> appended. O(1) amortized.</summary>
    /// <param name="value">Element to append.</param>
    public FingerTree<TElement, TMeasure, TMeasureOps> Append(TElement value) =>
        new(_root.Snoc(new MeasuredLeaf<TElement, TMeasure, TMeasureOps>(value)));

    /// <summary>Concatenates this tree with <paramref name="other"/>. O(log(min(n, m))) amortized.</summary>
    /// <param name="other">Tree whose elements follow this tree's elements.</param>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public FingerTree<TElement, TMeasure, TMeasureOps> Concat(FingerTree<TElement, TMeasure, TMeasureOps> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (other._root.IsEmpty)
            return this;
        if (_root.IsEmpty)
            return other;
        return new(_root.Concat(other._root));
    }

    /// <summary>Attempts to remove the first element. O(1) amortized.</summary>
    /// <param name="head">The removed first element when present; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining tree, or <see cref="Empty"/>.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public bool TryViewLeft(out TElement head, out FingerTree<TElement, TMeasure, TMeasureOps> rest)
    {
        if (_root.TryViewLeft(out var leaf, out var remaining))
        {
            head = leaf.Value;
            rest = Wrap(remaining);
            return true;
        }

        head = default!;
        rest = EmptyInstance;
        return false;
    }

    /// <summary>Attempts to remove the last element. O(1) amortized.</summary>
    /// <param name="last">The removed last element when present; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining tree, or <see cref="Empty"/>.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public bool TryViewRight(out TElement last, out FingerTree<TElement, TMeasure, TMeasureOps> rest)
    {
        if (_root.TryViewRight(out var leaf, out var remaining))
        {
            last = leaf.Value;
            rest = Wrap(remaining);
            return true;
        }

        last = default!;
        rest = EmptyInstance;
        return false;
    }

    /// <summary>
    /// Splits the tree at the point where <paramref name="predicate"/>, applied to the measure accumulated
    /// from the left, first becomes true.
    /// </summary>
    /// <param name="predicate">
    /// A monotone predicate over the accumulated measure: once true for some prefix measure it must stay
    /// true for every longer prefix. The result is specified only for monotone predicates.
    /// </param>
    /// <returns>
    /// <c>Left</c> is the longest prefix whose accumulated measure never satisfies <paramref name="predicate"/>;
    /// <c>Right</c> is the remaining suffix, whose head is the boundary element (the first whose inclusion
    /// makes the predicate true). <c>Left.Concat(Right)</c> reproduces the original sequence.
    /// </returns>
    /// <remarks>O(log(min(k, n − k))) amortized, where k is the boundary position.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="predicate"/> is <see langword="null"/>.</exception>
    public (FingerTree<TElement, TMeasure, TMeasureOps> Left, FingerTree<TElement, TMeasure, TMeasureOps> Right)
        Split(Func<TMeasure, bool> predicate)
    {
        ArgumentNullException.ThrowIfNull(predicate);
        return Split(new FuncMeasurePredicate<TMeasure>(predicate));
    }

    /// <summary>
    /// Splits the tree using a value-type predicate instead of a delegate, so the split builds its result trees
    /// without also allocating a predicate closure. Otherwise identical to
    /// <see cref="Split(Func{TMeasure, bool})"/>.
    /// </summary>
    /// <typeparam name="TPredicate">A value-type monotone predicate over the measure.</typeparam>
    /// <param name="predicate">The monotone split predicate.</param>
    /// <returns>The longest prefix not satisfying <paramref name="predicate"/>, and the remaining suffix.</returns>
    public (FingerTree<TElement, TMeasure, TMeasureOps> Left, FingerTree<TElement, TMeasure, TMeasureOps> Right)
        Split<TPredicate>(TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        if (_root.IsEmpty || !predicate.Invoke(_root.Measure))
            return (this, EmptyInstance);

        var (left, hit, right) = _root.SplitTree(predicate, TMeasureOps.Empty);
        return (Wrap(left), Wrap(right.Cons(hit)));
    }

    /// <summary>
    /// Splits the tree around the single element at which <paramref name="predicate"/> over the accumulated
    /// measure first becomes true, returning that element separately.
    /// </summary>
    /// <param name="predicate">Monotone predicate over the accumulated measure.</param>
    /// <param name="left">Elements before the boundary element.</param>
    /// <param name="found">The boundary element — the first whose inclusion makes the predicate true.</param>
    /// <param name="right">Elements after the boundary element.</param>
    /// <returns>
    /// <see langword="true"/> when some prefix satisfies <paramref name="predicate"/> (so a boundary element
    /// exists); otherwise <see langword="false"/>, with <paramref name="left"/> set to the whole tree.
    /// </returns>
    /// <remarks>
    /// O(log(min(k, n − k))) amortized. For an ordered tree this is "find the first element whose key
    /// reaches a threshold"; for a priority queue with a maximum measure, <c>predicate = m =&gt; ... &gt;= Measure</c>
    /// locates the maximum.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="predicate"/> is <see langword="null"/>.</exception>
    /// <example>
    /// <code>
    /// // Priority queue: extract an occurrence of the maximum.
    /// var pq = FingerTree&lt;int, int, MaxPriority&gt;.Create(3, 9, 1, 9, 4);
    /// if (pq.TrySplitFind(m =&gt; m &gt;= pq.Measure, out var lo, out var max, out var hi))
    /// {
    ///     var withoutOneMax = lo.Concat(hi);   // max removed (max == pq.Measure)
    /// }
    /// </code>
    /// </example>
    public bool TrySplitFind(
        Func<TMeasure, bool> predicate,
        out FingerTree<TElement, TMeasure, TMeasureOps> left,
        out TElement found,
        out FingerTree<TElement, TMeasure, TMeasureOps> right)
    {
        ArgumentNullException.ThrowIfNull(predicate);
        return TrySplitFind(new FuncMeasurePredicate<TMeasure>(predicate), out left, out found, out right);
    }

    /// <summary>
    /// Splits around the boundary element using a value-type predicate instead of a delegate, so no predicate
    /// closure is allocated. Otherwise identical to
    /// <see cref="TrySplitFind(Func{TMeasure, bool}, out FingerTree{TElement, TMeasure, TMeasureOps}, out TElement, out FingerTree{TElement, TMeasure, TMeasureOps})"/>.
    /// </summary>
    /// <typeparam name="TPredicate">A value-type monotone predicate over the measure.</typeparam>
    /// <param name="predicate">The monotone split predicate.</param>
    /// <param name="left">Elements before the boundary element.</param>
    /// <param name="found">The boundary element when one exists; otherwise <see langword="default"/>.</param>
    /// <param name="right">Elements after the boundary element.</param>
    /// <returns><see langword="true"/> when a boundary element exists; otherwise <see langword="false"/>.</returns>
    public bool TrySplitFind<TPredicate>(
        TPredicate predicate,
        out FingerTree<TElement, TMeasure, TMeasureOps> left,
        out TElement found,
        out FingerTree<TElement, TMeasure, TMeasureOps> right)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        if (_root.IsEmpty || !predicate.Invoke(_root.Measure))
        {
            left = this;
            found = default!;
            right = EmptyInstance;
            return false;
        }

        var (leftTree, hit, rightTree) = _root.SplitTree(predicate, TMeasureOps.Empty);
        left = Wrap(leftTree);
        found = hit.Value;
        right = Wrap(rightTree);
        return true;
    }

    /// <summary>
    /// Locates the boundary element where <paramref name="predicate"/> over the accumulated measure first
    /// becomes true, returning it and the measure accumulated strictly before it — <em>without</em>
    /// reconstructing the surrounding subtrees. This is the allocation-free read counterpart to
    /// <see cref="TrySplitFind"/>: use it for membership, rank, order-statistic, and neighbor queries that need
    /// only the boundary element or the preceding measure, not the split halves.
    /// </summary>
    /// <param name="predicate">Monotone predicate over the accumulated measure (false then true).</param>
    /// <param name="measureBefore">
    /// The measure accumulated strictly before the boundary element when one is found; otherwise the measure of
    /// the whole tree (so a rank query reads the full count when no element satisfies the predicate).
    /// </param>
    /// <param name="found">The boundary element when one exists; otherwise <see langword="default"/>.</param>
    /// <returns>
    /// <see langword="true"/> when some prefix satisfies <paramref name="predicate"/> (a boundary element
    /// exists); otherwise <see langword="false"/>.
    /// </returns>
    /// <remarks>O(log(min(k, n − k))) amortized, allocating nothing beyond forcing already-deferred spine
    /// repairs along the descended path. Unlike <see cref="TrySplitFind"/> it builds no result trees, so it is
    /// the right tool for hot read paths over the sorted and order-statistic collections.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="predicate"/> is <see langword="null"/>.</exception>
    public bool TryLocate(Func<TMeasure, bool> predicate, out TMeasure measureBefore, [MaybeNullWhen(false)] out TElement found)
    {
        ArgumentNullException.ThrowIfNull(predicate);
        return TryLocate(new FuncMeasurePredicate<TMeasure>(predicate), out measureBefore, out found);
    }

    /// <summary>
    /// The allocation-free locate over a value-type predicate: the same boundary search as
    /// <see cref="TryLocate(Func{TMeasure, bool}, out TMeasure, out TElement)"/> but with the predicate supplied
    /// as a constrained struct, so the runtime monomorphizes the descent and it allocates nothing at all.
    /// Supply a non-capturing <see cref="IMeasurePredicate{TMeasure}"/> struct to write a zero-allocation
    /// membership, rank, order-statistic, or neighbor query; the library's own sorted, order-statistic,
    /// priority-queue, and interval collections use this overload internally.
    /// </summary>
    /// <typeparam name="TPredicate">A value-type monotone predicate over the measure.</typeparam>
    /// <param name="predicate">The boundary predicate.</param>
    /// <param name="measureBefore">The measure before the boundary element, or the whole-tree measure when none.</param>
    /// <param name="found">The boundary element when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a boundary element exists; otherwise <see langword="false"/>.</returns>
    /// <example>
    /// <code>
    /// readonly struct AtLeast(int threshold) : IMeasurePredicate&lt;int&gt;
    /// {
    ///     public bool Invoke(int runningTotal) =&gt; runningTotal &gt;= threshold;
    /// }
    ///
    /// var tree = FingerTree&lt;int, int, SumMeasure&lt;int&gt;&gt;.Create(5, 1, 4, 2);
    /// tree.TryLocate(new AtLeast(7), out var before, out var element);   // element = 4, before = 6; no allocation
    /// </code>
    /// </example>
    public bool TryLocate<TPredicate>(TPredicate predicate, out TMeasure measureBefore, [MaybeNullWhen(false)] out TElement found)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        if (_root.IsEmpty || !predicate.Invoke(_root.Measure))
        {
            measureBefore = _root.Measure;
            found = default;
            return false;
        }

        var (before, hit) = _root.LocateTree(predicate, TMeasureOps.Empty);
        measureBefore = before;
        found = hit.Value;
        return true;
    }

    /// <summary>Copies the elements to a new array in order. O(n).</summary>
    /// <returns>A new array of the elements in order.</returns>
    public TElement[] ToArray()
    {
        if (_root.IsEmpty)
            return [];
        var list = new List<TElement>();
        _root.Flatten(list);
        return [.. list];
    }

    /// <summary>Returns an enumerator over the elements in order.</summary>
    /// <returns>An enumerator yielding elements left to right.</returns>
    /// <remarks>
    /// Enumeration is O(n) total with O(1) amortized cost per yielded element; the enumerator keeps an O(log n)
    /// traversal stack and does not materialize the tree before yielding.
    /// </remarks>
    public Enumerator GetEnumerator() => new(_root);

    IEnumerator<TElement> IEnumerable<TElement>.GetEnumerator() => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private static FingerTree<TElement, TMeasure, TMeasureOps> Wrap(
        MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps> root) =>
        root.IsEmpty ? EmptyInstance : new(root);

    private static InvalidOperationException EmptyError() => new("The finger tree is empty.");

    /// <summary>
    /// Enumerates a <see cref="FingerTree{TElement, TMeasure, TMeasureOps}"/> without allocating a boxed
    /// enumerator in pattern-based foreach.
    /// </summary>
    /// <remarks>
    /// Maintains an explicit traversal stack of O(log n) frames, yielding from the immutable snapshot captured
    /// when the enumerator was created.
    /// </remarks>
    public struct Enumerator : IEnumerator<TElement>
    {
        private Frame[]? _stack;
        private int _depth;
        private TElement _current;

        internal Enumerator(MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps> root)
        {
            _stack = null;
            _depth = 0;
            _current = default!;
            if (!root.IsEmpty)
                Push(root);
        }

        /// <summary>Gets the current element.</summary>
        /// <remarks>Undefined before the first call to <see cref="MoveNext"/> and after enumeration ends.</remarks>
        public readonly TElement Current => _current;

        readonly object? IEnumerator.Current => Current;

        /// <summary>Releases resources held by the enumerator.</summary>
        public void Dispose()
        {
        }

        /// <summary>Advances to the next element.</summary>
        /// <returns><see langword="true"/> when the enumerator advanced to an element; otherwise <see langword="false"/>.</returns>
        public bool MoveNext()
        {
            while (_depth > 0)
            {
                ref var frame = ref _stack![_depth - 1];
                if (frame.NextChild == frame.Block.ChildCount)
                {
                    frame = default;
                    _depth--;
                    continue;
                }

                var block = frame.Block;
                if (block.TryGetChild(frame.NextChild++, out var leaf, out var child))
                {
                    _current = leaf;
                    return true;
                }

                Push(child!);
            }

            _current = default!;
            return false;
        }

        /// <summary>
        /// Not supported; the enumerator cannot be reset. Create a new enumerator instead.
        /// </summary>
        /// <exception cref="NotSupportedException">Always thrown.</exception>
        void IEnumerator.Reset() => throw new NotSupportedException();

        private void Push(IEnumerationBlock<TElement> block)
        {
            _stack ??= new Frame[8];
            if (_depth == _stack.Length)
                Array.Resize(ref _stack, _depth * 2);
            _stack[_depth++] = new Frame(block);
        }

        /// <summary>One traversal-stack entry: a block and the index of its next unvisited child.</summary>
        /// <param name="block">The block being traversed at this depth.</param>
        private struct Frame(IEnumerationBlock<TElement> block)
        {
            public readonly IEnumerationBlock<TElement> Block = block;
            public int NextChild = 0;
        }
    }
}
