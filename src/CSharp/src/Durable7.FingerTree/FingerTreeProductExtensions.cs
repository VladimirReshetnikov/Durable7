using System.Diagnostics.CodeAnalysis;
using System.Numerics;

namespace Durable7.FingerTree;

/// <summary>
/// Operations over product-measured trees
/// (<see cref="ProductMeasure{TElement, TFirst, TSecond, TFirstOps, TSecondOps}"/>): the component-projecting
/// splits that work for any pairing, plus named operations for the headline size + sum, size + max, and
/// size + min compositions so callers express positional, cumulative-weight, and priority-queue intent
/// directly. The five type arguments are inferred from the tree argument, so call sites read as
/// <c>tree.SplitByFirst(...)</c> with no explicit type arguments.
/// </summary>
public static class FingerTreeProductExtensions
{
    // ---- Generic component-projecting splits (any product) ---------------------------------------------

    /// <summary>
    /// Splits a product-measured tree by a monotone predicate over the <em>first</em> component's accumulated
    /// measure. O(log(min(k, n − k))) amortized for a boundary at distance k from the nearer end.
    /// </summary>
    /// <typeparam name="TElement">Stored element type.</typeparam>
    /// <typeparam name="TFirst">First component measure type.</typeparam>
    /// <typeparam name="TSecond">Second component measure type.</typeparam>
    /// <typeparam name="TFirstOps">First component measure algebra.</typeparam>
    /// <typeparam name="TSecondOps">Second component measure algebra.</typeparam>
    /// <param name="tree">A product-measured tree.</param>
    /// <param name="predicate">A monotone predicate over the accumulated first component.</param>
    /// <returns>
    /// <c>Left</c> is the longest prefix whose first component never satisfies <paramref name="predicate"/>;
    /// <c>Right</c> is the rest, headed by the boundary element. <c>Left.Concat(Right)</c> reproduces the tree.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> or <paramref name="predicate"/> is
    /// <see langword="null"/>.</exception>
    public static (
        FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> Left,
        FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> Right)
        SplitByFirst<TElement, TFirst, TSecond, TFirstOps, TSecondOps>(
            this FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> tree,
            Func<TFirst, bool> predicate)
        where TFirstOps : IMeasure<TElement, TFirst>
        where TSecondOps : IMeasure<TElement, TSecond>
    {
        ArgumentNullException.ThrowIfNull(tree);
        ArgumentNullException.ThrowIfNull(predicate);
        return tree.Split(new FirstComponentPredicate<FuncMeasurePredicate<TFirst>, TFirst, TSecond>(new FuncMeasurePredicate<TFirst>(predicate)));
    }

    /// <summary>
    /// Splits a product-measured tree by a monotone predicate over the <em>second</em> component's accumulated
    /// measure. O(log(min(k, n − k))) amortized.
    /// </summary>
    /// <typeparam name="TElement">Stored element type.</typeparam>
    /// <typeparam name="TFirst">First component measure type.</typeparam>
    /// <typeparam name="TSecond">Second component measure type.</typeparam>
    /// <typeparam name="TFirstOps">First component measure algebra.</typeparam>
    /// <typeparam name="TSecondOps">Second component measure algebra.</typeparam>
    /// <param name="tree">A product-measured tree.</param>
    /// <param name="predicate">A monotone predicate over the accumulated second component.</param>
    /// <returns>
    /// <c>Left</c> is the longest prefix whose second component never satisfies <paramref name="predicate"/>;
    /// <c>Right</c> is the rest, headed by the boundary element.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> or <paramref name="predicate"/> is
    /// <see langword="null"/>.</exception>
    public static (
        FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> Left,
        FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> Right)
        SplitBySecond<TElement, TFirst, TSecond, TFirstOps, TSecondOps>(
            this FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> tree,
            Func<TSecond, bool> predicate)
        where TFirstOps : IMeasure<TElement, TFirst>
        where TSecondOps : IMeasure<TElement, TSecond>
    {
        ArgumentNullException.ThrowIfNull(tree);
        ArgumentNullException.ThrowIfNull(predicate);
        return tree.Split(new SecondComponentPredicate<FuncMeasurePredicate<TSecond>, TFirst, TSecond>(new FuncMeasurePredicate<TSecond>(predicate)));
    }

    /// <summary>
    /// Splits a product-measured tree around the element where a monotone predicate over the <em>first</em>
    /// component first becomes true, returning that boundary element separately. O(log(min(k, n − k))) amortized.
    /// </summary>
    /// <typeparam name="TElement">Stored element type.</typeparam>
    /// <typeparam name="TFirst">First component measure type.</typeparam>
    /// <typeparam name="TSecond">Second component measure type.</typeparam>
    /// <typeparam name="TFirstOps">First component measure algebra.</typeparam>
    /// <typeparam name="TSecondOps">Second component measure algebra.</typeparam>
    /// <param name="tree">A product-measured tree.</param>
    /// <param name="predicate">A monotone predicate over the accumulated first component.</param>
    /// <param name="left">Elements before the boundary element.</param>
    /// <param name="found">The boundary element — the first whose inclusion makes the predicate true.</param>
    /// <param name="right">Elements after the boundary element.</param>
    /// <returns><see langword="true"/> when a boundary element exists; otherwise <see langword="false"/>, with
    /// <paramref name="left"/> set to the whole tree.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> or <paramref name="predicate"/> is
    /// <see langword="null"/>.</exception>
    public static bool TrySplitFindByFirst<TElement, TFirst, TSecond, TFirstOps, TSecondOps>(
        this FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> tree,
        Func<TFirst, bool> predicate,
        out FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> left,
        out TElement found,
        out FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> right)
        where TFirstOps : IMeasure<TElement, TFirst>
        where TSecondOps : IMeasure<TElement, TSecond>
    {
        ArgumentNullException.ThrowIfNull(tree);
        ArgumentNullException.ThrowIfNull(predicate);
        return tree.TrySplitFind(new FirstComponentPredicate<FuncMeasurePredicate<TFirst>, TFirst, TSecond>(new FuncMeasurePredicate<TFirst>(predicate)), out left, out found, out right);
    }

    /// <summary>
    /// Splits a product-measured tree around the element where a monotone predicate over the <em>second</em>
    /// component first becomes true, returning that boundary element separately. O(log(min(k, n − k))) amortized.
    /// </summary>
    /// <typeparam name="TElement">Stored element type.</typeparam>
    /// <typeparam name="TFirst">First component measure type.</typeparam>
    /// <typeparam name="TSecond">Second component measure type.</typeparam>
    /// <typeparam name="TFirstOps">First component measure algebra.</typeparam>
    /// <typeparam name="TSecondOps">Second component measure algebra.</typeparam>
    /// <param name="tree">A product-measured tree.</param>
    /// <param name="predicate">A monotone predicate over the accumulated second component.</param>
    /// <param name="left">Elements before the boundary element.</param>
    /// <param name="found">The boundary element — the first whose inclusion makes the predicate true.</param>
    /// <param name="right">Elements after the boundary element.</param>
    /// <returns><see langword="true"/> when a boundary element exists; otherwise <see langword="false"/>, with
    /// <paramref name="left"/> set to the whole tree.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> or <paramref name="predicate"/> is
    /// <see langword="null"/>.</exception>
    public static bool TrySplitFindBySecond<TElement, TFirst, TSecond, TFirstOps, TSecondOps>(
        this FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> tree,
        Func<TSecond, bool> predicate,
        out FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> left,
        out TElement found,
        out FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> right)
        where TFirstOps : IMeasure<TElement, TFirst>
        where TSecondOps : IMeasure<TElement, TSecond>
    {
        ArgumentNullException.ThrowIfNull(tree);
        ArgumentNullException.ThrowIfNull(predicate);
        return tree.TrySplitFind(new SecondComponentPredicate<FuncMeasurePredicate<TSecond>, TFirst, TSecond>(new FuncMeasurePredicate<TSecond>(predicate)), out left, out found, out right);
    }

    // ---- Generic component-projecting splits with value-type predicates (zero closure) -----------------

    /// <summary>Closure-free overload of <see cref="SplitByFirst{TElement, TFirst, TSecond, TFirstOps,
    /// TSecondOps}(FingerTree{TElement, MeasurePair{TFirst, TSecond}, ProductMeasure{TElement, TFirst, TSecond,
    /// TFirstOps, TSecondOps}}, Func{TFirst, bool})"/> taking a value-type predicate over the first component,
    /// so the split allocates no delegate. Identical behavior otherwise.</summary>
    public static (
        FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> Left,
        FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> Right)
        SplitByFirst<TElement, TFirst, TSecond, TFirstOps, TSecondOps, TPredicate>(
            this FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> tree,
            TPredicate predicate)
        where TFirstOps : IMeasure<TElement, TFirst>
        where TSecondOps : IMeasure<TElement, TSecond>
        where TPredicate : struct, IMeasurePredicate<TFirst>
    {
        ArgumentNullException.ThrowIfNull(tree);
        return tree.Split(new FirstComponentPredicate<TPredicate, TFirst, TSecond>(predicate));
    }

    /// <summary>Closure-free, value-type-predicate counterpart of the second-component split. Identical behavior
    /// to the <see cref="Func{TSecond, Boolean}"/> overload, but allocates no delegate.</summary>
    public static (
        FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> Left,
        FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> Right)
        SplitBySecond<TElement, TFirst, TSecond, TFirstOps, TSecondOps, TPredicate>(
            this FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> tree,
            TPredicate predicate)
        where TFirstOps : IMeasure<TElement, TFirst>
        where TSecondOps : IMeasure<TElement, TSecond>
        where TPredicate : struct, IMeasurePredicate<TSecond>
    {
        ArgumentNullException.ThrowIfNull(tree);
        return tree.Split(new SecondComponentPredicate<TPredicate, TFirst, TSecond>(predicate));
    }

    /// <summary>Closure-free, value-type-predicate counterpart of the first-component split-find. Identical
    /// behavior to the <see cref="Func{TFirst, Boolean}"/> overload, but allocates no delegate.</summary>
    public static bool TrySplitFindByFirst<TElement, TFirst, TSecond, TFirstOps, TSecondOps, TPredicate>(
        this FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> tree,
        TPredicate predicate,
        out FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> left,
        out TElement found,
        out FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> right)
        where TFirstOps : IMeasure<TElement, TFirst>
        where TSecondOps : IMeasure<TElement, TSecond>
        where TPredicate : struct, IMeasurePredicate<TFirst>
    {
        ArgumentNullException.ThrowIfNull(tree);
        return tree.TrySplitFind(new FirstComponentPredicate<TPredicate, TFirst, TSecond>(predicate), out left, out found, out right);
    }

    /// <summary>Closure-free, value-type-predicate counterpart of the second-component split-find. Identical
    /// behavior to the <see cref="Func{TSecond, Boolean}"/> overload, but allocates no delegate.</summary>
    public static bool TrySplitFindBySecond<TElement, TFirst, TSecond, TFirstOps, TSecondOps, TPredicate>(
        this FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> tree,
        TPredicate predicate,
        out FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> left,
        out TElement found,
        out FingerTree<TElement, MeasurePair<TFirst, TSecond>, ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>> right)
        where TFirstOps : IMeasure<TElement, TFirst>
        where TSecondOps : IMeasure<TElement, TSecond>
        where TPredicate : struct, IMeasurePredicate<TSecond>
    {
        ArgumentNullException.ThrowIfNull(tree);
        return tree.TrySplitFind(new SecondComponentPredicate<TPredicate, TFirst, TSecond>(predicate), out left, out found, out right);
    }

    // ---- Size + sum (positional and Fenwick) -----------------------------------------------------------

    /// <summary>
    /// Splits a size + sum tree positionally so that <c>Left</c> holds exactly the first
    /// <paramref name="index"/> elements (fewer if the tree is shorter). O(log(min(index, n − index)))
    /// amortized.
    /// </summary>
    /// <typeparam name="T">A numeric element/weight type (generic math).</typeparam>
    /// <param name="tree">A size + sum tree (see <see cref="ProductMeasures.CreateSizeAndSum{T}"/>).</param>
    /// <param name="index">Number of elements to place in the left part.</param>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> is <see langword="null"/>.</exception>
    public static (
        FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>> Left,
        FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>> Right)
        SplitAtIndex<T>(this FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>> tree, int index)
        where T : IAdditionOperators<T, T, T>, IAdditiveIdentity<T, T> =>
        tree.SplitByFirst(new IntAbovePredicate(index));

    /// <summary>
    /// Splits a size + sum tree at the first point where the cumulative sum exceeds
    /// <paramref name="threshold"/>: <c>Left</c> is the longest prefix whose total is at most
    /// <paramref name="threshold"/>, <c>Right</c> the rest. O(log n) amortized.
    /// </summary>
    /// <typeparam name="T">A numeric element/weight type (generic math).</typeparam>
    /// <param name="tree">A size + sum tree (see <see cref="ProductMeasures.CreateSizeAndSum{T}"/>).</param>
    /// <param name="threshold">The cumulative-weight threshold.</param>
    /// <remarks>
    /// As with <see cref="SumMeasure{T}"/>, floating-point addition is not exactly associative; with
    /// <see cref="double"/> or <see cref="float"/> weights the accumulated sums — and therefore the split
    /// boundary — are subject to the usual rounding, which the caller must tolerate.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> is <see langword="null"/>.</exception>
    public static (
        FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>> Left,
        FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>> Right)
        SplitByCumulativeWeight<T>(this FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>> tree, T threshold)
        where T : IAdditionOperators<T, T, T>, IAdditiveIdentity<T, T>, IComparisonOperators<T, T, bool> =>
        tree.SplitBySecond(new SumAbovePredicate<T>(threshold));

    /// <summary>
    /// Selects the element whose cumulative-weight interval contains <paramref name="threshold"/> — the first
    /// at which the running total exceeds it — and reports its zero-based index. With weights summing to
    /// <c>total</c>, every <paramref name="threshold"/> in <c>[0, total)</c> selects exactly one element, so a
    /// uniform draw samples elements in proportion to their weight. Unlike the pure
    /// <see cref="SumMeasure{T}"/> selection, the size component yields the selected element's position for
    /// free. O(log n) amortized.
    /// </summary>
    /// <typeparam name="T">A numeric element/weight type (generic math).</typeparam>
    /// <param name="tree">A size + sum tree (see <see cref="ProductMeasures.CreateSizeAndSum{T}"/>).</param>
    /// <param name="threshold">The cumulative weight to locate.</param>
    /// <param name="selected">The selected element when one exists; otherwise <see langword="default"/>.</param>
    /// <param name="indexBefore">The number of elements before the selected one (its zero-based index).</param>
    /// <returns><see langword="true"/> when an element's interval contains <paramref name="threshold"/> (the
    /// total weight exceeds it); otherwise <see langword="false"/>.</returns>
    /// <remarks>
    /// As with <see cref="SumMeasure{T}"/>, with <see cref="double"/> or <see cref="float"/> weights the
    /// cumulative sums (and therefore the selection boundary) are subject to floating-point rounding.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> is <see langword="null"/>.</exception>
    /// <example>
    /// <code>
    /// var weights = ProductMeasures.CreateSizeAndSum(5, 1, 4);   // total 10
    /// // threshold 7 falls in the third element's interval [6, 10):
    /// weights.TrySelectByCumulativeWeight(7, out var picked, out var index); // picked = 4, index = 2
    /// </code>
    /// </example>
    public static bool TrySelectByCumulativeWeight<T>(
        this FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>> tree,
        T threshold,
        [MaybeNullWhen(false)] out T selected,
        out int indexBefore)
        where T : IAdditionOperators<T, T, T>, IAdditiveIdentity<T, T>, IComparisonOperators<T, T, bool>
    {
        ArgumentNullException.ThrowIfNull(tree);
        if (tree.TrySplitFindBySecond(new SumAbovePredicate<T>(threshold), out var before, out selected, out _))
        {
            indexBefore = before.Measure.First;
            return true;
        }

        selected = default;
        indexBefore = 0;
        return false;
    }

    // ---- Size + max (positional and max-priority) ------------------------------------------------------

    /// <summary>Reads the maximum element of a size + max tree without removing it. O(1).</summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
    /// <param name="tree">A size + max tree (see <see cref="ProductMeasures.CreateSizeAndMax{T}"/>).</param>
    /// <param name="max">The maximum element when non-empty; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when the tree is non-empty; otherwise <see langword="false"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> is <see langword="null"/>.</exception>
    public static bool TryPeekMax<T>(
        this FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MaxMeasure<T>>> tree,
        [MaybeNullWhen(false)] out T max)
    {
        ArgumentNullException.ThrowIfNull(tree);
        var measure = tree.Measure.Second;
        max = measure.HasValue ? measure.Value : default!;
        return measure.HasValue;
    }

    /// <summary>
    /// Removes one occurrence of the maximum element of a size + max tree (the first, in sequence order).
    /// O(log n) amortized.
    /// </summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
    /// <param name="tree">A size + max tree (see <see cref="ProductMeasures.CreateSizeAndMax{T}"/>).</param>
    /// <param name="max">The removed maximum element when non-empty; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining tree with that occurrence removed.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> is <see langword="null"/>.</exception>
    public static bool TryExtractMax<T>(
        this FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MaxMeasure<T>>> tree,
        [MaybeNullWhen(false)] out T max,
        out FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MaxMeasure<T>>> rest)
    {
        ArgumentNullException.ThrowIfNull(tree);
        if (tree.IsEmpty)
        {
            max = default!;
            rest = tree;
            return false;
        }

        var target = tree.Measure.Second.Value;
        tree.TrySplitFindBySecond(new OptionalAtLeastPredicate<T>(Comparer<T>.Default, target), out var before, out max, out var after);
        rest = before.Concat(after);
        return true;
    }

    // ---- Size + min (positional and min-priority) ------------------------------------------------------

    /// <summary>Reads the minimum element of a size + min tree without removing it. O(1).</summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
    /// <param name="tree">A size + min tree (see <see cref="ProductMeasures.CreateSizeAndMin{T}"/>).</param>
    /// <param name="min">The minimum element when non-empty; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when the tree is non-empty; otherwise <see langword="false"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> is <see langword="null"/>.</exception>
    public static bool TryPeekMin<T>(
        this FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MinMeasure<T>>> tree,
        [MaybeNullWhen(false)] out T min)
    {
        ArgumentNullException.ThrowIfNull(tree);
        var measure = tree.Measure.Second;
        min = measure.HasValue ? measure.Value : default!;
        return measure.HasValue;
    }

    /// <summary>
    /// Removes one occurrence of the minimum element of a size + min tree (the first, in sequence order).
    /// O(log n) amortized.
    /// </summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
    /// <param name="tree">A size + min tree (see <see cref="ProductMeasures.CreateSizeAndMin{T}"/>).</param>
    /// <param name="min">The removed minimum element when non-empty; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining tree with that occurrence removed.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> is <see langword="null"/>.</exception>
    public static bool TryExtractMin<T>(
        this FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MinMeasure<T>>> tree,
        [MaybeNullWhen(false)] out T min,
        out FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MinMeasure<T>>> rest)
    {
        ArgumentNullException.ThrowIfNull(tree);
        if (tree.IsEmpty)
        {
            min = default!;
            rest = tree;
            return false;
        }

        var target = tree.Measure.Second.Value;
        tree.TrySplitFindBySecond(new OptionalAtMostPredicate<T>(Comparer<T>.Default, target), out var before, out min, out var after);
        rest = before.Concat(after);
        return true;
    }
}
