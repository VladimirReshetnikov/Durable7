using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

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
/// <see cref="MyersIncrementalAncestorArena{T}"/> instead gives O(1)-amortized insertion and
/// O(log M) ancestor queries after M historical insertions.
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
    private readonly IIncrementalAncestorArena<T> _arena;
    private readonly Segment _left;
    private readonly Segment _right;

    private BilateralAncestralDeque(
        IIncrementalAncestorArena<T> arena,
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

    /// <summary>Creates an empty deque owned by an explicit incremental-ancestor arena.</summary>
    /// <param name="arena">The manager that retains and navigates every inserted node.</param>
    /// <returns>An empty bilateral handle.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="arena"/> is null.</exception>
    /// <exception cref="ArgumentException">The arena's bottom node is not at depth -1.</exception>
    public static BilateralAncestralDeque<T> Create(IIncrementalAncestorArena<T> arena)
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
    public static BilateralAncestralDeque<T> CreateMyers() => Create(new MyersIncrementalAncestorArena<T>());

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
