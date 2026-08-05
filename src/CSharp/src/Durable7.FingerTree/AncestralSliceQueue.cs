using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

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
    /// <exception cref="ArgumentNullException"><paramref name="arena"/> is null.</exception>
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
