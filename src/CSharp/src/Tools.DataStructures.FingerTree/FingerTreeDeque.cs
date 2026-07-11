using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// Represents an immutable persistent catenable deque backed by a finger tree.
/// </summary>
/// <typeparam name="T">Element type stored in the deque.</typeparam>
/// <remarks>
/// <para>
/// The deque is a sequence collection with efficient access at both ends, logarithmic concatenation,
/// indexed lookup and replacement, splitting by index, and logarithmic sorted search when callers keep the
/// sequence ordered by the comparer they search with. Every operation returns a new instance and leaves all
/// existing instances usable; new instances share unchanged internal structure with their inputs.
/// </para>
/// <para>
/// The representation is the simplified 2-3 finger tree (digits of one through three elements, middle nodes
/// of two or three children) with cached per-subtree leaf counts and rightmost-element signposts, and with
/// the middle subtree of each deep node held behind a memoize-on-first-force suspension — the original
/// finger-tree paper's strategy for strict languages. The full complexity contract lives in
/// <c>docs/api-specification.md</c> in this project's workspace; headline bounds: <see cref="Count"/>,
/// <see cref="First"/>, and <see cref="Last"/> are O(1) worst-case; endpoint insertion and removal are
/// O(log n) worst-case and O(1) amortized; <see cref="Concat"/> is logarithmic in the smaller operand
/// amortized; indexed access and splitting are logarithmic in the distance from the nearer end amortized.
/// Because forced suspensions are memoized and shared, the amortized bounds hold under fully persistent
/// use, including branching version histories, matching Hinze and Paterson's analysis.
/// </para>
/// <para>
/// Instances are immutable snapshots and safe for concurrent reads from multiple threads without locking.
/// Readers may race to force the same pending suspension; such races duplicate only that bounded
/// computation, publish one canonical result with a compare-exchange, and can never expose a partially
/// initialized node or two different element sequences.
/// </para>
/// <para>
/// The type intentionally does not override <see cref="object.Equals(object)"/>: structural equality is
/// O(n) and callers who need it should use <see cref="Enumerable.SequenceEqual{TSource}(IEnumerable{TSource}, IEnumerable{TSource})"/>.
/// Operations may return an existing instance when the result is observably identical, so callers must not
/// use reference identity to detect logical equality.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// var items = FingerTreeDeque&lt;int&gt;.Empty
///     .AddLast(10)
///     .AddLast(20)
///     .AddFirst(5);
///
/// var split = items.SplitAt(2);
///
/// // split.Left:  [5, 10]
/// // split.Right: [20]
/// // items is still [5, 10, 20]
/// </code>
/// </example>
public sealed class FingerTreeDeque<T> : IReadOnlyList<T>
{
    private static readonly FingerTreeDeque<T> EmptyInstance = new(EmptyTree<T, Leaf<T>>.Instance);

    private readonly Tree<T, Leaf<T>> _root;

    private FingerTreeDeque(Tree<T, Leaf<T>> root)
    {
        _root = root;
    }

    /// <summary>
    /// Gets the empty deque.
    /// </summary>
    /// <remarks>
    /// This is the canonical empty instance: operations whose result is empty return this same instance.
    /// </remarks>
    public static FingerTreeDeque<T> Empty => EmptyInstance;

    /// <summary>
    /// Gets the number of elements in the deque.
    /// </summary>
    /// <remarks>O(1); the count is cached throughout the tree.</remarks>
    public int Count => _root.Size;

    /// <summary>
    /// Gets a value indicating whether the deque contains no elements.
    /// </summary>
    /// <remarks>O(1).</remarks>
    public bool IsEmpty => _root.Size == 0;

    /// <summary>
    /// Gets the first element in the deque.
    /// </summary>
    /// <remarks>O(1) worst-case.</remarks>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public T First => IsEmpty ? throw EmptyDequeError() : _root.FirstLeaf;

    /// <summary>
    /// Gets the last element in the deque.
    /// </summary>
    /// <remarks>O(1) worst-case.</remarks>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public T Last => IsEmpty ? throw EmptyDequeError() : _root.LastLeaf;

    /// <summary>
    /// Gets the element at the specified zero-based index.
    /// </summary>
    /// <param name="index">Zero-based element index.</param>
    /// <returns>The element at <paramref name="index"/>.</returns>
    /// <remarks>
    /// O(log min(index + 1, Count - index)) amortized: logarithmic in the distance from the nearer end. A
    /// single call may additionally force deferred spine work, bounded by O(log n) and memoized for all
    /// later readers.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public T this[int index]
    {
        get
        {
            if ((uint)index >= (uint)Count)
                throw IndexRangeError(index);
            return _root.GetLeaf(index);
        }
    }

    /// <summary>
    /// Creates a deque containing the specified items in order.
    /// </summary>
    /// <param name="items">Items to store in the deque.</param>
    /// <returns>A deque containing <paramref name="items"/> in the same order.</returns>
    /// <remarks>O(items.Length) total.</remarks>
    public static FingerTreeDeque<T> Create(params ReadOnlySpan<T> items)
    {
        Tree<T, Leaf<T>> root = EmptyTree<T, Leaf<T>>.Instance;
        foreach (var item in items)
            root = root.Snoc(new Leaf<T>(item));
        return Wrap(root);
    }

    /// <summary>
    /// Creates a deque from an enumerable source.
    /// </summary>
    /// <param name="items">Items to store in the deque.</param>
    /// <returns>A deque containing the enumeration results in order.</returns>
    /// <remarks>
    /// Enumerates <paramref name="items"/> exactly once; O(n) total. When <paramref name="items"/> is
    /// already a <see cref="FingerTreeDeque{T}"/>, the same instance is returned without enumeration.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The enumeration yields more than <see cref="int.MaxValue"/> elements.</exception>
    public static FingerTreeDeque<T> CreateRange(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items is FingerTreeDeque<T> deque)
            return deque;
        return Wrap(BuildTree(items, int.MaxValue));
    }

    /// <summary>
    /// Adds an item to the left end of the deque.
    /// </summary>
    /// <param name="item">Item to add.</param>
    /// <returns>A deque with <paramref name="item"/> prepended.</returns>
    /// <remarks>
    /// O(log n) worst-case, O(1) amortized; the amortized bound holds under fully persistent use,
    /// including branching version histories.
    /// </remarks>
    /// <exception cref="OverflowException">The deque already holds <see cref="int.MaxValue"/> elements.</exception>
    public FingerTreeDeque<T> AddFirst(T item)
    {
        CheckRoomForOneMore();
        return new(_root.Cons(new Leaf<T>(item)));
    }

    /// <summary>
    /// Adds an item to the right end of the deque.
    /// </summary>
    /// <param name="item">Item to add.</param>
    /// <returns>A deque with <paramref name="item"/> appended.</returns>
    /// <remarks>
    /// O(log n) worst-case, O(1) amortized; the amortized bound holds under fully persistent use,
    /// including branching version histories.
    /// </remarks>
    /// <exception cref="OverflowException">The deque already holds <see cref="int.MaxValue"/> elements.</exception>
    public FingerTreeDeque<T> AddLast(T item)
    {
        CheckRoomForOneMore();
        return new(_root.Snoc(new Leaf<T>(item)));
    }

    /// <summary>
    /// Appends the specified enumerable source to this deque.
    /// </summary>
    /// <param name="items">Items to append.</param>
    /// <returns>A deque containing this deque followed by <paramref name="items"/>.</returns>
    /// <remarks>Enumerates <paramref name="items"/> exactly once; O(m + log n) total for m appended items.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The combined count would exceed <see cref="int.MaxValue"/>.</exception>
    public FingerTreeDeque<T> AddRange(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items is FingerTreeDeque<T> deque)
            return ConcatCore(deque);

        var root = _root;
        foreach (var item in items)
        {
            if (root.Size == int.MaxValue)
                throw CountOverflowError();
            root = root.Snoc(new Leaf<T>(item));
        }

        return root == _root ? this : Wrap(root);
    }

    /// <summary>
    /// Appends another deque to this deque.
    /// </summary>
    /// <param name="items">Deque to append.</param>
    /// <returns>A deque containing this deque followed by <paramref name="items"/>.</returns>
    /// <remarks>Equivalent to <see cref="Concat"/>: O(log(min(n, m) + 1)) amortized, O(log(n + m)) worst-case.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The combined count would exceed <see cref="int.MaxValue"/>.</exception>
    public FingerTreeDeque<T> AddRange(FingerTreeDeque<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        return ConcatCore(items);
    }

    /// <summary>
    /// Concatenates this deque with another deque.
    /// </summary>
    /// <param name="other">Deque to append.</param>
    /// <returns>A deque containing this deque followed by <paramref name="other"/>.</returns>
    /// <remarks>
    /// O(log(min(n, m) + 1)) amortized — logarithmic in the smaller operand — and O(log(n + m)) worst-case
    /// for a single call when deferred spine work must be forced. Neither input is copied; the result
    /// shares structure with both. Concatenating with an empty deque returns the non-empty instance.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The combined count would exceed <see cref="int.MaxValue"/>.</exception>
    public FingerTreeDeque<T> Concat(FingerTreeDeque<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return ConcatCore(other);
    }

    /// <summary>
    /// Removes the first element.
    /// </summary>
    /// <returns>A deque containing all elements except the first.</returns>
    /// <remarks>O(log n) worst-case, O(1) amortized; the amortized bound holds under fully persistent use.</remarks>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public FingerTreeDeque<T> RemoveFirst()
    {
        if (IsEmpty)
            throw EmptyDequeError();
        return Wrap(_root.RemoveFirst(out _));
    }

    /// <summary>
    /// Removes the last element.
    /// </summary>
    /// <returns>A deque containing all elements except the last.</returns>
    /// <remarks>O(log n) worst-case, O(1) amortized; the amortized bound holds under fully persistent use.</remarks>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public FingerTreeDeque<T> RemoveLast()
    {
        if (IsEmpty)
            throw EmptyDequeError();
        return Wrap(_root.RemoveLast(out _));
    }

    /// <summary>
    /// Removes and returns the first element and the remaining suffix.
    /// </summary>
    /// <returns>The removed element and remaining suffix.</returns>
    /// <remarks>O(log n) worst-case, O(1) amortized; the amortized bound holds under fully persistent use.</remarks>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public FingerTreeDequePop<T> PopFirst()
    {
        if (IsEmpty)
            throw EmptyDequeError();
        var rest = _root.RemoveFirst(out var removed);
        return new(removed.Value, Wrap(rest));
    }

    /// <summary>
    /// Removes and returns the last element and the remaining prefix.
    /// </summary>
    /// <returns>The removed element and remaining prefix.</returns>
    /// <remarks>O(log n) worst-case, O(1) amortized; the amortized bound holds under fully persistent use.</remarks>
    /// <exception cref="InvalidOperationException">The deque is empty.</exception>
    public FingerTreeDequePop<T> PopLast()
    {
        if (IsEmpty)
            throw EmptyDequeError();
        var rest = _root.RemoveLast(out var removed);
        return new(removed.Value, Wrap(rest));
    }

    /// <summary>
    /// Attempts to read the first element.
    /// </summary>
    /// <param name="value">The first element when the deque is non-empty; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a first element exists; otherwise <see langword="false"/>.</returns>
    /// <remarks>O(1) worst-case.</remarks>
    /// <example>
    /// <code>
    /// if (deque.TryPeekFirst(out var first))
    ///     Process(first);          // deque is non-empty; First would also succeed
    /// else
    ///     HandleEmpty();           // value is default and must not be used
    /// </code>
    /// </example>
    public bool TryPeekFirst([MaybeNullWhen(false)] out T value)
    {
        if (IsEmpty)
        {
            value = default;
            return false;
        }

        value = _root.FirstLeaf;
        return true;
    }

    /// <summary>
    /// Attempts to read the last element.
    /// </summary>
    /// <param name="value">The last element when the deque is non-empty; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a last element exists; otherwise <see langword="false"/>.</returns>
    /// <remarks>O(1) worst-case.</remarks>
    /// <example>
    /// <code>
    /// if (deque.TryPeekLast(out var last))
    ///     Process(last);           // deque is non-empty; Last would also succeed
    /// </code>
    /// </example>
    public bool TryPeekLast([MaybeNullWhen(false)] out T value)
    {
        if (IsEmpty)
        {
            value = default;
            return false;
        }

        value = _root.LastLeaf;
        return true;
    }

    /// <summary>
    /// Attempts to remove and return the first element.
    /// </summary>
    /// <param name="value">The removed first element when present; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining suffix when present; otherwise <see cref="Empty"/>.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    /// <remarks>O(log n) worst-case, O(1) amortized; the amortized bound holds under fully persistent use.</remarks>
    /// <example>
    /// <code>
    /// // Drain a work queue front to back without exceptions.
    /// while (deque.TryPopFirst(out var item, out deque))
    ///     Process(item);
    /// // Here deque is the canonical Empty instance.
    /// </code>
    /// </example>
    public bool TryPopFirst([MaybeNullWhen(false)] out T value, out FingerTreeDeque<T> rest)
    {
        if (IsEmpty)
        {
            value = default;
            rest = EmptyInstance;
            return false;
        }

        rest = Wrap(_root.RemoveFirst(out var removed));
        value = removed.Value;
        return true;
    }

    /// <summary>
    /// Attempts to remove and return the last element.
    /// </summary>
    /// <param name="value">The removed last element when present; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining prefix when present; otherwise <see cref="Empty"/>.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    /// <remarks>O(log n) worst-case, O(1) amortized; the amortized bound holds under fully persistent use.</remarks>
    /// <example>
    /// <code>
    /// // Unwind a stack-like history from the back.
    /// while (deque.TryPopLast(out var item, out deque))
    ///     Undo(item);
    /// </code>
    /// </example>
    public bool TryPopLast([MaybeNullWhen(false)] out T value, out FingerTreeDeque<T> rest)
    {
        if (IsEmpty)
        {
            value = default;
            rest = EmptyInstance;
            return false;
        }

        rest = Wrap(_root.RemoveLast(out var removed));
        value = removed.Value;
        return true;
    }

    /// <summary>
    /// Attempts to read an indexed element.
    /// </summary>
    /// <param name="index">Zero-based element index.</param>
    /// <param name="value">The indexed element when found; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when <paramref name="index"/> is valid; otherwise <see langword="false"/>.</returns>
    /// <remarks>O(log min(index + 1, Count - index)) amortized; O(log n) worst-case for a single call.</remarks>
    /// <example>
    /// <code>
    /// // Probe an index that may be out of range without catching exceptions.
    /// if (deque.TryGetItem(cursor, out var item))
    ///     Render(item);
    /// else
    ///     RenderPlaceholder();     // cursor was negative or past the end
    /// </code>
    /// </example>
    public bool TryGetItem(int index, [MaybeNullWhen(false)] out T value)
    {
        if ((uint)index >= (uint)Count)
        {
            value = default;
            return false;
        }

        value = _root.GetLeaf(index);
        return true;
    }

    /// <summary>
    /// Replaces an indexed element.
    /// </summary>
    /// <param name="index">Zero-based element index.</param>
    /// <param name="value">Replacement value.</param>
    /// <returns>A deque with the indexed element replaced.</returns>
    /// <remarks>
    /// O(log min(index + 1, Count - index)) amortized; replacing an endpoint element is O(1) worst-case
    /// because endpoint digits are never behind a suspension. The stored value is always replaced, even
    /// when it compares equal to the old value.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public FingerTreeDeque<T> SetItem(int index, T value)
    {
        if ((uint)index >= (uint)Count)
            throw IndexRangeError(index);
        return new(_root.SetLeaf(index, value));
    }

    /// <summary>
    /// Updates an indexed element by applying a delegate to its current value.
    /// </summary>
    /// <param name="index">Zero-based element index.</param>
    /// <param name="updater">Delegate that maps the old value to the new value.</param>
    /// <returns>A deque with the indexed element replaced by the delegate result.</returns>
    /// <remarks>
    /// Calls <paramref name="updater"/> exactly once. If it throws, the original deque is unchanged.
    /// O(log min(index + 1, Count - index)) amortized.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="updater"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public FingerTreeDeque<T> UpdateAt(int index, Func<T, T> updater)
    {
        ArgumentNullException.ThrowIfNull(updater);
        if ((uint)index >= (uint)Count)
            throw IndexRangeError(index);
        return new(_root.SetLeaf(index, updater(_root.GetLeaf(index))));
    }

    /// <summary>
    /// Inserts an element at the specified index.
    /// </summary>
    /// <param name="index">Insertion index in the inclusive range from zero through <see cref="Count"/>.</param>
    /// <param name="item">Item to insert.</param>
    /// <returns>A deque with <paramref name="item"/> inserted at <paramref name="index"/>.</returns>
    /// <remarks>
    /// O(log(min(index, Count - index) + 1)) amortized: logarithmic in the distance from the nearer end.
    /// O(log n) worst-case for a single call.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    /// <exception cref="OverflowException">The deque already holds <see cref="int.MaxValue"/> elements.</exception>
    public FingerTreeDeque<T> InsertAt(int index, T item)
    {
        if ((uint)index > (uint)Count)
            throw IndexRangeError(index);
        if (index == 0)
            return AddFirst(item);
        if (index == Count)
            return AddLast(item);

        CheckRoomForOneMore();
        _root.SplitChild(index, out var left, out var hit, out _, out var right);
        return new(TreeOperations.Concat(left.Snoc(new Leaf<T>(item)), right.Cons(hit)));
    }

    /// <summary>
    /// Inserts a range at the specified index.
    /// </summary>
    /// <param name="index">Insertion index in the inclusive range from zero through <see cref="Count"/>.</param>
    /// <param name="items">Items to insert.</param>
    /// <returns>A deque with <paramref name="items"/> inserted at <paramref name="index"/>.</returns>
    /// <remarks>Enumerates <paramref name="items"/> exactly once; O(m + log n) total for m inserted items.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    /// <exception cref="OverflowException">The combined count would exceed <see cref="int.MaxValue"/>.</exception>
    public FingerTreeDeque<T> InsertRange(int index, IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if ((uint)index > (uint)Count)
            throw IndexRangeError(index);

        var inserted = items is FingerTreeDeque<T> deque ? deque._root : BuildTree(items, int.MaxValue - Count);
        if (inserted.Size == 0)
            return this;
        if (inserted.Size > int.MaxValue - Count)
            throw CountOverflowError();
        if (index == 0)
            return new(TreeOperations.Concat(inserted, _root));
        if (index == Count)
            return new(TreeOperations.Concat(_root, inserted));

        _root.SplitChild(index, out var left, out var hit, out _, out var right);
        return new(TreeOperations.Concat(TreeOperations.Concat(left, inserted), right.Cons(hit)));
    }

    /// <summary>
    /// Removes one indexed element.
    /// </summary>
    /// <param name="index">Zero-based element index.</param>
    /// <returns>A deque without the indexed element.</returns>
    /// <remarks>O(log min(index + 1, Count - index)) amortized; O(log n) worst-case for a single call.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public FingerTreeDeque<T> RemoveAt(int index)
    {
        if ((uint)index >= (uint)Count)
            throw IndexRangeError(index);
        _root.SplitChild(index, out var left, out _, out _, out var right);
        return Wrap(TreeOperations.Concat(left, right));
    }

    /// <summary>
    /// Removes a contiguous indexed range.
    /// </summary>
    /// <param name="index">Zero-based start index.</param>
    /// <param name="count">Number of elements to remove.</param>
    /// <returns>A deque without the specified range.</returns>
    /// <remarks>O(log n).</remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="index"/> or <paramref name="count"/> is negative, or the range extends past the end.
    /// </exception>
    public FingerTreeDeque<T> RemoveRange(int index, int count)
    {
        CheckRange(index, count);
        if (count == 0)
            return this;
        if (count == Count)
            return EmptyInstance;
        var (left, rest) = SplitTree(_root, index);
        var (_, right) = SplitTree(rest, count);
        return Wrap(TreeOperations.Concat(left, right));
    }

    /// <summary>
    /// Extracts a contiguous indexed range.
    /// </summary>
    /// <param name="index">Zero-based start index.</param>
    /// <param name="count">Number of elements to extract.</param>
    /// <returns>A deque containing the specified range.</returns>
    /// <remarks>O(log n). Extracting the whole deque returns the same instance.</remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="index"/> or <paramref name="count"/> is negative, or the range extends past the end.
    /// </exception>
    public FingerTreeDeque<T> GetRange(int index, int count)
    {
        CheckRange(index, count);
        if (count == 0)
            return EmptyInstance;
        if (count == Count)
            return this;
        var (_, rest) = SplitTree(_root, index);
        var (range, _) = SplitTree(rest, count);
        return Wrap(range);
    }

    /// <summary>
    /// Splits the deque at an index.
    /// </summary>
    /// <param name="index">Split index in the inclusive range from zero through <see cref="Count"/>.</param>
    /// <returns>The prefix before <paramref name="index"/> and the suffix starting at <paramref name="index"/>.</returns>
    /// <remarks>
    /// O(log(min(index, Count - index) + 1)) amortized: logarithmic in the smaller resulting side, with an
    /// O(log n) worst-case for a single call. Both results share structure with the original, which remains
    /// unchanged.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public FingerTreeDequeSplit<T> SplitAt(int index)
    {
        if ((uint)index > (uint)Count)
            throw IndexRangeError(index);
        if (index == 0)
            return new(EmptyInstance, this);
        if (index == Count)
            return new(this, EmptyInstance);
        var (left, right) = SplitTree(_root, index);
        return new(Wrap(left), Wrap(right));
    }

    /// <summary>
    /// Splits the deque around one indexed item.
    /// </summary>
    /// <param name="index">Zero-based item index.</param>
    /// <returns>The prefix, indexed item, and suffix.</returns>
    /// <remarks>O(log min(index + 1, Count - index)) amortized; O(log n) worst-case for a single call.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the valid range.</exception>
    public FingerTreeDequeItemSplit<T> SplitItemAt(int index)
    {
        if ((uint)index >= (uint)Count)
            throw IndexRangeError(index);
        _root.SplitChild(index, out var left, out var hit, out _, out var right);
        return new(Wrap(left), hit.Value, Wrap(right));
    }

    /// <summary>Locates and removes one comparer-equal sorted item in a single tree descent.</summary>
    internal bool TryRemoveSortedItem(
        T item,
        IComparer<T> comparer,
        out int index,
        [MaybeNullWhen(false)] out T stored,
        out FingerTreeDeque<T> result)
    {
        if (!TrySplitSortedItem(item, comparer, out var left, out var hit, out var right))
        {
            index = -1;
            stored = default;
            result = this;
            return false;
        }

        index = left.Size;
        stored = hit.Value;
        result = Wrap(TreeOperations.Concat(left, right));
        return true;
    }

    /// <summary>Locates and updates one comparer-equal sorted item in a single tree descent.</summary>
    internal bool TryUpdateSortedItem(
        T item,
        IComparer<T> comparer,
        Func<T, T> updater,
        [MaybeNullWhen(false)] out T stored,
        out FingerTreeDeque<T> result)
    {
        ArgumentNullException.ThrowIfNull(updater);
        if (!TrySplitSortedItem(item, comparer, out var left, out var hit, out var right))
        {
            stored = default;
            result = this;
            return false;
        }

        stored = hit.Value;
        var replacement = new Leaf<T>(updater(stored));
        result = new FingerTreeDeque<T>(TreeOperations.Concat(left.Snoc(replacement), right));
        return true;
    }

    private bool TrySplitSortedItem(
        T item,
        IComparer<T> comparer,
        out Tree<T, Leaf<T>> left,
        out Leaf<T> hit,
        out Tree<T, Leaf<T>> right)
    {
        if (!_root.TrySplitBoundChild(value => comparer.Compare(value, item) >= 0, out left, out hit, out right) ||
            comparer.Compare(hit.Value, item) != 0)
        {
            left = _root;
            hit = default;
            right = EmptyTree<T, Leaf<T>>.Instance;
            return false;
        }

        return true;
    }

    /// <summary>
    /// Splits the deque around a contiguous indexed range.
    /// </summary>
    /// <param name="index">Zero-based range start index.</param>
    /// <param name="count">Number of elements in the middle range.</param>
    /// <returns>The prefix, middle range, and suffix.</returns>
    /// <remarks>O(log n).</remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="index"/> or <paramref name="count"/> is negative, or the range extends past the end.
    /// </exception>
    public FingerTreeDequeRangeSplit<T> SplitRange(int index, int count)
    {
        CheckRange(index, count);
        var (before, rest) = SplitTree(_root, index);
        var (range, after) = SplitTree(rest, count);
        return new(Wrap(before), Wrap(range), Wrap(after));
    }

    /// <summary>
    /// Searches a sorted deque for the first element greater than or equal to the specified item.
    /// </summary>
    /// <param name="item">Search item.</param>
    /// <param name="comparer">Comparer defining the sorted order, or <see langword="null"/> for the default comparer.</param>
    /// <returns>The lower-bound index, or <see cref="Count"/> when every element is less than <paramref name="item"/>.</returns>
    /// <remarks>
    /// The result is specified only when the deque is sorted in nondecreasing order by the same comparer.
    /// O(log min(k + 1, Count - k + 1)) comparer calls worst-case for result index k, using cached
    /// rightmost-element signposts to skip whole subtrees. Exceptions thrown by the comparer — including
    /// the default comparer's failure for element types that are not comparable — propagate to the caller.
    /// </remarks>
    public int SortedLowerBound(T item, IComparer<T>? comparer = null)
    {
        var effective = comparer ?? Comparer<T>.Default;
        return _root.BoundIndex(value => effective.Compare(value, item) >= 0);
    }

    /// <summary>
    /// Searches a sorted deque for the first element greater than the specified item.
    /// </summary>
    /// <param name="item">Search item.</param>
    /// <param name="comparer">Comparer defining the sorted order, or <see langword="null"/> for the default comparer.</param>
    /// <returns>The upper-bound index, or <see cref="Count"/> when no element is greater than <paramref name="item"/>.</returns>
    /// <remarks>
    /// The result is specified only when the deque is sorted in nondecreasing order by the same comparer.
    /// O(log min(k + 1, Count - k + 1)) comparer calls worst-case for result index k. Exceptions thrown by
    /// the comparer — including the default comparer's failure for element types that are not comparable —
    /// propagate to the caller.
    /// </remarks>
    public int SortedUpperBound(T item, IComparer<T>? comparer = null)
    {
        var effective = comparer ?? Comparer<T>.Default;
        return _root.BoundIndex(value => effective.Compare(value, item) > 0);
    }

    /// <summary>
    /// Searches a sorted deque using deterministic lower-bound duplicate semantics.
    /// </summary>
    /// <param name="item">Search item.</param>
    /// <param name="comparer">Comparer defining the sorted order, or <see langword="null"/> for the default comparer.</param>
    /// <returns>The first equal index, or the bitwise complement of the insertion index.</returns>
    /// <remarks>
    /// Unlike <see cref="Array.BinarySearch(Array, object?)"/>, the matching duplicate is deterministic: the
    /// first equal element is returned. The result is specified only when the deque is sorted by the same
    /// comparer. O(log n) amortized. Exceptions thrown by the comparer — including the default comparer's
    /// failure for element types that are not comparable — propagate to the caller.
    /// </remarks>
    /// <example>
    /// <code>
    /// var sorted = FingerTreeDeque&lt;int&gt;.Create(1, 3, 3, 7, 9);
    ///
    /// var first = sorted.SortedBinarySearch(3);   // 1: index of the FIRST 3
    /// var missing = sorted.SortedBinarySearch(4); // ~3: complement of the insertion index
    ///
    /// if (missing &lt; 0)
    ///     sorted = sorted.InsertAt(~missing, 4);  // [1, 3, 3, 4, 7, 9]
    /// </code>
    /// </example>
    public int SortedBinarySearch(T item, IComparer<T>? comparer = null)
    {
        var effective = comparer ?? Comparer<T>.Default;
        var lower = SortedLowerBound(item, effective);
        if (lower < Count && effective.Compare(_root.GetLeaf(lower), item) == 0)
            return lower;
        return ~lower;
    }

    /// <summary>
    /// Determines whether a sorted deque contains an equal element.
    /// </summary>
    /// <param name="item">Search item.</param>
    /// <param name="comparer">Comparer defining the sorted order, or <see langword="null"/> for the default comparer.</param>
    /// <returns><see langword="true"/> when an equal element exists; otherwise <see langword="false"/>.</returns>
    /// <remarks>
    /// The result is specified only when the deque is sorted by the same comparer. O(log n) amortized.
    /// Exceptions thrown by the comparer — including the default comparer's failure for element types that
    /// are not comparable — propagate to the caller.
    /// </remarks>
    public bool SortedContains(T item, IComparer<T>? comparer = null) =>
        SortedBinarySearch(item, comparer) >= 0;

    /// <summary>
    /// Splits a sorted deque at the lower bound for an item.
    /// </summary>
    /// <param name="item">Search item.</param>
    /// <param name="comparer">Comparer defining the sorted order, or <see langword="null"/> for the default comparer.</param>
    /// <returns>The elements less than the item and the elements greater than or equal to it.</returns>
    /// <remarks>
    /// The result is specified only when the deque is sorted by the same comparer. O(log n) amortized.
    /// Exceptions thrown by the comparer — including the default comparer's failure for element types that
    /// are not comparable — propagate to the caller.
    /// </remarks>
    public FingerTreeDequeSplit<T> SplitAtSortedLowerBound(T item, IComparer<T>? comparer = null) =>
        SplitAt(SortedLowerBound(item, comparer));

    /// <summary>
    /// Splits a sorted deque at the upper bound for an item.
    /// </summary>
    /// <param name="item">Search item.</param>
    /// <param name="comparer">Comparer defining the sorted order, or <see langword="null"/> for the default comparer.</param>
    /// <returns>The elements less than or equal to the item and the elements greater than it.</returns>
    /// <remarks>
    /// The result is specified only when the deque is sorted by the same comparer. O(log n) amortized.
    /// Exceptions thrown by the comparer — including the default comparer's failure for element types that
    /// are not comparable — propagate to the caller.
    /// </remarks>
    public FingerTreeDequeSplit<T> SplitAtSortedUpperBound(T item, IComparer<T>? comparer = null) =>
        SplitAt(SortedUpperBound(item, comparer));

    /// <summary>
    /// Splits a sorted deque into less-than, equal, and greater-than partitions.
    /// </summary>
    /// <param name="item">Search item.</param>
    /// <param name="comparer">Comparer defining the sorted order, or <see langword="null"/> for the default comparer.</param>
    /// <returns>The less-than prefix, equal range, and greater-than suffix.</returns>
    /// <remarks>
    /// The result is specified only when the deque is sorted by the same comparer. O(log n) amortized.
    /// Exceptions thrown by the comparer — including the default comparer's failure for element types that
    /// are not comparable — propagate to the caller.
    /// </remarks>
    /// <example>
    /// <code>
    /// var sorted = FingerTreeDeque&lt;int&gt;.Create(1, 3, 3, 7, 9);
    /// var split = sorted.SplitAtSortedEqualRange(3);
    /// // split.Before: [1], split.Range: [3, 3], split.After: [7, 9]
    /// </code>
    /// </example>
    public FingerTreeDequeRangeSplit<T> SplitAtSortedEqualRange(T item, IComparer<T>? comparer = null)
    {
        var effective = comparer ?? Comparer<T>.Default;
        var lower = SortedLowerBound(item, effective);
        var upper = SortedUpperBound(item, effective);
        var (before, rest) = SplitTree(_root, lower);
        var (range, after) = SplitTree(rest, upper - lower);
        return new(Wrap(before), Wrap(range), Wrap(after));
    }

    /// <summary>
    /// Inserts an item into its upper-bound position in a sorted deque.
    /// </summary>
    /// <param name="item">Item to insert.</param>
    /// <param name="comparer">Comparer defining the sorted order, or <see langword="null"/> for the default comparer.</param>
    /// <returns>A sorted deque containing the inserted item.</returns>
    /// <remarks>
    /// Inserts after existing equal elements, preserving their relative order. The result is specified only
    /// when the deque is sorted by the same comparer. O(log n) amortized. Exceptions thrown by the comparer
    /// — including the default comparer's failure for element types that are not comparable — propagate to
    /// the caller.
    /// </remarks>
    /// <exception cref="OverflowException">The deque already holds <see cref="int.MaxValue"/> elements.</exception>
    public FingerTreeDeque<T> InsertSorted(T item, IComparer<T>? comparer = null) =>
        InsertAt(SortedUpperBound(item, comparer), item);

    /// <summary>
    /// Removes all elements equal to the specified item from a sorted deque.
    /// </summary>
    /// <param name="item">Item to remove.</param>
    /// <param name="comparer">Comparer defining the sorted order, or <see langword="null"/> for the default comparer.</param>
    /// <returns>A sorted deque with all equal elements removed.</returns>
    /// <remarks>
    /// The result is specified only when the deque is sorted by the same comparer. O(log n) amortized.
    /// Exceptions thrown by the comparer — including the default comparer's failure for element types that
    /// are not comparable — propagate to the caller.
    /// </remarks>
    public FingerTreeDeque<T> RemoveAllSorted(T item, IComparer<T>? comparer = null)
    {
        var effective = comparer ?? Comparer<T>.Default;
        var lower = SortedLowerBound(item, effective);
        var upper = SortedUpperBound(item, effective);
        if (lower == upper)
            return this;
        return RemoveRange(lower, upper - lower);
    }

    /// <summary>
    /// Copies the deque contents to a new array.
    /// </summary>
    /// <returns>A new array containing the deque elements in order.</returns>
    /// <remarks>O(n).</remarks>
    public T[] ToArray()
    {
        if (IsEmpty)
            return [];
        var array = new T[Count];
        var offset = 0;
        _root.CopyLeavesTo(array, ref offset);
        return array;
    }

    /// <summary>
    /// Copies the deque contents into an existing array.
    /// </summary>
    /// <param name="array">Destination array.</param>
    /// <param name="arrayIndex">Zero-based destination start index.</param>
    /// <remarks>
    /// O(n); allocates nothing beyond one-time forcing of any pending deferred spine work, which is
    /// memoized for later readers.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="array"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="arrayIndex"/> is negative.</exception>
    /// <exception cref="ArgumentException">The destination is too small for the deque contents.</exception>
    public void CopyTo(T[] array, int arrayIndex)
    {
        ArgumentNullException.ThrowIfNull(array);
        ArgumentOutOfRangeException.ThrowIfNegative(arrayIndex);
        if (array.Length - arrayIndex < Count)
        {
            throw new ArgumentException(
                "The destination array is too small to hold the deque contents starting at the given index.");
        }

        var offset = arrayIndex;
        _root.CopyLeavesTo(array, ref offset);
    }

    /// <summary>
    /// Gets an enumerator over the deque contents.
    /// </summary>
    /// <returns>An enumerator that yields elements from left to right.</returns>
    /// <remarks>
    /// Enumeration is O(n) total with O(1) amortized cost per yielded element; the enumerator keeps an
    /// O(log n) traversal stack.
    /// </remarks>
    public Enumerator GetEnumerator() => new(_root);

    IEnumerator<T> IEnumerable<T>.GetEnumerator() => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>
    /// Verifies every cached measure and digit-length invariant of the underlying tree, throwing
    /// <see cref="InvalidOperationException"/> on the first violation. Test-only; O(n).
    /// </summary>
    internal void ValidateInvariants()
    {
        var computed = _root.ValidateAndCount();
        if (computed != Count)
            throw new InvalidOperationException($"Root size {Count} disagrees with recomputed total {computed}.");
    }

    /// <summary>
    /// Gets the number of deep levels in the underlying tree. Test-only; O(log n).
    /// </summary>
    internal int TreeDepth => _root.Depth;

    private static FingerTreeDeque<T> Wrap(Tree<T, Leaf<T>> root) =>
        root.Size == 0 ? EmptyInstance : new(root);

    private FingerTreeDeque<T> ConcatCore(FingerTreeDeque<T> other)
    {
        if (other.IsEmpty)
            return this;
        if (IsEmpty)
            return other;
        if (other.Count > int.MaxValue - Count)
            throw CountOverflowError();
        return new(TreeOperations.Concat(_root, other._root));
    }

    /// <summary>
    /// Builds a tree by appending the enumeration results, throwing once the element count would exceed
    /// <paramref name="capacity"/>.
    /// </summary>
    /// <param name="items">Items to append.</param>
    /// <param name="capacity">Largest allowed element count.</param>
    private static Tree<T, Leaf<T>> BuildTree(IEnumerable<T> items, int capacity)
    {
        Tree<T, Leaf<T>> root = EmptyTree<T, Leaf<T>>.Instance;
        foreach (var item in items)
        {
            if (root.Size >= capacity)
                throw CountOverflowError();
            root = root.Snoc(new Leaf<T>(item));
        }

        return root;
    }

    /// <summary>
    /// Splits a tree into the leaves before <paramref name="index"/> and the leaves at and after it.
    /// </summary>
    /// <param name="tree">Tree to split.</param>
    /// <param name="index">Split index in the inclusive range from zero through the tree size.</param>
    private static (Tree<T, Leaf<T>> Left, Tree<T, Leaf<T>> Right) SplitTree(Tree<T, Leaf<T>> tree, int index)
    {
        if (index == 0)
            return (EmptyTree<T, Leaf<T>>.Instance, tree);
        if (index == tree.Size)
            return (tree, EmptyTree<T, Leaf<T>>.Instance);
        tree.SplitChild(index, out var left, out var hit, out _, out var right);
        return (left, right.Cons(hit));
    }

    private void CheckRange(int index, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (count > Count - index)
        {
            throw new ArgumentOutOfRangeException(
                nameof(count),
                count,
                "The range extends past the end of the deque.");
        }
    }

    private void CheckRoomForOneMore()
    {
        if (Count == int.MaxValue)
            throw CountOverflowError();
    }

    private static InvalidOperationException EmptyDequeError() =>
        new("The deque is empty.");

    private static ArgumentOutOfRangeException IndexRangeError(int index) =>
        new(nameof(index), index, "Index is outside the valid range of the deque.");

    private static OverflowException CountOverflowError() =>
        new("The operation would create a deque with more than Int32.MaxValue elements.");

    /// <summary>
    /// Enumerates a <see cref="FingerTreeDeque{T}"/> without allocating a boxed enumerator in pattern-based foreach.
    /// </summary>
    /// <remarks>
    /// Maintains an explicit traversal stack of O(log n) frames, giving O(1) amortized cost per yielded
    /// element. The enumerator observes the immutable snapshot it was created from.
    /// </remarks>
    public struct Enumerator : IEnumerator<T>
    {
        private Frame[]? _stack;
        private int _depth;
        private T _current;

        internal Enumerator(Tree<T, Leaf<T>> root)
        {
            _stack = null;
            _depth = 0;
            _current = default!;
            if (root.Size > 0)
                Push(root);
        }

        /// <summary>
        /// Gets the current element.
        /// </summary>
        /// <remarks>Undefined before the first call to <see cref="MoveNext"/> and after enumeration ends.</remarks>
        public readonly T Current => _current;

        readonly object? IEnumerator.Current => Current;

        /// <summary>
        /// Releases resources held by the enumerator.
        /// </summary>
        public void Dispose()
        {
        }

        /// <summary>
        /// Advances to the next element.
        /// </summary>
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

                // Advance the current frame before any Push: a Push may grow (and thus copy) the stack,
                // and the increment must already be recorded in the slot that gets copied.
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

        private void Push(IEnumerationBlock<T> block)
        {
            _stack ??= new Frame[8];
            if (_depth == _stack.Length)
                Array.Resize(ref _stack, _depth * 2);
            _stack[_depth++] = new Frame(block);
        }

        /// <summary>One traversal-stack entry: a block and the index of its next unvisited child.</summary>
        /// <param name="block">The block being traversed at this depth.</param>
        private struct Frame(IEnumerationBlock<T> block)
        {
            public readonly IEnumerationBlock<T> Block = block;
            public int NextChild = 0;
        }
    }
}
