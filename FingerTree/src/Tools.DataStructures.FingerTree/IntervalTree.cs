using System.Collections;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// A closed interval <c>[Low, High]</c> over a totally ordered <typeparamref name="T"/>
/// (compared with <see cref="Comparer{T}.Default"/>).
/// </summary>
/// <typeparam name="T">Endpoint type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
/// <param name="Low">Inclusive lower endpoint; should satisfy <c>Low &lt;= High</c>.</param>
/// <param name="High">Inclusive upper endpoint.</param>
/// <remarks>
/// The interval is treated as the closed range including both endpoints. <c>Low &lt;= High</c> is a
/// precondition (an interval with <c>Low &gt; High</c> is degenerate and makes overlap results
/// unspecified); it is not validated, mirroring the "caller maintains the precondition" style of the sorted
/// helpers.
/// </remarks>
public readonly record struct Interval<T>(T Low, T High)
{
    /// <summary>Determines whether this interval contains <paramref name="point"/> (inclusive).</summary>
    /// <param name="point">The point to test.</param>
    /// <returns><see langword="true"/> when <c>Low &lt;= point &lt;= High</c>.</returns>
    public bool Contains(T point)
    {
        var comparer = Comparer<T>.Default;
        return comparer.Compare(Low, point) <= 0 && comparer.Compare(point, High) <= 0;
    }

    /// <summary>Determines whether this interval overlaps <paramref name="other"/> (shares any point).</summary>
    /// <param name="other">The interval to test against.</param>
    /// <returns><see langword="true"/> when <c>Low &lt;= other.High</c> and <c>other.Low &lt;= High</c>.</returns>
    public bool Overlaps(Interval<T> other)
    {
        var comparer = Comparer<T>.Default;
        return comparer.Compare(Low, other.High) <= 0 && comparer.Compare(other.Low, High) <= 0;
    }
}

/// <summary>
/// The measure carried by an interval tree: an element count, the rightmost interval's low endpoint, and the
/// maximum high endpoint of the range.
/// </summary>
/// <typeparam name="T">Endpoint type.</typeparam>
/// <param name="Count">Number of intervals summarized.</param>
/// <param name="LastLow">Low endpoint of the rightmost interval (used to navigate the low ordering), or
/// <see cref="Optional{T}.None"/> when empty.</param>
/// <param name="MaxHigh">Maximum high endpoint in the range (the stabbing-search signpost), or
/// <see cref="Optional{T}.None"/> when empty.</param>
public readonly record struct IntervalAnnotation<T>(int Count, Optional<T> LastLow, Optional<T> MaxHigh);

/// <summary>
/// Measures an <see cref="Interval{T}"/> for an interval tree: count adds, the low endpoint is right-biased
/// (so a prefix's measure carries its last interval's low, supporting the low ordering), and the high
/// endpoint accumulates as a maximum (driving the stabbing search).
/// </summary>
/// <typeparam name="T">Endpoint type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
public readonly struct IntervalMeasure<T> : IMeasure<Interval<T>, IntervalAnnotation<T>>
{
    /// <inheritdoc/>
    public static IntervalAnnotation<T> Empty => new(0, Optional<T>.None, Optional<T>.None);

    /// <inheritdoc/>
    public static IntervalAnnotation<T> Measure(Interval<T> element) =>
        new(1, Optional<T>.Some(element.Low), Optional<T>.Some(element.High));

    /// <inheritdoc/>
    public static IntervalAnnotation<T> Combine(IntervalAnnotation<T> left, IntervalAnnotation<T> right) =>
        new(
            left.Count + right.Count,
            right.LastLow.HasValue ? right.LastLow : left.LastLow,
            CombineMax(left.MaxHigh, right.MaxHigh));

    private static Optional<T> CombineMax(Optional<T> left, Optional<T> right)
    {
        if (!left.HasValue)
            return right;
        if (!right.HasValue)
            return left;
        return Comparer<T>.Default.Compare(left.Value, right.Value) >= 0 ? left : right;
    }
}

/// <summary>
/// An immutable, persistent interval tree: a collection of closed <see cref="Interval{T}"/> values kept
/// ordered by low endpoint, supporting logarithmic insertion and stabbing/overlap queries.
/// </summary>
/// <typeparam name="T">Endpoint type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
/// <remarks>
/// <para>
/// This is the Hinze–Paterson interval tree, built directly on the general measured
/// <see cref="FingerTree{TElement, TMeasure, TMeasureOps}"/> with <see cref="IntervalMeasure{T}"/>. The
/// maximum-high-endpoint component of the measure lets a single overlap query descend in O(log n): the first
/// interval whose prefix maximum high reaches the query's low is the candidate, and checking its low against
/// the query's high decides the overlap. Because intervals are stored in low order, enumeration is in
/// nondecreasing low-endpoint order.
/// </para>
/// <para>
/// Complexity: <see cref="Count"/>/<see cref="IsEmpty"/> are O(1); <see cref="Insert(Interval{T})"/> is
/// O(log n) amortized; <see cref="TryFindOverlap(Interval{T}, out Interval{T})"/> is O(log n);
/// <see cref="FindOverlaps(Interval{T})"/> is O(k log n) for k results. Bounds are amortized for ephemeral
/// use, matching the underlying strict measured tree.
/// </para>
/// <para>Instances are immutable and safe for concurrent reads.</para>
/// </remarks>
/// <example>
/// <code>
/// var tree = IntervalTree&lt;int&gt;.Empty
///     .Insert(1, 5)
///     .Insert(10, 15)
///     .Insert(3, 8);
///
/// if (tree.TryFindOverlap(new Interval&lt;int&gt;(6, 9), out var hit))
///     Console.WriteLine(hit);        // [3, 8]
///
/// var all = tree.FindOverlaps(new Interval&lt;int&gt;(4, 12));  // [1,5], [3,8], [10,15]
/// </code>
/// </example>
public sealed class IntervalTree<T> : IEnumerable<Interval<T>>
{
    private static readonly IntervalTree<T> EmptyInstance =
        new(FingerTree<Interval<T>, IntervalAnnotation<T>, IntervalMeasure<T>>.Empty);

    private readonly FingerTree<Interval<T>, IntervalAnnotation<T>, IntervalMeasure<T>> _tree;

    private IntervalTree(FingerTree<Interval<T>, IntervalAnnotation<T>, IntervalMeasure<T>> tree) => _tree = tree;

    /// <summary>Gets the empty interval tree.</summary>
    public static IntervalTree<T> Empty => EmptyInstance;

    /// <summary>Gets a value indicating whether the tree has no intervals. O(1).</summary>
    public bool IsEmpty => _tree.IsEmpty;

    /// <summary>Gets the number of intervals. O(1).</summary>
    public int Count => _tree.Measure.Count;

    /// <summary>Builds an interval tree from a sequence of intervals. O(n log n).</summary>
    /// <param name="intervals">Intervals to insert.</param>
    /// <returns>An interval tree containing all of <paramref name="intervals"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="intervals"/> is <see langword="null"/>.</exception>
    public static IntervalTree<T> CreateRange(IEnumerable<Interval<T>> intervals)
    {
        ArgumentNullException.ThrowIfNull(intervals);
        var tree = EmptyInstance;
        foreach (var interval in intervals)
            tree = tree.Insert(interval);
        return tree;
    }

    /// <summary>Inserts an interval, keeping the collection ordered by low endpoint. O(log n) amortized.</summary>
    /// <param name="interval">The interval to insert.</param>
    /// <returns>A new tree containing <paramref name="interval"/>.</returns>
    public IntervalTree<T> Insert(Interval<T> interval)
    {
        var comparer = Comparer<T>.Default;
        var (left, right) = _tree.Split(m => m.LastLow.HasValue && comparer.Compare(m.LastLow.Value, interval.Low) >= 0);
        return new(left.Append(interval).Concat(right));
    }

    /// <summary>Inserts the interval <c>[low, high]</c>, keeping low order. O(log n) amortized.</summary>
    /// <param name="low">Inclusive lower endpoint.</param>
    /// <param name="high">Inclusive upper endpoint.</param>
    /// <returns>A new tree containing the interval.</returns>
    public IntervalTree<T> Insert(T low, T high) => Insert(new Interval<T>(low, high));

    /// <summary>
    /// Finds one interval overlapping <paramref name="query"/>, if any. O(log n).
    /// </summary>
    /// <param name="query">The query interval.</param>
    /// <param name="match">An overlapping interval when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when an overlapping interval exists; otherwise <see langword="false"/>.</returns>
    /// <remarks>
    /// Locates the first interval whose accumulated maximum high reaches <c>query.Low</c> (so its high is at
    /// least <c>query.Low</c>); it overlaps exactly when its low is at most <c>query.High</c>. If that
    /// interval starts after <c>query.High</c>, no interval overlaps, because every later interval starts no
    /// earlier and every earlier interval ends before <c>query.Low</c>.
    /// </remarks>
    public bool TryFindOverlap(Interval<T> query, out Interval<T> match)
    {
        var comparer = Comparer<T>.Default;
        if (!_tree.TryLocate(new MaxHighAtLeastPredicate<T>(comparer, query.Low), out _, out var candidate))
        {
            match = default;
            return false;
        }

        if (comparer.Compare(candidate.Low, query.High) <= 0)
        {
            match = candidate;
            return true;
        }

        match = default;
        return false;
    }

    /// <summary>Finds one interval overlapping <c>[low, high]</c>, if any. O(log n).</summary>
    /// <param name="low">Inclusive lower endpoint of the query.</param>
    /// <param name="high">Inclusive upper endpoint of the query.</param>
    /// <param name="match">An overlapping interval when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when an overlapping interval exists; otherwise <see langword="false"/>.</returns>
    public bool TryFindOverlap(T low, T high, out Interval<T> match) =>
        TryFindOverlap(new Interval<T>(low, high), out match);

    /// <summary>
    /// Finds an interval containing <paramref name="point"/> (a point-stabbing query), if any. O(log n).
    /// </summary>
    /// <param name="point">The point to stab.</param>
    /// <param name="match">A containing interval when one exists; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when some interval contains <paramref name="point"/>; otherwise <see langword="false"/>.</returns>
    public bool TryFindContaining(T point, out Interval<T> match) =>
        TryFindOverlap(new Interval<T>(point, point), out match);

    /// <summary>
    /// Returns every interval overlapping <paramref name="query"/>, in nondecreasing low-endpoint order.
    /// O(k log n) for k results.
    /// </summary>
    /// <param name="query">The query interval.</param>
    /// <returns>All overlapping intervals.</returns>
    public IReadOnlyList<Interval<T>> FindOverlaps(Interval<T> query)
    {
        var comparer = Comparer<T>.Default;
        var results = new List<Interval<T>>();

        // Restrict to intervals whose low is at most the query's high; only these can overlap.
        var (candidates, _) = _tree.Split(m => m.LastLow.HasValue && comparer.Compare(m.LastLow.Value, query.High) > 0);

        // Repeatedly take the leftmost candidate whose high reaches the query's low; everything before it
        // ends too early to overlap, so advance into the suffix after each hit.
        while (candidates.TrySplitFind(
            m => m.MaxHigh.HasValue && comparer.Compare(m.MaxHigh.Value, query.Low) >= 0,
            out _,
            out var hit,
            out var after))
        {
            results.Add(hit);
            candidates = after;
        }

        return results;
    }

    /// <summary>Counts the intervals overlapping <paramref name="query"/>. O(k log n) for k results.</summary>
    /// <param name="query">The query interval.</param>
    /// <returns>The number of overlapping intervals.</returns>
    public int CountOverlaps(Interval<T> query) => FindOverlaps(query).Count;

    /// <summary>
    /// Determines whether an interval matching <paramref name="interval"/> is present, where "matching" means
    /// both endpoints compare equal under <see cref="Comparer{T}.Default"/> (the order the tree is built on).
    /// O(log n) when low endpoints are distinct; otherwise additionally linear in the number of intervals
    /// sharing that low endpoint.
    /// </summary>
    /// <param name="interval">The interval to look for (matched by the comparer on both endpoints).</param>
    /// <returns><see langword="true"/> when present; otherwise <see langword="false"/>.</returns>
    public bool Contains(Interval<T> interval)
    {
        var comparer = Comparer<T>.Default;
        var (_, atOrAfter) = _tree.Split(m => m.LastLow.HasValue && comparer.Compare(m.LastLow.Value, interval.Low) >= 0);
        var current = atOrAfter;
        while (current.TryViewLeft(out var head, out var tail))
        {
            if (comparer.Compare(head.Low, interval.Low) != 0)
                break;
            if (comparer.Compare(head.High, interval.High) == 0)
                return true;
            current = tail;
        }

        return false;
    }

    /// <summary>
    /// Removes one interval matching <paramref name="interval"/>, if present, where "matching" means both
    /// endpoints compare equal under <see cref="Comparer{T}.Default"/> (the order the tree is built on).
    /// O(log n) when low endpoints are distinct; otherwise additionally linear in the number of intervals
    /// sharing that low endpoint.
    /// </summary>
    /// <param name="interval">The interval to remove (matched by the comparer on both endpoints).</param>
    /// <param name="result">The tree with one matching interval removed; otherwise the unchanged tree.</param>
    /// <returns><see langword="true"/> when an interval was removed; otherwise <see langword="false"/>.</returns>
    public bool TryRemove(Interval<T> interval, out IntervalTree<T> result)
    {
        var comparer = Comparer<T>.Default;
        var (left, atOrAfter) = _tree.Split(m => m.LastLow.HasValue && comparer.Compare(m.LastLow.Value, interval.Low) >= 0);

        var skipped = FingerTree<Interval<T>, IntervalAnnotation<T>, IntervalMeasure<T>>.Empty;
        var current = atOrAfter;
        while (current.TryViewLeft(out var head, out var tail))
        {
            if (comparer.Compare(head.Low, interval.Low) != 0)
                break;
            if (comparer.Compare(head.High, interval.High) == 0)
            {
                result = new(left.Concat(skipped).Concat(tail));
                return true;
            }

            skipped = skipped.Append(head);
            current = tail;
        }

        result = this;
        return false;
    }

    /// <summary>
    /// Removes one interval matching <paramref name="interval"/> (both endpoints comparer-equal) if present,
    /// returning the unchanged tree otherwise.
    /// </summary>
    /// <param name="interval">The interval to remove (matched by the comparer on both endpoints).</param>
    /// <returns>The resulting tree.</returns>
    public IntervalTree<T> Remove(Interval<T> interval) => TryRemove(interval, out var result) ? result : this;

    /// <summary>
    /// Returns an interval tree in which overlapping intervals (including those touching at a single point,
    /// per closed-interval semantics) are merged into maximal disjoint intervals. O(n).
    /// </summary>
    /// <returns>A tree of disjoint merged intervals in low-endpoint order.</returns>
    /// <remarks>
    /// Intervals are merged only when they share a point; intervals that are merely adjacent with a gap
    /// (for example <c>[1, 2]</c> and <c>[4, 5]</c>) are left separate, since "adjacency" would require a
    /// successor operation that an arbitrary <typeparamref name="T"/> does not provide.
    /// </remarks>
    public IntervalTree<T> Coalesce()
    {
        if (_tree.IsEmpty)
            return this;

        var comparer = Comparer<T>.Default;
        var merged = FingerTree<Interval<T>, IntervalAnnotation<T>, IntervalMeasure<T>>.Empty;
        var current = default(Interval<T>);
        var hasCurrent = false;

        foreach (var interval in _tree)
        {
            if (!hasCurrent)
            {
                current = interval;
                hasCurrent = true;
                continue;
            }

            if (comparer.Compare(interval.Low, current.High) <= 0)
            {
                // Sorted by low, so interval.Low >= current.Low; they overlap, so extend the running high.
                var high = comparer.Compare(interval.High, current.High) >= 0 ? interval.High : current.High;
                current = new Interval<T>(current.Low, high);
            }
            else
            {
                merged = merged.Append(current);
                current = interval;
            }
        }

        merged = merged.Append(current);
        return new(merged);
    }

    /// <summary>Copies the intervals to a new array in low-endpoint order. O(n).</summary>
    /// <returns>A new array of the intervals in low order.</returns>
    public Interval<T>[] ToArray() => _tree.ToArray();

    /// <summary>Returns an enumerator over the intervals in nondecreasing low-endpoint order.</summary>
    /// <returns>An enumerator over the intervals.</returns>
    public IEnumerator<Interval<T>> GetEnumerator() => _tree.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
