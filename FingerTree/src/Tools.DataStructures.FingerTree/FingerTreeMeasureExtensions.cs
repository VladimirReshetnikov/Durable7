namespace Tools.DataStructures.FingerTree;

/// <summary>
/// Named operations over <see cref="FingerTree{TElement, TMeasure, TMeasureOps}"/> instances built with the
/// ready-made measures (<see cref="MaxMeasure{T}"/>, <see cref="MinMeasure{T}"/>, <see cref="KeyMeasure{T}"/>,
/// <see cref="OrderStatisticMeasure{T}"/>), so callers express priority-queue, ordered-set, and
/// order-statistic intent directly instead of building split predicates by hand.
/// </summary>
public static class FingerTreeMeasureExtensions
{
    /// <summary>Reads the maximum element of a max-measured tree without removing it. O(1).</summary>
    /// <typeparam name="T">Element type.</typeparam>
    /// <param name="tree">A tree measured by <see cref="MaxMeasure{T}"/>.</param>
    /// <param name="max">The maximum element when non-empty; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when the tree is non-empty; otherwise <see langword="false"/>.</returns>
    public static bool TryPeekMax<T>(this FingerTree<T, Optional<T>, MaxMeasure<T>> tree, out T max)
    {
        ArgumentNullException.ThrowIfNull(tree);
        var measure = tree.Measure;
        max = measure.HasValue ? measure.Value : default!;
        return measure.HasValue;
    }

    /// <summary>
    /// Removes one occurrence of the maximum element of a max-measured tree (the first, in sequence order).
    /// O(log n) amortized.
    /// </summary>
    /// <typeparam name="T">Element type.</typeparam>
    /// <param name="tree">A tree measured by <see cref="MaxMeasure{T}"/>.</param>
    /// <param name="max">The removed maximum element when non-empty; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining tree with that occurrence removed.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    /// <example>
    /// <code>
    /// var pq = FingerTree&lt;int, Optional&lt;int&gt;, MaxMeasure&lt;int&gt;&gt;.Create(3, 9, 1, 9, 4);
    /// while (pq.TryExtractMax(out var top, out pq))
    ///     Console.WriteLine(top);   // 9, 9, 4, 3, 1
    /// </code>
    /// </example>
    public static bool TryExtractMax<T>(
        this FingerTree<T, Optional<T>, MaxMeasure<T>> tree,
        out T max,
        out FingerTree<T, Optional<T>, MaxMeasure<T>> rest)
    {
        ArgumentNullException.ThrowIfNull(tree);
        if (tree.IsEmpty)
        {
            max = default!;
            rest = tree;
            return false;
        }

        var target = tree.Measure.Value;
        var comparer = Comparer<T>.Default;
        tree.TrySplitFind(m => m.HasValue && comparer.Compare(m.Value, target) >= 0, out var before, out max, out var after);
        rest = before.Concat(after);
        return true;
    }

    /// <summary>Reads the minimum element of a min-measured tree without removing it. O(1).</summary>
    /// <typeparam name="T">Element type.</typeparam>
    /// <param name="tree">A tree measured by <see cref="MinMeasure{T}"/>.</param>
    /// <param name="min">The minimum element when non-empty; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when the tree is non-empty; otherwise <see langword="false"/>.</returns>
    public static bool TryPeekMin<T>(this FingerTree<T, Optional<T>, MinMeasure<T>> tree, out T min)
    {
        ArgumentNullException.ThrowIfNull(tree);
        var measure = tree.Measure;
        min = measure.HasValue ? measure.Value : default!;
        return measure.HasValue;
    }

    /// <summary>
    /// Removes one occurrence of the minimum element of a min-measured tree (the first, in sequence order).
    /// O(log n) amortized.
    /// </summary>
    /// <typeparam name="T">Element type.</typeparam>
    /// <param name="tree">A tree measured by <see cref="MinMeasure{T}"/>.</param>
    /// <param name="min">The removed minimum element when non-empty; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining tree with that occurrence removed.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public static bool TryExtractMin<T>(
        this FingerTree<T, Optional<T>, MinMeasure<T>> tree,
        out T min,
        out FingerTree<T, Optional<T>, MinMeasure<T>> rest)
    {
        ArgumentNullException.ThrowIfNull(tree);
        if (tree.IsEmpty)
        {
            min = default!;
            rest = tree;
            return false;
        }

        var target = tree.Measure.Value;
        var comparer = Comparer<T>.Default;
        tree.TrySplitFind(m => m.HasValue && comparer.Compare(m.Value, target) <= 0, out var before, out min, out var after);
        rest = before.Concat(after);
        return true;
    }

    /// <summary>
    /// Splits a key-measured tree at the lower bound for <paramref name="key"/>: every element of
    /// <c>Left</c> compares less than <paramref name="key"/>, every element of <c>Right</c> compares greater
    /// than or equal. The result is specified only when the tree is sorted by <see cref="Comparer{T}.Default"/>.
    /// O(log(min(k, n − k))) amortized.
    /// </summary>
    /// <typeparam name="T">Element type, sorted by <see cref="Comparer{T}.Default"/>.</typeparam>
    /// <param name="tree">A sorted tree measured by <see cref="KeyMeasure{T}"/>.</param>
    /// <param name="key">The search key.</param>
    public static (FingerTree<T, Optional<T>, KeyMeasure<T>> Left, FingerTree<T, Optional<T>, KeyMeasure<T>> Right)
        SplitByLowerBound<T>(this FingerTree<T, Optional<T>, KeyMeasure<T>> tree, T key)
    {
        ArgumentNullException.ThrowIfNull(tree);
        var comparer = Comparer<T>.Default;
        return tree.Split(m => m.HasValue && comparer.Compare(m.Value, key) >= 0);
    }

    /// <summary>
    /// Splits a key-measured tree at the upper bound for <paramref name="key"/>: every element of
    /// <c>Left</c> compares less than or equal to <paramref name="key"/>, every element of <c>Right</c>
    /// compares greater. The result is specified only when the tree is sorted by
    /// <see cref="Comparer{T}.Default"/>. O(log(min(k, n − k))) amortized.
    /// </summary>
    /// <typeparam name="T">Element type, sorted by <see cref="Comparer{T}.Default"/>.</typeparam>
    /// <param name="tree">A sorted tree measured by <see cref="KeyMeasure{T}"/>.</param>
    /// <param name="key">The search key.</param>
    public static (FingerTree<T, Optional<T>, KeyMeasure<T>> Left, FingerTree<T, Optional<T>, KeyMeasure<T>> Right)
        SplitByUpperBound<T>(this FingerTree<T, Optional<T>, KeyMeasure<T>> tree, T key)
    {
        ArgumentNullException.ThrowIfNull(tree);
        var comparer = Comparer<T>.Default;
        return tree.Split(m => m.HasValue && comparer.Compare(m.Value, key) > 0);
    }

    /// <summary>
    /// Splits an order-statistic tree positionally so that <c>Left</c> holds exactly the first
    /// <paramref name="index"/> elements. O(log(min(index, n − index))) amortized.
    /// </summary>
    /// <typeparam name="T">Element type.</typeparam>
    /// <param name="tree">A tree measured by <see cref="OrderStatisticMeasure{T}"/>.</param>
    /// <param name="index">Number of elements to place in the left part; clamped conceptually to the count.</param>
    public static (FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> Left, FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> Right)
        SplitAtIndex<T>(this FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> tree, int index)
    {
        ArgumentNullException.ThrowIfNull(tree);
        return tree.Split(m => m.Count > index);
    }

    /// <summary>
    /// Splits an order-statistic tree at the lower bound for <paramref name="key"/> using the key component.
    /// The result is specified only when the tree is sorted by <see cref="Comparer{T}.Default"/>.
    /// O(log(min(k, n − k))) amortized.
    /// </summary>
    /// <typeparam name="T">Element type, sorted by <see cref="Comparer{T}.Default"/>.</typeparam>
    /// <param name="tree">A sorted tree measured by <see cref="OrderStatisticMeasure{T}"/>.</param>
    /// <param name="key">The search key.</param>
    public static (FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> Left, FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> Right)
        SplitByLowerBound<T>(this FingerTree<T, RankedKey<T>, OrderStatisticMeasure<T>> tree, T key)
    {
        ArgumentNullException.ThrowIfNull(tree);
        var comparer = Comparer<T>.Default;
        return tree.Split(m => m.Key.HasValue && comparer.Compare(m.Key.Value, key) >= 0);
    }
}
