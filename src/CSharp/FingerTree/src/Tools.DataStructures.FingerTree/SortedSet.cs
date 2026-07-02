using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// An immutable, persistent sorted set (no duplicates) backed by an order-statistic measured finger tree:
/// logarithmic add/remove/search, navigable neighbor queries, ranking and order-statistic indexing, range
/// extraction, and linear-merge set algebra, with constant-time count and endpoints.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
/// <remarks>
/// <para>
/// The uniqueness-enforcing counterpart of <see cref="SortedBag{T}"/>: two elements are considered the same
/// when they compare equal under the set's <see cref="Comparer"/>, and <see cref="Add"/> is a no-op when an
/// equal element is already present. Like <see cref="SortedBag{T}"/>, it is built on
/// <see cref="OrderStatisticMeasure{T}"/> whose combine is order-independent, so it accepts an arbitrary
/// <see cref="IComparer{T}"/> applied in the split predicates at query time.
/// </para>
/// <para>
/// Complexity: <see cref="Count"/>, <see cref="IsEmpty"/>, <see cref="Min"/>, <see cref="Max"/> are O(1);
/// <see cref="Add"/>, <see cref="Remove"/>, <see cref="Contains"/>, <see cref="IndexOf"/>, the indexer, the
/// navigable queries, and <see cref="GetRange"/> are O(log n); the binary set operations and relations are
/// O(n + m). Instances are immutable and safe for concurrent reads, provided the supplied comparer is safe
/// for concurrent <c>Compare</c> calls (as <see cref="Comparer{T}.Default"/> and <see cref="Comparer{T}.Create"/>
/// results are).
/// </para>
/// <para>The binary operations assume the other set uses the same order; results are otherwise unspecified.</para>
/// </remarks>
/// <example>
/// <code>
/// var primes = SortedSet&lt;int&gt;.CreateRange(new[] { 2, 3, 5, 7, 11 });
/// primes.TryCeiling(6, out var atLeast6);   // 7
/// var third = primes[2];                     // 5  (third smallest)
/// var small = primes.GetRange(3, 8);         // { 3, 5, 7 }
/// </code>
/// </example>
public sealed class SortedSet<T> : IReadOnlyCollection<T>
{
    private static readonly SortedSet<T> EmptyDefault = new(
        FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>>.Empty,
        Comparer<T>.Default);

    private readonly FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> _tree;
    private readonly IComparer<T> _comparer;

    private SortedSet(FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> tree, IComparer<T> comparer)
    {
        _tree = tree;
        _comparer = comparer;
    }

    /// <summary>Gets the empty set ordered by <see cref="Comparer{T}.Default"/>.</summary>
    public static SortedSet<T> Empty => EmptyDefault;

    /// <summary>Creates an empty set with the given comparer (or the default comparer when null).</summary>
    /// <param name="comparer">Order to maintain, or <see langword="null"/> for <see cref="Comparer{T}.Default"/>.</param>
    /// <returns>An empty set using the chosen order.</returns>
    public static SortedSet<T> Create(IComparer<T>? comparer = null) =>
        comparer is null || ReferenceEquals(comparer, Comparer<T>.Default)
            ? EmptyDefault
            : new(FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>>.Empty, comparer);

    /// <summary>Creates a set containing the distinct elements of <paramref name="items"/>. O(n log n).</summary>
    /// <param name="items">Items to include (duplicates collapsed, keeping the first by comparer order).</param>
    /// <param name="comparer">Order to maintain, or <see langword="null"/> for <see cref="Comparer{T}.Default"/>.</param>
    /// <returns>A set of the distinct items, sorted.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static SortedSet<T> CreateRange(IEnumerable<T> items, IComparer<T>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var order = comparer ?? Comparer<T>.Default;
        var tree = FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>>.Empty;
        var hasPrevious = false;
        var previous = default(T);
        foreach (var item in items.OrderBy(x => x, order))
        {
            if (hasPrevious && order.Compare(previous!, item) == 0)
                continue;
            tree = tree.Append(item);
            previous = item;
            hasPrevious = true;
        }

        return new(tree, order);
    }

    /// <summary>Gets the number of elements. O(1).</summary>
    public int Count => _tree.Measure.Count;

    /// <summary>Gets a value indicating whether the set is empty. O(1).</summary>
    public bool IsEmpty => _tree.IsEmpty;

    /// <summary>Gets the comparer defining the order.</summary>
    public IComparer<T> Comparer => _comparer;

    /// <summary>Gets the least element under the set's <see cref="Comparer"/>. O(1).</summary>
    /// <exception cref="InvalidOperationException">The set is empty.</exception>
    public T Min => _tree.IsEmpty ? throw EmptyError() : _tree.First;

    /// <summary>Gets the greatest element under the set's <see cref="Comparer"/>. O(1).</summary>
    /// <exception cref="InvalidOperationException">The set is empty.</exception>
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
                throw new ArgumentOutOfRangeException(nameof(index), index, "Rank is outside the set's range.");
            return ElementAt(index);
        }
    }

    /// <summary>Returns the rank of <paramref name="item"/> (number of smaller elements), or -1 when absent. O(log n).</summary>
    /// <param name="item">Element to locate.</param>
    /// <returns>The zero-based rank, or -1 when <paramref name="item"/> is not present.</returns>
    public int IndexOf(T item) =>
        _tree.TryLocate(new KeyAtLeastPredicate<T>(_comparer, item), out var before, out var found)
        && _comparer.Compare(found, item) == 0
            ? before.Count
            : -1;

    /// <summary>Adds an element, or returns the unchanged set when an equal element is already present. O(log n).</summary>
    /// <param name="item">Element to add.</param>
    /// <returns>A set containing <paramref name="item"/>.</returns>
    public SortedSet<T> Add(T item)
    {
        var (less, atLeast) = SplitAtLeast(item);
        if (!atLeast.IsEmpty && _comparer.Compare(atLeast.First, item) == 0)
            return this;
        return Wrap(less.Append(item).Concat(atLeast));
    }

    /// <summary>Adds every element of <paramref name="items"/> (duplicates ignored). O(m log n).</summary>
    /// <param name="items">Elements to add.</param>
    /// <returns>A set containing all distinct added elements.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public SortedSet<T> AddRange(IEnumerable<T> items)
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

    /// <summary>Removes the element comparing equal to <paramref name="item"/>, if present. O(log n).</summary>
    /// <param name="item">Element to remove.</param>
    /// <param name="result">The set with the matching element removed; otherwise the unchanged set.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public bool TryRemove(T item, out SortedSet<T> result)
    {
        var (less, atLeast) = SplitAtLeast(item);
        if (atLeast.TryViewLeft(out var head, out var tail) && _comparer.Compare(head, item) == 0)
        {
            result = Wrap(less.Concat(tail));
            return true;
        }

        result = this;
        return false;
    }

    /// <summary>Removes the element comparing equal to <paramref name="item"/>, or returns the unchanged set. O(log n).</summary>
    /// <param name="item">Element to remove.</param>
    /// <returns>The resulting set.</returns>
    public SortedSet<T> Remove(T item) => TryRemove(item, out var result) ? result : this;

    /// <summary>Finds the greatest element less than or equal to <paramref name="item"/>. O(log n).</summary>
    /// <param name="item">Reference value.</param>
    /// <param name="floor">The floor element when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a floor exists; otherwise <see langword="false"/>.</returns>
    public bool TryFloor(T item, [MaybeNullWhen(false)] out T floor)
    {
        // The floor is the element just before the first element greater than item.
        var atMost = CountBelow(new KeyAbovePredicate<T>(_comparer, item));
        if (atMost == 0)
        {
            floor = default!;
            return false;
        }

        floor = ElementAt(atMost - 1);
        return true;
    }

    /// <summary>Finds the least element greater than or equal to <paramref name="item"/>. O(log n).</summary>
    /// <param name="item">Reference value.</param>
    /// <param name="ceiling">The ceiling element when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a ceiling exists; otherwise <see langword="false"/>.</returns>
    public bool TryCeiling(T item, [MaybeNullWhen(false)] out T ceiling) =>
        _tree.TryLocate(new KeyAtLeastPredicate<T>(_comparer, item), out _, out ceiling);

    /// <summary>Finds the greatest element strictly less than <paramref name="item"/>. O(log n).</summary>
    /// <param name="item">Reference value.</param>
    /// <param name="lower">The predecessor when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a strictly smaller element exists; otherwise <see langword="false"/>.</returns>
    public bool TryLower(T item, [MaybeNullWhen(false)] out T lower)
    {
        // The strict predecessor is the element just before the first element at or after item.
        var less = CountBelow(new KeyAtLeastPredicate<T>(_comparer, item));
        if (less == 0)
        {
            lower = default!;
            return false;
        }

        lower = ElementAt(less - 1);
        return true;
    }

    /// <summary>Finds the least element strictly greater than <paramref name="item"/>. O(log n).</summary>
    /// <param name="item">Reference value.</param>
    /// <param name="higher">The successor when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a strictly greater element exists; otherwise <see langword="false"/>.</returns>
    public bool TryHigher(T item, [MaybeNullWhen(false)] out T higher) =>
        _tree.TryLocate(new KeyAbovePredicate<T>(_comparer, item), out _, out higher);

    /// <summary>Returns the subset of elements in the inclusive range <c>[low, high]</c>. O(log n).</summary>
    /// <param name="low">Inclusive lower bound.</param>
    /// <param name="high">Inclusive upper bound.</param>
    /// <returns>A set of the in-range elements (empty when <paramref name="low"/> exceeds <paramref name="high"/>).</returns>
    public SortedSet<T> GetRange(T low, T high)
    {
        var (_, atLeast) = SplitAtLeast(low);
        var (inRange, _) = atLeast.Split(new KeyAbovePredicate<T>(_comparer, high));
        return Wrap(inRange);
    }

    /// <summary>Returns the union of this set and <paramref name="other"/>. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns>A set containing every element of either operand.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public SortedSet<T> Union(SortedSet<T> other) => Merge(other, emitOnlyThis: true, emitBoth: true, emitOnlyOther: true);

    /// <summary>Returns the intersection of this set and <paramref name="other"/>. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns>A set containing the elements present in both operands.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public SortedSet<T> Intersect(SortedSet<T> other) => Merge(other, emitOnlyThis: false, emitBoth: true, emitOnlyOther: false);

    /// <summary>Returns the elements of this set not in <paramref name="other"/>. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns>A set containing this set's elements absent from <paramref name="other"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public SortedSet<T> Except(SortedSet<T> other) => Merge(other, emitOnlyThis: true, emitBoth: false, emitOnlyOther: false);

    /// <summary>Returns the elements in exactly one of this set and <paramref name="other"/>. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns>A set containing the symmetric difference.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public SortedSet<T> SymmetricExcept(SortedSet<T> other) => Merge(other, emitOnlyThis: true, emitBoth: false, emitOnlyOther: true);

    /// <summary>Determines whether every element of this set is in <paramref name="other"/>. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns><see langword="true"/> when this set is a subset of <paramref name="other"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public bool IsSubsetOf(SortedSet<T> other)
    {
        var counts = MergeCounts(other);
        return counts.OnlyThis == 0;
    }

    /// <summary>Determines whether this set contains every element of <paramref name="other"/>. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns><see langword="true"/> when this set is a superset of <paramref name="other"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public bool IsSupersetOf(SortedSet<T> other)
    {
        var counts = MergeCounts(other);
        return counts.OnlyOther == 0;
    }

    /// <summary>Determines whether this set is a proper subset of <paramref name="other"/>. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns><see langword="true"/> when this set is a strict subset of <paramref name="other"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public bool IsProperSubsetOf(SortedSet<T> other)
    {
        var counts = MergeCounts(other);
        return counts.OnlyThis == 0 && counts.OnlyOther > 0;
    }

    /// <summary>Determines whether this set is a proper superset of <paramref name="other"/>. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns><see langword="true"/> when this set is a strict superset of <paramref name="other"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public bool IsProperSupersetOf(SortedSet<T> other)
    {
        var counts = MergeCounts(other);
        return counts.OnlyOther == 0 && counts.OnlyThis > 0;
    }

    /// <summary>Determines whether this set shares any element with <paramref name="other"/>. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns><see langword="true"/> when the sets intersect.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public bool Overlaps(SortedSet<T> other) => MergeCounts(other).Both > 0;

    /// <summary>Determines whether the two sets contain the same elements. O(n + m).</summary>
    /// <param name="other">The other set (assumed ordered the same way).</param>
    /// <returns><see langword="true"/> when the sets are equal.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public bool SetEquals(SortedSet<T> other)
    {
        var counts = MergeCounts(other);
        return counts.OnlyThis == 0 && counts.OnlyOther == 0;
    }

    /// <summary>Copies the elements to a new array in sorted order. O(n).</summary>
    /// <returns>A sorted array of the elements.</returns>
    public T[] ToArray() => _tree.ToArray();

    /// <summary>Returns an enumerator over the elements in sorted order.</summary>
    /// <returns>An enumerator yielding elements smallest first.</returns>
    public IEnumerator<T> GetEnumerator() => _tree.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>Reads the element at rank <paramref name="rank"/> without reconstructing a subtree. O(log n).</summary>
    /// <param name="rank">A zero-based rank known to be in <c>0 .. Count - 1</c>.</param>
    private T ElementAt(int rank)
    {
        _tree.TryLocate(new CountAbovePredicate<T>(rank), out _, out var item);
        return item!;
    }

    /// <summary>Counts the elements strictly before the first one satisfying <paramref name="atOrPast"/>, without
    /// reconstructing a subtree or allocating a closure. O(log n).</summary>
    /// <typeparam name="TPredicate">A value-type boundary predicate.</typeparam>
    /// <param name="atOrPast">Monotone predicate marking the boundary element.</param>
    private int CountBelow<TPredicate>(TPredicate atOrPast)
        where TPredicate : struct, IMeasurePredicate<RankedKey<T>>
    {
        _tree.TryLocate(atOrPast, out var before, out _);
        return before.Count;
    }

    private (FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> Less, FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> AtLeast)
        SplitAtLeast(T item) =>
        _tree.Split(new KeyAtLeastPredicate<T>(_comparer, item));

    private SortedSet<T> Merge(SortedSet<T> other, bool emitOnlyThis, bool emitBoth, bool emitOnlyOther)
    {
        ArgumentNullException.ThrowIfNull(other);
        var result = FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>>.Empty;
        using var thisItems = GetEnumerator();
        using var otherItems = other.GetEnumerator();
        var hasThis = thisItems.MoveNext();
        var hasOther = otherItems.MoveNext();

        while (hasThis && hasOther)
        {
            var order = _comparer.Compare(thisItems.Current, otherItems.Current);
            if (order < 0)
            {
                if (emitOnlyThis)
                    result = result.Append(thisItems.Current);
                hasThis = thisItems.MoveNext();
            }
            else if (order > 0)
            {
                if (emitOnlyOther)
                    result = result.Append(otherItems.Current);
                hasOther = otherItems.MoveNext();
            }
            else
            {
                if (emitBoth)
                    result = result.Append(thisItems.Current);
                hasThis = thisItems.MoveNext();
                hasOther = otherItems.MoveNext();
            }
        }

        if (emitOnlyThis)
            while (hasThis)
            {
                result = result.Append(thisItems.Current);
                hasThis = thisItems.MoveNext();
            }

        if (emitOnlyOther)
            while (hasOther)
            {
                result = result.Append(otherItems.Current);
                hasOther = otherItems.MoveNext();
            }

        return new(result, _comparer);
    }

    private (int OnlyThis, int Both, int OnlyOther) MergeCounts(SortedSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        int onlyThis = 0, both = 0, onlyOther = 0;
        using var thisItems = GetEnumerator();
        using var otherItems = other.GetEnumerator();
        var hasThis = thisItems.MoveNext();
        var hasOther = otherItems.MoveNext();

        while (hasThis && hasOther)
        {
            var order = _comparer.Compare(thisItems.Current, otherItems.Current);
            if (order < 0)
            {
                onlyThis++;
                hasThis = thisItems.MoveNext();
            }
            else if (order > 0)
            {
                onlyOther++;
                hasOther = otherItems.MoveNext();
            }
            else
            {
                both++;
                hasThis = thisItems.MoveNext();
                hasOther = otherItems.MoveNext();
            }
        }

        while (hasThis)
        {
            onlyThis++;
            hasThis = thisItems.MoveNext();
        }

        while (hasOther)
        {
            onlyOther++;
            hasOther = otherItems.MoveNext();
        }

        return (onlyThis, both, onlyOther);
    }

    // Collapse to the shared empty singleton only when the tree is empty AND the default comparer is in use;
    // a non-default-comparer set must keep its own comparer when it becomes empty.
    private SortedSet<T> Wrap(FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> tree) =>
        tree.IsEmpty && ReferenceEquals(_comparer, Comparer<T>.Default) ? EmptyDefault : new(tree, _comparer);

    private static InvalidOperationException EmptyError() => new("The sorted set is empty.");
}
