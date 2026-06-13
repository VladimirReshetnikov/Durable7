using System.Numerics;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// The carrier for a <see cref="ProductMeasure{TElement, TFirst, TSecond, TFirstOps, TSecondOps}"/>: a pair of
/// independently combined component measures. Splitting a product-measured tree projects a predicate onto one
/// component, so a single tree can answer questions about either axis at once.
/// </summary>
/// <typeparam name="TFirst">The first component's measure type.</typeparam>
/// <typeparam name="TSecond">The second component's measure type.</typeparam>
/// <param name="First">The first component's accumulated measure.</param>
/// <param name="Second">The second component's accumulated measure.</param>
/// <remarks>
/// A named record struct is used rather than <see cref="ValueTuple{T1, T2}"/> so the carrier reads clearly in
/// API signatures, carries its own XML documentation, and never collides with the language's tuple syntax in
/// split predicates (<c>m =&gt; m.First &gt; k</c> rather than <c>m =&gt; m.Item1 &gt; k</c>).
/// </remarks>
public readonly record struct MeasurePair<TFirst, TSecond>(TFirst First, TSecond Second);

/// <summary>
/// Combines two independent monoidal measures into one, so a single
/// <see cref="FingerTree{TElement, TMeasure, TMeasureOps}"/> can be split by either component without
/// re-measuring. The product of two monoids is a monoid: identity is the pair of identities and
/// <see cref="Combine"/> acts component-wise, so the monoid laws are inherited from the two factors.
/// </summary>
/// <typeparam name="TElement">Stored element type.</typeparam>
/// <typeparam name="TFirst">The first component's measure type (for example <see cref="int"/> for a count).</typeparam>
/// <typeparam name="TSecond">The second component's measure type (for example the element type for a sum).</typeparam>
/// <typeparam name="TFirstOps">The first component's measure algebra; must satisfy
/// <see cref="IMeasure{TElement, TFirst}"/>.</typeparam>
/// <typeparam name="TSecondOps">The second component's measure algebra; must satisfy
/// <see cref="IMeasure{TElement, TSecond}"/>.</typeparam>
/// <remarks>
/// <para>
/// Composing two existing measures avoids hand-rolling a bespoke product. The headline pairings are
/// <see cref="SizeMeasure{TElement}"/> + <see cref="SumMeasure{T}"/> (a sequence that is positional
/// <em>and</em> a Fenwick-style cumulative-weight structure) and <see cref="SizeMeasure{TElement}"/> +
/// <see cref="MaxMeasure{T}"/> / <see cref="MinMeasure{T}"/> (a priority queue with positional access). Project
/// a split onto a component with
/// <see cref="FingerTreeProductExtensions.SplitByFirst{TElement, TFirst, TSecond, TFirstOps, TSecondOps}"/> and
/// <see cref="FingerTreeProductExtensions.SplitBySecond{TElement, TFirst, TSecond, TFirstOps, TSecondOps}"/>;
/// the <see cref="ProductMeasures"/> factory builds the headline pairings without naming the five type
/// arguments.
/// </para>
/// <para>
/// For three or more components, nest the pair — for example
/// <c>ProductMeasure&lt;T, int, MeasurePair&lt;T, Optional&lt;T&gt;&gt;, SizeMeasure&lt;T&gt;,
/// ProductMeasure&lt;T, T, Optional&lt;T&gt;, SumMeasure&lt;T&gt;, MaxMeasure&lt;T&gt;&gt;&gt;</c> is a size + sum +
/// max tree, queried through <c>m.First</c>, <c>m.Second.First</c>, and <c>m.Second.Second</c>.
/// </para>
/// <para>
/// <see cref="OrderStatisticMeasure{T}"/> is the same construction specialized to size + last-key; it keeps its
/// dedicated <see cref="RankedKey{T}"/> carrier for readability rather than reusing this generic pair.
/// </para>
/// </remarks>
/// <example>
/// A positional sequence that is simultaneously a cumulative-weight (Fenwick) structure:
/// <code>
/// var weights = ProductMeasures.CreateSizeAndSum(5, 1, 4, 2);   // Measure = (Count: 4, Sum: 12)
/// var (firstTwo, rest)   = weights.SplitAtIndex(2);             // by the size component  -> [5, 1] | [4, 2]
/// var (lightSide, heavy) = weights.SplitByCumulativeWeight(6);  // by the sum component   -> [5, 1] | [4, 2]
/// </code>
/// </example>
public readonly struct ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>
    : IMeasure<TElement, MeasurePair<TFirst, TSecond>>
    where TFirstOps : IMeasure<TElement, TFirst>
    where TSecondOps : IMeasure<TElement, TSecond>
{
    /// <inheritdoc/>
    public static MeasurePair<TFirst, TSecond> Empty => new(TFirstOps.Empty, TSecondOps.Empty);

    /// <inheritdoc/>
    public static MeasurePair<TFirst, TSecond> Measure(TElement element) =>
        new(TFirstOps.Measure(element), TSecondOps.Measure(element));

    /// <inheritdoc/>
    public static MeasurePair<TFirst, TSecond> Combine(MeasurePair<TFirst, TSecond> left, MeasurePair<TFirst, TSecond> right) =>
        new(TFirstOps.Combine(left.First, right.First), TSecondOps.Combine(left.Second, right.Second));
}

/// <summary>
/// Factory methods that build the headline product-measured trees — size + sum, size + max, and size + min —
/// without spelling out the five type arguments of
/// <see cref="ProductMeasure{TElement, TFirst, TSecond, TFirstOps, TSecondOps}"/> at the call site.
/// </summary>
/// <remarks>
/// Each composed tree answers positional queries through its first (size) component and value queries through
/// its second component, on one structure. The named operations live in
/// <see cref="FingerTreeProductExtensions"/>.
/// </remarks>
public static class ProductMeasures
{
    /// <summary>
    /// Builds a size + sum tree from the given elements: positional (split by index) and Fenwick-style
    /// (split by cumulative weight) at once. O(n).
    /// </summary>
    /// <typeparam name="T">A numeric element/weight type (generic math).</typeparam>
    /// <param name="values">Elements to store, in order.</param>
    /// <returns>A tree measured by size and running sum.</returns>
    public static FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>>
        CreateSizeAndSum<T>(params ReadOnlySpan<T> values)
        where T : IAdditionOperators<T, T, T>, IAdditiveIdentity<T, T> =>
        FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>>.Create(values);

    /// <summary>Builds a size + sum tree from an enumerable source, enumerated once. O(n).</summary>
    /// <typeparam name="T">A numeric element/weight type (generic math).</typeparam>
    /// <param name="values">Elements to store, in order.</param>
    /// <returns>A tree measured by size and running sum.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="values"/> is <see langword="null"/>.</exception>
    public static FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>>
        CreateSizeAndSumRange<T>(IEnumerable<T> values)
        where T : IAdditionOperators<T, T, T>, IAdditiveIdentity<T, T> =>
        FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>>.CreateRange(values);

    /// <summary>Gets the empty size + sum tree. O(1).</summary>
    /// <typeparam name="T">A numeric element/weight type (generic math).</typeparam>
    /// <returns>The empty tree measured by size and running sum.</returns>
    public static FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>>
        EmptySizeAndSum<T>()
        where T : IAdditionOperators<T, T, T>, IAdditiveIdentity<T, T> =>
        FingerTree<T, MeasurePair<int, T>, ProductMeasure<T, int, T, SizeMeasure<T>, SumMeasure<T>>>.Empty;

    /// <summary>
    /// Builds a size + max tree from the given elements: positional access plus O(1) maximum and O(log n)
    /// extract-max, on one structure. O(n).
    /// </summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/> for the maximum.</typeparam>
    /// <param name="values">Elements to store, in order.</param>
    /// <returns>A tree measured by size and maximum.</returns>
    public static FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MaxMeasure<T>>>
        CreateSizeAndMax<T>(params ReadOnlySpan<T> values) =>
        FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MaxMeasure<T>>>.Create(values);

    /// <summary>Builds a size + max tree from an enumerable source, enumerated once. O(n).</summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/> for the maximum.</typeparam>
    /// <param name="values">Elements to store, in order.</param>
    /// <returns>A tree measured by size and maximum.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="values"/> is <see langword="null"/>.</exception>
    public static FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MaxMeasure<T>>>
        CreateSizeAndMaxRange<T>(IEnumerable<T> values) =>
        FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MaxMeasure<T>>>.CreateRange(values);

    /// <summary>Gets the empty size + max tree. O(1).</summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/> for the maximum.</typeparam>
    /// <returns>The empty tree measured by size and maximum.</returns>
    public static FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MaxMeasure<T>>>
        EmptySizeAndMax<T>() =>
        FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MaxMeasure<T>>>.Empty;

    /// <summary>
    /// Builds a size + min tree from the given elements: positional access plus O(1) minimum and O(log n)
    /// extract-min, on one structure. O(n).
    /// </summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/> for the minimum.</typeparam>
    /// <param name="values">Elements to store, in order.</param>
    /// <returns>A tree measured by size and minimum.</returns>
    public static FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MinMeasure<T>>>
        CreateSizeAndMin<T>(params ReadOnlySpan<T> values) =>
        FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MinMeasure<T>>>.Create(values);

    /// <summary>Builds a size + min tree from an enumerable source, enumerated once. O(n).</summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/> for the minimum.</typeparam>
    /// <param name="values">Elements to store, in order.</param>
    /// <returns>A tree measured by size and minimum.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="values"/> is <see langword="null"/>.</exception>
    public static FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MinMeasure<T>>>
        CreateSizeAndMinRange<T>(IEnumerable<T> values) =>
        FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MinMeasure<T>>>.CreateRange(values);

    /// <summary>Gets the empty size + min tree. O(1).</summary>
    /// <typeparam name="T">Element type, ordered by <see cref="Comparer{T}.Default"/> for the minimum.</typeparam>
    /// <returns>The empty tree measured by size and minimum.</returns>
    public static FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MinMeasure<T>>>
        EmptySizeAndMin<T>() =>
        FingerTree<T, MeasurePair<int, Optional<T>>, ProductMeasure<T, int, Optional<T>, SizeMeasure<T>, MinMeasure<T>>>.Empty;
}
