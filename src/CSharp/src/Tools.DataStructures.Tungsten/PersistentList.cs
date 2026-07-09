// Persistent Tungsten-List-shaped sequence facade over the FingerTree deque core.
//
// Design provenance: docs/reference/derived-structure-catalog.md (consumer case study:
// Tungsten) and the Tungsten design study
// C:\Smithereens\src\Tungsten\docs\reports\2026-07-03-list-association-persistent-backends.md.
// The engine-level representation tiers described there (small-array tier, packed numeric
// tier, memoized structural hash) are client responsibilities behind the client's own
// abstract expression surface; this library ships the single persistent representation
// those tiers promote into.

using System.Collections;
using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.Tungsten;

/// <summary>
/// Non-instantiable factory entry points for <see cref="PersistentList{T}"/> that infer the
/// element type from the arguments.
/// </summary>
public static class PersistentList
{
    /// <summary>
    /// Creates a persistent list from the specified items.
    /// </summary>
    /// <typeparam name="T">Element type.</typeparam>
    /// <param name="items">Items in front-to-back order.</param>
    /// <returns>A list containing <paramref name="items"/> in order.</returns>
    /// <remarks>O(n). The span contents are copied; later changes to the source do not affect the list.</remarks>
    public static PersistentList<T> Create<T>(params ReadOnlySpan<T> items) =>
        PersistentList<T>.Create(items);

    /// <summary>
    /// Creates a persistent list from an item sequence.
    /// </summary>
    /// <typeparam name="T">Element type.</typeparam>
    /// <param name="items">Items in front-to-back order.</param>
    /// <returns>A list containing <paramref name="items"/> in enumeration order.</returns>
    /// <remarks>O(n). The sequence is enumerated exactly once.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static PersistentList<T> CreateRange<T>(IEnumerable<T> items) =>
        PersistentList<T>.CreateRange(items);
}

/// <summary>
/// An immutable, persistent ordered sequence shaped for representing a Tungsten Language
/// <c>List</c> expression: <c>{e1, e2, ..., en}</c>.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
/// <remarks>
/// <para>
/// Every mutating-style operation returns a new list that shares structure with its source;
/// existing versions are never observably changed and remain fully usable, including under
/// branching histories. The implementation delegates to <see cref="FingerTreeDeque{T}"/>, so end
/// operations (<see cref="Append"/>, <see cref="Prepend"/>, <see cref="RemoveFirst"/>,
/// <see cref="RemoveLast"/>) are O(1) amortized, positional operations (<see cref="this[int]"/>,
/// <see cref="Insert"/>, <see cref="RemoveAt"/>, <see cref="SetItem"/>, <see cref="SplitAt"/>)
/// are O(log n), and <see cref="Join"/> of two lists is O(log min(n1, n2)). In particular the
/// classic Tungsten append-in-a-loop trap (quadratic list growth) costs O(n) total here along a
/// linear history.
/// </para>
/// <para>
/// Indexing is zero-based throughout, following .NET convention; the corresponding Tungsten
/// <c>Part</c> positions are one-based. Tungsten operation correspondence:
/// <c>Length</c> → <see cref="Count"/>, <c>Part</c> → <see cref="this[int]"/> and
/// <see cref="GetRange"/>, <c>Append</c> → <see cref="Append"/>, <c>Prepend</c> →
/// <see cref="Prepend"/>, <c>Insert</c> → <see cref="Insert"/>, <c>Delete</c> →
/// <see cref="RemoveAt"/>, <c>ReplacePart</c> → <see cref="SetItem"/>, <c>MapAt</c> →
/// <see cref="UpdateAt"/>, <c>Join</c> → <see cref="Join"/> and <see cref="AddRange"/>,
/// <c>Take</c> → <see cref="Take"/> and <see cref="TakeLast"/>, <c>Drop</c> →
/// <see cref="Drop"/> and <see cref="DropLast"/>, <c>First</c> → <see cref="First"/>,
/// <c>Last</c> → <see cref="Last"/>, <c>Rest</c> → <see cref="RemoveFirst"/>, <c>Most</c> →
/// <see cref="RemoveLast"/>, <c>Reverse</c> → <see cref="Reverse"/>, <c>Map</c> →
/// <see cref="Map{TResult}"/>, <c>MemberQ</c> → <see cref="Contains"/>, <c>FirstPosition</c> →
/// <see cref="IndexOf"/>.
/// </para>
/// <para>
/// The type is a reference type; <c>default(PersistentList&lt;T&gt;)</c> is
/// <see langword="null"/>, not an empty list. Use <see cref="Empty"/> as the canonical empty
/// value: all empty results collapse to that single shared instance. Operations that produce a
/// list with the same contents as the receiver return the receiver instance where the underlying
/// deque detects it, giving callers a reference-equality "nothing changed" signal. Instances are
/// safe for concurrent readers without synchronization.
/// </para>
/// </remarks>
public sealed class PersistentList<T> : IReadOnlyList<T>
{
    private static readonly PersistentList<T> EmptyInstance = new(FingerTreeDeque<T>.Empty);

    private readonly FingerTreeDeque<T> _items;

    private PersistentList(FingerTreeDeque<T> items) => _items = items;

    private static PersistentList<T> Wrap(FingerTreeDeque<T> items) =>
        items.IsEmpty ? EmptyInstance : new(items);

    private PersistentList<T> Replace(FingerTreeDeque<T> items) =>
        ReferenceEquals(items, _items) ? this : Wrap(items);

    /// <summary>
    /// Gets the canonical empty list.
    /// </summary>
    /// <remarks>O(1). A single shared instance; all empty results reuse it.</remarks>
    public static PersistentList<T> Empty => EmptyInstance;

    /// <summary>
    /// Gets the number of elements. Tungsten: <c>Length</c>.
    /// </summary>
    /// <remarks>O(1).</remarks>
    public int Count => _items.Count;

    /// <summary>
    /// Gets a value indicating whether the list has no elements.
    /// </summary>
    /// <remarks>O(1).</remarks>
    public bool IsEmpty => _items.IsEmpty;

    /// <summary>
    /// Gets the first element. Tungsten: <c>First</c>.
    /// </summary>
    /// <remarks>O(1) worst-case.</remarks>
    /// <exception cref="InvalidOperationException">The list is empty.</exception>
    public T First => IsEmpty ? throw EmptyError() : _items.First;

    /// <summary>
    /// Gets the last element. Tungsten: <c>Last</c>.
    /// </summary>
    /// <remarks>O(1) worst-case.</remarks>
    /// <exception cref="InvalidOperationException">The list is empty.</exception>
    public T Last => IsEmpty ? throw EmptyError() : _items.Last;

    /// <summary>
    /// Gets the element at a zero-based index. Tungsten: <c>Part</c> (one-based there).
    /// </summary>
    /// <param name="index">Zero-based element index.</param>
    /// <remarks>O(log min(index + 1, Count - index)).</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or ≥ <see cref="Count"/>.</exception>
    public T this[int index] => _items[index];

    /// <summary>
    /// Creates a persistent list from the specified items.
    /// </summary>
    /// <param name="items">Items in front-to-back order.</param>
    /// <returns>A list containing <paramref name="items"/> in order.</returns>
    /// <remarks>O(n). The span contents are copied; later changes to the source do not affect the list.</remarks>
    public static PersistentList<T> Create(params ReadOnlySpan<T> items) =>
        Wrap(FingerTreeDeque<T>.Create(items));

    /// <summary>
    /// Creates a persistent list from an item sequence.
    /// </summary>
    /// <param name="items">Items in front-to-back order.</param>
    /// <returns>A list containing <paramref name="items"/> in enumeration order.</returns>
    /// <remarks>
    /// O(n). When <paramref name="items"/> is itself a <see cref="PersistentList{T}"/>, the
    /// existing instance is returned in O(1) without copying.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static PersistentList<T> CreateRange(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        return items as PersistentList<T> ?? Wrap(FingerTreeDeque<T>.CreateRange(items));
    }

    /// <summary>
    /// Returns a list with an element added at the back. Tungsten: <c>Append</c>.
    /// </summary>
    /// <param name="item">Element to add.</param>
    /// <returns>A new list ending with <paramref name="item"/>; the source is unchanged.</returns>
    /// <remarks>O(1) amortized, O(log n) worst-case.</remarks>
    public PersistentList<T> Append(T item) => Wrap(_items.AddLast(item));

    /// <summary>
    /// Returns a list with an element added at the front. Tungsten: <c>Prepend</c>.
    /// </summary>
    /// <param name="item">Element to add.</param>
    /// <returns>A new list starting with <paramref name="item"/>; the source is unchanged.</returns>
    /// <remarks>O(1) amortized, O(log n) worst-case.</remarks>
    public PersistentList<T> Prepend(T item) => Wrap(_items.AddFirst(item));

    /// <summary>
    /// Returns a list with a sequence appended at the back. Tungsten: <c>Join</c> with a
    /// materialized second operand.
    /// </summary>
    /// <param name="items">Items to append in enumeration order.</param>
    /// <returns>A new list; the source is unchanged.</returns>
    /// <remarks>
    /// O(k) for k appended items; O(log min(n, k)) when <paramref name="items"/> is itself a
    /// <see cref="PersistentList{T}"/>, via balanced tree concatenation.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public PersistentList<T> AddRange(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        return items is PersistentList<T> other
            ? Join(other)
            : Replace(_items.AddRange(items));
    }

    /// <summary>
    /// Returns the concatenation of this list and another persistent list. Tungsten: <c>Join</c>.
    /// </summary>
    /// <param name="other">List to append after this one.</param>
    /// <returns>
    /// A new list containing this list's elements followed by <paramref name="other"/>'s; both
    /// sources are unchanged. Returns the non-empty operand instance when the other is empty.
    /// </returns>
    /// <remarks>O(log min(n1, n2)) via balanced tree concatenation; both operands share structure with the result.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public PersistentList<T> Join(PersistentList<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (other.IsEmpty)
            return this;
        if (IsEmpty)
            return other;
        return Wrap(_items.Concat(other._items));
    }

    /// <summary>
    /// Returns a list with an element inserted before the specified zero-based index. Tungsten:
    /// <c>Insert</c> (one-based there).
    /// </summary>
    /// <param name="index">Zero-based insertion index; <see cref="Count"/> appends at the back.</param>
    /// <param name="item">Element to insert.</param>
    /// <returns>A new list; the source is unchanged.</returns>
    /// <remarks>O(log(min(index, Count - index) + 1)) amortized — logarithmic in the distance from the nearer end; O(log n) worst-case for a single call.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or &gt; <see cref="Count"/>.</exception>
    public PersistentList<T> Insert(int index, T item) => Wrap(_items.InsertAt(index, item));

    /// <summary>
    /// Returns a list with a sequence inserted before the specified zero-based index.
    /// </summary>
    /// <param name="index">Zero-based insertion index; <see cref="Count"/> appends at the back.</param>
    /// <param name="items">Items to insert in enumeration order.</param>
    /// <returns>A new list; the source is unchanged.</returns>
    /// <remarks>O(log n + k) for k inserted items.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or &gt; <see cref="Count"/>.</exception>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public PersistentList<T> InsertRange(int index, IEnumerable<T> items) =>
        Replace(_items.InsertRange(index, items));

    /// <summary>
    /// Returns a list with the element at a zero-based index removed. Tungsten: <c>Delete</c>
    /// (one-based there).
    /// </summary>
    /// <param name="index">Zero-based element index.</param>
    /// <returns>A new list one element shorter; the source is unchanged.</returns>
    /// <remarks>O(log min(index + 1, Count - index)) amortized; O(log n) worst-case for a single call.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or ≥ <see cref="Count"/>.</exception>
    public PersistentList<T> RemoveAt(int index) => Wrap(_items.RemoveAt(index));

    /// <summary>
    /// Returns a list with a contiguous range removed.
    /// </summary>
    /// <param name="index">Zero-based index of the first removed element.</param>
    /// <param name="count">Number of elements to remove.</param>
    /// <returns>A new list; the source is unchanged.</returns>
    /// <remarks>O(log n).</remarks>
    /// <exception cref="ArgumentOutOfRangeException">The range does not lie within the list.</exception>
    public PersistentList<T> RemoveRange(int index, int count) =>
        Replace(_items.RemoveRange(index, count));

    /// <summary>
    /// Returns a list with the first element removed. Tungsten: <c>Rest</c>.
    /// </summary>
    /// <returns>A new list one element shorter; the source is unchanged.</returns>
    /// <remarks>O(1) amortized, O(log n) worst-case.</remarks>
    /// <exception cref="InvalidOperationException">The list is empty.</exception>
    public PersistentList<T> RemoveFirst() =>
        IsEmpty ? throw EmptyError() : Wrap(_items.RemoveFirst());

    /// <summary>
    /// Returns a list with the last element removed. Tungsten: <c>Most</c>.
    /// </summary>
    /// <returns>A new list one element shorter; the source is unchanged.</returns>
    /// <remarks>O(1) amortized, O(log n) worst-case.</remarks>
    /// <exception cref="InvalidOperationException">The list is empty.</exception>
    public PersistentList<T> RemoveLast() =>
        IsEmpty ? throw EmptyError() : Wrap(_items.RemoveLast());

    /// <summary>
    /// Returns a list with the element at a zero-based index replaced. Tungsten:
    /// <c>ReplacePart</c> (one-based there).
    /// </summary>
    /// <param name="index">Zero-based element index.</param>
    /// <param name="value">Replacement element.</param>
    /// <returns>A new list; the source is unchanged.</returns>
    /// <remarks>O(log min(index + 1, Count - index)) amortized; replacing an endpoint element is O(1) worst-case.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or ≥ <see cref="Count"/>.</exception>
    public PersistentList<T> SetItem(int index, T value) => Replace(_items.SetItem(index, value));

    /// <summary>
    /// Returns a list with the element at a zero-based index replaced by a computed value.
    /// Tungsten: <c>MapAt</c> (one-based there).
    /// </summary>
    /// <param name="index">Zero-based element index.</param>
    /// <param name="updater">Pure function from the current element to its replacement.</param>
    /// <returns>A new list; the source is unchanged.</returns>
    /// <remarks>
    /// O(log min(index + 1, Count - index)) amortized. <paramref name="updater"/> is invoked
    /// exactly once; exceptions it throws propagate and leave the source unchanged.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or ≥ <see cref="Count"/>.</exception>
    /// <exception cref="ArgumentNullException"><paramref name="updater"/> is <see langword="null"/>.</exception>
    public PersistentList<T> UpdateAt(int index, Func<T, T> updater) =>
        Replace(_items.UpdateAt(index, updater));

    /// <summary>
    /// Returns the contiguous sub-list starting at a zero-based index. Tungsten: <c>Part</c>
    /// with a span (one-based there).
    /// </summary>
    /// <param name="index">Zero-based index of the first kept element.</param>
    /// <param name="count">Number of elements to keep.</param>
    /// <returns>A new list of length <paramref name="count"/>; the source is unchanged.</returns>
    /// <remarks>O(log n); the result shares structure with the source.</remarks>
    /// <exception cref="ArgumentOutOfRangeException">The range does not lie within the list.</exception>
    public PersistentList<T> GetRange(int index, int count) =>
        Replace(_items.GetRange(index, count));

    /// <summary>
    /// Returns the first <paramref name="count"/> elements. Tungsten: <c>Take[list, count]</c>.
    /// </summary>
    /// <param name="count">Number of leading elements to keep.</param>
    /// <returns>A new list of length <paramref name="count"/>; the source is unchanged.</returns>
    /// <remarks>O(log n); the result shares structure with the source.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is negative or &gt; <see cref="Count"/>.</exception>
    public PersistentList<T> Take(int count) => GetRange(0, count);

    /// <summary>
    /// Returns the last <paramref name="count"/> elements. Tungsten: <c>Take[list, -count]</c>.
    /// </summary>
    /// <param name="count">Number of trailing elements to keep.</param>
    /// <returns>A new list of length <paramref name="count"/>; the source is unchanged.</returns>
    /// <remarks>O(log n); the result shares structure with the source.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is negative or &gt; <see cref="Count"/>.</exception>
    public PersistentList<T> TakeLast(int count)
    {
        CheckCount(count);
        return GetRange(Count - count, count);
    }

    /// <summary>
    /// Returns the list without its first <paramref name="count"/> elements. Tungsten:
    /// <c>Drop[list, count]</c>.
    /// </summary>
    /// <param name="count">Number of leading elements to remove.</param>
    /// <returns>A new list of length <c>Count - count</c>; the source is unchanged.</returns>
    /// <remarks>O(log n); the result shares structure with the source.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is negative or &gt; <see cref="Count"/>.</exception>
    public PersistentList<T> Drop(int count)
    {
        CheckCount(count);
        return GetRange(count, Count - count);
    }

    /// <summary>
    /// Returns the list without its last <paramref name="count"/> elements. Tungsten:
    /// <c>Drop[list, -count]</c>.
    /// </summary>
    /// <param name="count">Number of trailing elements to remove.</param>
    /// <returns>A new list of length <c>Count - count</c>; the source is unchanged.</returns>
    /// <remarks>O(log n); the result shares structure with the source.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is negative or &gt; <see cref="Count"/>.</exception>
    public PersistentList<T> DropLast(int count)
    {
        CheckCount(count);
        return GetRange(0, Count - count);
    }

    private void CheckCount(int count)
    {
        if ((uint)count > (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(count), count, "Count must lie within 0 .. Count.");
    }

    /// <summary>
    /// Splits the list before a zero-based index.
    /// </summary>
    /// <param name="index">Zero-based split point; 0 puts everything on the right, <see cref="Count"/> everything on the left.</param>
    /// <returns>The leading <paramref name="index"/> elements and the remaining elements; the source is unchanged.</returns>
    /// <remarks>O(log n); both halves share structure with the source.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or &gt; <see cref="Count"/>.</exception>
    public (PersistentList<T> Left, PersistentList<T> Right) SplitAt(int index)
    {
        var split = _items.SplitAt(index);
        return (Wrap(split.Left), Wrap(split.Right));
    }

    /// <summary>
    /// Returns the list with element order reversed. Tungsten: <c>Reverse</c>.
    /// </summary>
    /// <returns>A new list; the source is unchanged.</returns>
    /// <remarks>
    /// O(n), matching the reference Tungsten cost. A wrapper-level reversal bit would make this
    /// O(1) at the price of a direction dispatch on every other operation; clients that reverse
    /// on a hot path should keep such a bit on their side (see the design provenance note at the
    /// top of this file).
    /// </remarks>
    public PersistentList<T> Reverse()
    {
        if (Count <= 1)
            return this;
        var array = _items.ToArray();
        Array.Reverse(array);
        return Wrap(FingerTreeDeque<T>.Create(array));
    }

    /// <summary>
    /// Returns the list with a pure function applied to every element. Tungsten: <c>Map</c>.
    /// </summary>
    /// <typeparam name="TResult">Result element type.</typeparam>
    /// <param name="selector">Pure function applied to each element in order.</param>
    /// <returns>A new list of equal length; the source is unchanged.</returns>
    /// <remarks>
    /// O(n). <paramref name="selector"/> is invoked exactly once per element, front to back;
    /// exceptions it throws propagate and leave the source unchanged.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="selector"/> is <see langword="null"/>.</exception>
    public PersistentList<TResult> Map<TResult>(Func<T, TResult> selector)
    {
        ArgumentNullException.ThrowIfNull(selector);
        if (IsEmpty)
            return PersistentList<TResult>.Empty;
        var array = new TResult[Count];
        var i = 0;
        foreach (var item in _items)
            array[i++] = selector(item);
        return PersistentList<TResult>.Create(array);
    }

    /// <summary>
    /// Finds the zero-based index of the first occurrence of an element. Tungsten:
    /// <c>FirstPosition</c> (one-based there).
    /// </summary>
    /// <param name="item">Element to locate.</param>
    /// <param name="comparer">Equality comparer, or <see langword="null"/> for <see cref="EqualityComparer{T}.Default"/>.</param>
    /// <returns>The zero-based index of the first equal element, or -1 when absent.</returns>
    /// <remarks>O(n) worst-case; stops at the first match.</remarks>
    public int IndexOf(T item, IEqualityComparer<T>? comparer = null)
    {
        var effective = comparer ?? EqualityComparer<T>.Default;
        var i = 0;
        foreach (var candidate in _items)
        {
            if (effective.Equals(candidate, item))
                return i;
            i++;
        }
        return -1;
    }

    /// <summary>
    /// Determines whether the list contains an equal element. Tungsten: <c>MemberQ</c>.
    /// </summary>
    /// <param name="item">Element to locate.</param>
    /// <param name="comparer">Equality comparer, or <see langword="null"/> for <see cref="EqualityComparer{T}.Default"/>.</param>
    /// <returns><see langword="true"/> when an equal element exists; otherwise <see langword="false"/>.</returns>
    /// <remarks>O(n) worst-case; stops at the first match.</remarks>
    public bool Contains(T item, IEqualityComparer<T>? comparer = null) =>
        IndexOf(item, comparer) >= 0;

    /// <summary>
    /// Copies the elements to a new array in list order.
    /// </summary>
    /// <returns>A fresh array of length <see cref="Count"/>.</returns>
    /// <remarks>O(n).</remarks>
    public T[] ToArray() => _items.ToArray();

    /// <summary>
    /// Copies the elements into an existing array in list order.
    /// </summary>
    /// <param name="array">Destination array.</param>
    /// <param name="arrayIndex">Destination start index.</param>
    /// <remarks>O(n).</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="array"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="arrayIndex"/> is negative.</exception>
    /// <exception cref="ArgumentException">The destination is too small.</exception>
    public void CopyTo(T[] array, int arrayIndex) => _items.CopyTo(array, arrayIndex);

    /// <summary>
    /// Returns a non-allocating struct enumerator over the elements in list order.
    /// </summary>
    /// <returns>An enumerator positioned before the first element.</returns>
    /// <remarks>
    /// Enumerates the immutable snapshot; it can never observe later versions. O(1) per step
    /// amortized.
    /// </remarks>
    public FingerTreeDeque<T>.Enumerator GetEnumerator() => _items.GetEnumerator();

    IEnumerator<T> IEnumerable<T>.GetEnumerator() => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private static InvalidOperationException EmptyError() => new("The list is empty.");
}
