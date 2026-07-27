// A numeric sum measure: each element measures itself and measures add, so a tree's measure is the
// running total of its elements.

using System.Diagnostics.CodeAnalysis;
using System.Numerics;

namespace Durable7.FingerTree;

/// <summary>
/// A numeric sum measure: each element measures itself and measures add, so a tree's measure is the running
/// total of its elements. With it, a measured finger tree becomes a persistent Fenwick-style structure —
/// O(1) total, O(log n) cumulative-weight split, and O(log n) weighted selection.
/// </summary>
/// <typeparam name="T">A numeric element/weight type, via generic math (<see cref="IAdditionOperators{TSelf, TOther, TResult}"/>
/// and <see cref="IAdditiveIdentity{TSelf, TResult}"/>); for example <see cref="int"/>, <see cref="long"/>,
/// <see cref="double"/>, <see cref="decimal"/>, or <see cref="BigInteger"/>.</typeparam>
/// <remarks>
/// <para>
/// The monoid is ordinary addition with the additive identity, so the measure of any range is the sum of its
/// elements. Splitting by the accumulated sum performs weighted selection: the element whose cumulative
/// interval contains a target weight is found in O(log n) — the persistent analogue of a Fenwick tree's
/// "find by cumulative frequency," and the core of weighted random sampling. See
/// <see cref="FingerTreeSumExtensions"/> for the named operations.
/// </para>
/// <para>
/// Floating-point addition is not exactly associative; with <see cref="double"/> or <see cref="float"/>
/// weights the cumulative sums (and therefore split boundaries) are subject to the usual rounding, which the
/// caller must tolerate. The sum measure carries no count, so positional ("first k elements") queries need a
/// separate size component — combine it with <see cref="SizeMeasure{TElement}"/> in a product measure when
/// both are required.
/// </para>
/// </remarks>
public readonly struct SumMeasure<T> : IMeasure<T, T>
    where T : IAdditionOperators<T, T, T>, IAdditiveIdentity<T, T>
{
    /// <inheritdoc/>
    public static T Empty => T.AdditiveIdentity;

    /// <inheritdoc/>
    public static T Measure(T element) => element;

    /// <inheritdoc/>
    public static T Combine(T left, T right) => left + right;
}

/// <summary>
/// Weighted-selection operations over a sum-measured finger tree (<see cref="SumMeasure{T}"/>).
/// </summary>
public static class FingerTreeSumExtensions
{
    /// <summary>
    /// Splits a sum-measured tree at the first point where the running total exceeds <paramref name="threshold"/>:
    /// <c>Left</c> is the longest prefix whose total is at most <paramref name="threshold"/>, <c>Right</c> the
    /// rest. O(log n).
    /// </summary>
    /// <typeparam name="T">Numeric element type.</typeparam>
    /// <param name="tree">A tree measured by <see cref="SumMeasure{T}"/>.</param>
    /// <param name="threshold">The cumulative-weight threshold.</param>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> is <see langword="null"/>.</exception>
    public static (FingerTree<T, T, SumMeasure<T>> Left, FingerTree<T, T, SumMeasure<T>> Right)
        /// <summary>Splits at the first point where the accumulated weight passes the threshold.</summary>
        SplitByCumulativeWeight<T>(this FingerTree<T, T, SumMeasure<T>> tree, T threshold)
        where T : IAdditionOperators<T, T, T>, IAdditiveIdentity<T, T>, IComparisonOperators<T, T, bool>
    {
        ArgumentNullException.ThrowIfNull(tree);
        return tree.Split(new SumAbovePredicate<T>(threshold));
    }

    /// <summary>
    /// Selects the element whose cumulative-weight interval contains <paramref name="threshold"/> — the first
    /// element at which the running total exceeds it. With weights summing to <c>total</c>, every
    /// <paramref name="threshold"/> in <c>[0, total)</c> selects exactly one element, so drawing the threshold
    /// uniformly samples elements in proportion to their weight. O(log n).
    /// </summary>
    /// <typeparam name="T">Numeric element type.</typeparam>
    /// <param name="tree">A tree measured by <see cref="SumMeasure{T}"/>.</param>
    /// <param name="threshold">The cumulative weight to locate.</param>
    /// <param name="selected">The selected element when one exists; otherwise <see langword="default"/>.</param>
    /// <param name="weightBefore">The summed weight of the elements before the selected one.</param>
    /// <returns>
    /// <see langword="true"/> when an element's interval contains <paramref name="threshold"/> (i.e. the total
    /// weight exceeds it); otherwise <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="tree"/> is <see langword="null"/>.</exception>
    /// <example>
    /// <code>
    /// var weights = FingerTree&lt;int, int, SumMeasure&lt;int&gt;&gt;.Create(5, 1, 4); // total 10
    /// var draw = random.Next(10);
    /// weights.TrySelectByCumulativeWeight(draw, out var picked, out _); // P(index 0)=5/10, 1=1/10, 2=4/10
    /// </code>
    /// </example>
    public static bool TrySelectByCumulativeWeight<T>(
        this FingerTree<T, T, SumMeasure<T>> tree,
        T threshold,
        [MaybeNullWhen(false)] out T selected,
        out T weightBefore)
        where T : IAdditionOperators<T, T, T>, IAdditiveIdentity<T, T>, IComparisonOperators<T, T, bool>
    {
        ArgumentNullException.ThrowIfNull(tree);
        if (tree.TrySplitFind(new SumAbovePredicate<T>(threshold), out var before, out selected, out _))
        {
            weightBefore = before.Measure;
            return true;
        }

        selected = default;
        weightBefore = T.AdditiveIdentity;
        return false;
    }
}
