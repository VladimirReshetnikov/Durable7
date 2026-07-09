using System.Collections;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// An immutable, persistent sorted multiset (duplicates allowed) backed by an order-statistic measured
/// finger tree: it keeps elements in comparer order and supports logarithmic add/remove/search, ranking,
/// range extraction, and order-statistic indexing, with constant-time count and endpoints.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
/// <remarks>
/// <para>
/// Built on <see cref="FingerTree{TElement, TMeasure, TMeasureOps}"/> with
/// <see cref="OrderStatisticMeasure{T}"/>. That measure's combine is order-independent (count plus a
/// right-biased last key), so the comparer is applied in the split predicates at query time; the bag stores
/// any <see cref="IComparer{T}"/> supplied at creation, defaulting to <see cref="Comparer{T}.Default"/>.
/// </para>
/// <para>
/// The sorted invariant is maintained by construction, so — unlike the raw measured tree, where sortedness
/// is a precondition — every query is well-defined. Equal elements keep insertion order (new equals are
/// added after existing ones). For set semantics, use <see cref="Contains"/> before <see cref="Add"/>.
/// </para>
/// <para>
/// Complexity: <see cref="Count"/>, <see cref="IsEmpty"/>, <see cref="Min"/>, <see cref="Max"/> are O(1);
/// <see cref="Add"/>, <see cref="Remove"/>, <see cref="RemoveAll"/>, <see cref="Contains"/>,
/// <see cref="CountOf"/>, <see cref="CountLessThan"/>, <see cref="CountAtMost"/>, the indexer, and
/// <see cref="GetRange"/> are O(log n). Instances are immutable and safe for concurrent reads, provided the
/// supplied comparer is itself safe for concurrent <c>Compare</c> calls — as <see cref="Comparer{T}.Default"/>
/// and the results of <see cref="Comparer{T}.Create"/> are.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// var bag = SortedBag&lt;int&gt;.CreateRange(new[] { 5, 1, 3, 3, 9 });
/// var rank = bag.CountLessThan(3);  // 1  (one element below 3)
/// var third = bag[2];               // 3  (third smallest)
/// var without = bag.Remove(3);      // removes one 3
/// </code>
/// </example>
public sealed class SortedBag<T> : IReadOnlyCollection<T>
{
    private static readonly SortedBag<T> EmptyDefault = new(
        FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>>.Empty,
        Comparer<T>.Default);

    private readonly FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> _tree;
    private readonly IComparer<T> _comparer;

    private SortedBag(FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> tree, IComparer<T> comparer)
    {
        _tree = tree;
        _comparer = comparer;
    }

    /// <summary>Gets the empty bag ordered by <see cref="Comparer{T}.Default"/>.</summary>
    public static SortedBag<T> Empty => EmptyDefault;

    /// <summary>Creates an empty bag with the given comparer (or the default comparer when null).</summary>
    /// <param name="comparer">Order to maintain, or <see langword="null"/> for <see cref="Comparer{T}.Default"/>.</param>
    /// <returns>An empty bag using the chosen order.</returns>
    public static SortedBag<T> Create(IComparer<T>? comparer = null) =>
        comparer is null || ReferenceEquals(comparer, Comparer<T>.Default)
            ? EmptyDefault
            : new(FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>>.Empty, comparer);

    /// <summary>Creates a bag containing all of <paramref name="items"/>, in comparer order. O(n log n).</summary>
    /// <param name="items">Items to include (duplicates preserved).</param>
    /// <param name="comparer">Order to maintain, or <see langword="null"/> for <see cref="Comparer{T}.Default"/>.</param>
    /// <returns>A bag containing every item, sorted.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static SortedBag<T> CreateRange(IEnumerable<T> items, IComparer<T>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var order = comparer ?? Comparer<T>.Default;
        // OrderBy is a stable sort, so equal elements keep input order — matching the incremental Add path
        // and honoring the documented stability invariant (Array.Sort is unstable and would not).
        var sorted = items.OrderBy(item => item, order);

        var tree = FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>>.Empty;
        foreach (var item in sorted)
            tree = tree.Append(item);
        return new(tree, order);
    }

    /// <summary>Gets the number of elements. O(1) amortized; the first read of a fresh spine may force memoized deferred work.</summary>
    public int Count => _tree.Measure.Count;

    /// <summary>Gets a value indicating whether the bag is empty. O(1).</summary>
    public bool IsEmpty => _tree.IsEmpty;

    /// <summary>Gets the comparer defining the order.</summary>
    public IComparer<T> Comparer => _comparer;

    /// <summary>Gets the least element under the bag's <see cref="Comparer"/>. O(1).</summary>
    /// <exception cref="InvalidOperationException">The bag is empty.</exception>
    public T Min => _tree.IsEmpty ? throw EmptyError() : _tree.First;

    /// <summary>Gets the greatest element under the bag's <see cref="Comparer"/>. O(1).</summary>
    /// <exception cref="InvalidOperationException">The bag is empty.</exception>
    public T Max => _tree.IsEmpty ? throw EmptyError() : _tree.Last;

    /// <summary>Gets the element at rank <paramref name="index"/> (the <c>index</c>-th smallest). O(log n).</summary>
    /// <param name="index">Zero-based rank.</param>
    /// <returns>The element with that rank.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count - 1</c>.</exception>
    public T this[int index]
    {
        get
        {
            if ((uint)index >= (uint)Count)
                throw new ArgumentOutOfRangeException(nameof(index), index, "Rank is outside the bag's range.");
            _tree.TryLocate(new CountAbovePredicate<T>(index), out _, out var item);
            return item!;
        }
    }

    /// <summary>Adds an element, after any existing equal elements. O(log n).</summary>
    /// <param name="item">Element to add.</param>
    /// <returns>A bag containing the added element.</returns>
    public SortedBag<T> Add(T item)
    {
        var (atMost, greater) = _tree.Split(new KeyAbovePredicate<T>(_comparer, item));
        return Wrap(atMost.Append(item).Concat(greater));
    }

    /// <summary>Adds every element of <paramref name="items"/>. O(m log n) for m items.</summary>
    /// <param name="items">Elements to add.</param>
    /// <returns>A bag containing all added elements.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public SortedBag<T> AddRange(IEnumerable<T> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = this;
        foreach (var item in items)
            result = result.Add(item);
        return result;
    }

    /// <summary>Determines whether an element comparing equal to <paramref name="item"/> is present. O(log n).</summary>
    /// <param name="item">Element to search for.</param>
    /// <returns><see langword="true"/> when present; otherwise <see langword="false"/>.</returns>
    public bool Contains(T item) =>
        _tree.TryLocate(new KeyAtLeastPredicate<T>(_comparer, item), out _, out var found)
        && _comparer.Compare(found, item) == 0;

    /// <summary>Counts elements comparing equal to <paramref name="item"/>. O(log n).</summary>
    /// <param name="item">Element to count.</param>
    /// <returns>The multiplicity of <paramref name="item"/>.</returns>
    public int CountOf(T item) => CountAtMost(item) - CountLessThan(item);

    /// <summary>Counts elements comparing strictly less than <paramref name="item"/> (its rank / lower bound). O(log n).</summary>
    /// <param name="item">Reference element.</param>
    /// <returns>The number of elements less than <paramref name="item"/>.</returns>
    public int CountLessThan(T item)
    {
        _tree.TryLocate(new KeyAtLeastPredicate<T>(_comparer, item), out var before, out _);
        return before.Count;
    }

    /// <summary>Counts elements comparing less than or equal to <paramref name="item"/> (its upper bound). O(log n).</summary>
    /// <param name="item">Reference element.</param>
    /// <returns>The number of elements less than or equal to <paramref name="item"/>.</returns>
    public int CountAtMost(T item)
    {
        _tree.TryLocate(new KeyAbovePredicate<T>(_comparer, item), out var before, out _);
        return before.Count;
    }

    /// <summary>Removes one element comparing equal to <paramref name="item"/>, if present. O(log n).</summary>
    /// <param name="item">Element to remove.</param>
    /// <param name="result">The bag with one matching element removed; otherwise the unchanged bag.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public bool TryRemove(T item, out SortedBag<T> result)
    {
        var (less, atLeast) = _tree.Split(new KeyAtLeastPredicate<T>(_comparer, item));
        if (atLeast.TryViewLeft(out var head, out var tail) && _comparer.Compare(head, item) == 0)
        {
            result = Wrap(less.Concat(tail));
            return true;
        }

        result = this;
        return false;
    }

    /// <summary>Removes one element comparing equal to <paramref name="item"/>, or returns the unchanged bag. O(log n).</summary>
    /// <param name="item">Element to remove.</param>
    /// <returns>The resulting bag.</returns>
    public SortedBag<T> Remove(T item) => TryRemove(item, out var result) ? result : this;

    /// <summary>Removes all elements comparing equal to <paramref name="item"/>. O(log n).</summary>
    /// <param name="item">Element whose occurrences are removed.</param>
    /// <returns>A bag with every equal element removed.</returns>
    public SortedBag<T> RemoveAll(T item)
    {
        var (less, atLeast) = _tree.Split(new KeyAtLeastPredicate<T>(_comparer, item));
        var (_, greater) = atLeast.Split(new KeyAbovePredicate<T>(_comparer, item));
        return Wrap(less.Concat(greater));
    }

    /// <summary>
    /// Returns the sub-bag of elements in the inclusive range <c>[low, high]</c>. O(log n).
    /// </summary>
    /// <param name="low">Inclusive lower bound.</param>
    /// <param name="high">Inclusive upper bound.</param>
    /// <returns>A bag of the in-range elements (empty when <paramref name="low"/> exceeds <paramref name="high"/>).</returns>
    public SortedBag<T> GetRange(T low, T high)
    {
        var (_, atLeast) = _tree.Split(new KeyAtLeastPredicate<T>(_comparer, low));
        var (inRange, _) = atLeast.Split(new KeyAbovePredicate<T>(_comparer, high));
        return Wrap(inRange);
    }

    /// <summary>Copies the elements to a new array in sorted order. O(n).</summary>
    /// <returns>A sorted array of the elements.</returns>
    public T[] ToArray() => _tree.ToArray();

    /// <summary>Returns an enumerator over the elements in sorted order.</summary>
    /// <returns>An enumerator yielding elements smallest first.</returns>
    public IEnumerator<T> GetEnumerator() => _tree.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    // Collapse to the shared empty singleton only when the tree is empty AND the default comparer is in use;
    // a non-default-comparer bag must keep its own comparer when it becomes empty.
    private SortedBag<T> Wrap(FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> tree) =>
        tree.IsEmpty && ReferenceEquals(_comparer, Comparer<T>.Default) ? EmptyDefault : new(tree, _comparer);

    private static InvalidOperationException EmptyError() => new("The sorted bag is empty.");
}
