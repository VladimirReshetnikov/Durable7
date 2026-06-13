namespace Tools.DataStructures.FingerTree;

/// <summary>
/// An optional value used as a measure carrier: either absent (the monoid identity) or holding a value.
/// </summary>
/// <typeparam name="T">The carried value type.</typeparam>
/// <remarks>
/// The extremal and last-key measures (<see cref="MaxMeasure{T}"/>, <see cref="MinMeasure{T}"/>,
/// <see cref="KeyMeasure{T}"/>) need a monoid identity, but an arbitrary <typeparamref name="T"/> has no
/// natural "bottom" or "empty" element. Wrapping the measure in an optional supplies that identity:
/// <see cref="None"/> is the identity, and combining it with any value yields that value. <see cref="Value"/>
/// is meaningful only when <see cref="HasValue"/> is <see langword="true"/> (it is <see langword="default"/>
/// otherwise).
/// </remarks>
public readonly struct Optional<T>
{
    private Optional(T value)
    {
        HasValue = true;
        Value = value;
    }

    /// <summary>Gets the absent value — the monoid identity. Equal to <see langword="default"/>.</summary>
    public static Optional<T> None => default;

    /// <summary>Gets a value indicating whether a value is present.</summary>
    public bool HasValue { get; }

    /// <summary>Gets the carried value; meaningful only when <see cref="HasValue"/> is <see langword="true"/>.</summary>
    public T Value { get; }

    /// <summary>Creates a present optional holding <paramref name="value"/>.</summary>
    /// <param name="value">The value to carry.</param>
    public static Optional<T> Some(T value) => new(value);

    /// <summary>Returns a readable representation for diagnostics.</summary>
    /// <returns>The carried value's string, or <c>"None"</c> when absent.</returns>
    public override string ToString() => HasValue ? $"Some({Value})" : "None";
}

/// <summary>
/// Measures each element as the maximum (by <see cref="Comparer{T}.Default"/>) so a tree's measure is its
/// maximum element, turning it into a mergeable max-priority queue.
/// </summary>
/// <typeparam name="T">Stored element type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
/// <remarks>
/// For a custom order, define a bespoke <see cref="IMeasure{TElement, TMeasure}"/> instead. The priority-queue
/// operations are exposed as extension methods (<see cref="FingerTreeMeasureExtensions.TryPeekMax{T}"/>,
/// <see cref="FingerTreeMeasureExtensions.TryExtractMax{T}"/>).
/// </remarks>
public readonly struct MaxMeasure<T> : IMeasure<T, Optional<T>>
{
    /// <inheritdoc/>
    public static Optional<T> Empty => Optional<T>.None;

    /// <inheritdoc/>
    public static Optional<T> Measure(T element) => Optional<T>.Some(element);

    /// <inheritdoc/>
    public static Optional<T> Combine(Optional<T> left, Optional<T> right)
    {
        if (!left.HasValue)
            return right;
        if (!right.HasValue)
            return left;
        return Comparer<T>.Default.Compare(left.Value, right.Value) >= 0 ? left : right;
    }
}

/// <summary>
/// Measures each element as the minimum (by <see cref="Comparer{T}.Default"/>) so a tree's measure is its
/// minimum element, turning it into a mergeable min-priority queue.
/// </summary>
/// <typeparam name="T">Stored element type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
/// <remarks>
/// The priority-queue operations are exposed as extension methods
/// (<see cref="FingerTreeMeasureExtensions.TryPeekMin{T}"/>,
/// <see cref="FingerTreeMeasureExtensions.TryExtractMin{T}"/>).
/// </remarks>
public readonly struct MinMeasure<T> : IMeasure<T, Optional<T>>
{
    /// <inheritdoc/>
    public static Optional<T> Empty => Optional<T>.None;

    /// <inheritdoc/>
    public static Optional<T> Measure(T element) => Optional<T>.Some(element);

    /// <inheritdoc/>
    public static Optional<T> Combine(Optional<T> left, Optional<T> right)
    {
        if (!left.HasValue)
            return right;
        if (!right.HasValue)
            return left;
        return Comparer<T>.Default.Compare(left.Value, right.Value) <= 0 ? left : right;
    }
}

/// <summary>
/// Measures each element as itself with a right-biased ("last wins") combine, so the measure of a prefix is
/// its rightmost element. On a sequence kept sorted by <see cref="Comparer{T}.Default"/>, this drives
/// logarithmic lower-bound and upper-bound splits.
/// </summary>
/// <typeparam name="T">Stored element type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
/// <remarks>
/// <para>
/// Because the accumulated measure of a prefix is its last (largest, when sorted) element, the predicate
/// "last element &gt;= key" is monotone over a sorted sequence and locates the lower bound; "last element
/// &gt; key" locates the upper bound. See
/// <see cref="FingerTreeMeasureExtensions.SplitByLowerBound{T}(FingerTree{T, Optional{T}, KeyMeasure{T}}, T)"/>
/// and <see cref="FingerTreeMeasureExtensions.SplitByUpperBound{T}(FingerTree{T, Optional{T}, KeyMeasure{T}}, T)"/>.
/// </para>
/// <para>Results are specified only when the sequence is sorted by <see cref="Comparer{T}.Default"/>.</para>
/// </remarks>
public readonly struct KeyMeasure<T> : IMeasure<T, Optional<T>>
{
    /// <inheritdoc/>
    public static Optional<T> Empty => Optional<T>.None;

    /// <inheritdoc/>
    public static Optional<T> Measure(T element) => Optional<T>.Some(element);

    /// <inheritdoc/>
    public static Optional<T> Combine(Optional<T> left, Optional<T> right) => right.HasValue ? right : left;
}

/// <summary>
/// A measure carrier pairing an element count with a last-key, the product measure of
/// <see cref="OrderStatisticMeasure{T}"/>.
/// </summary>
/// <typeparam name="T">Stored element type.</typeparam>
/// <param name="Count">Number of elements summarized.</param>
/// <param name="Key">The rightmost element, or <see cref="Optional{T}.None"/> when the range is empty.</param>
public readonly record struct RankedKey<T>(int Count, Optional<T> Key);

/// <summary>
/// Measures each element as a (count, last-key) pair, so a tree is simultaneously a positional sequence
/// (split by count) and — when kept sorted — an ordered search tree (split by key): an order-statistic tree.
/// </summary>
/// <typeparam name="T">Stored element type, ordered by <see cref="Comparer{T}.Default"/> for the key
/// component.</typeparam>
/// <remarks>
/// Split by the count component for positional / order-statistic access
/// (<see cref="FingerTreeMeasureExtensions.SplitAtIndex{T}"/>) and by the key component for ordered search
/// (<see cref="FingerTreeMeasureExtensions.SplitByLowerBound{T}(FingerTree{T, RankedKey{T}, OrderStatisticMeasure{T}}, T)"/>),
/// on the same tree.
/// </remarks>
public readonly struct OrderStatisticMeasure<T> : IMeasure<T, RankedKey<T>>
{
    /// <inheritdoc/>
    public static RankedKey<T> Empty => new(0, Optional<T>.None);

    /// <inheritdoc/>
    public static RankedKey<T> Measure(T element) => new(1, Optional<T>.Some(element));

    /// <inheritdoc/>
    public static RankedKey<T> Combine(RankedKey<T> left, RankedKey<T> right) =>
        new(left.Count + right.Count, right.Key.HasValue ? right.Key : left.Key);
}

/// <summary>
/// Like <see cref="MaxMeasure{T}"/> but ordered by a custom <typeparamref name="TComparison"/> instead of
/// <see cref="Comparer{T}.Default"/>, so a longest-first / by-projection / reversed priority queue needs only
/// a one-method <see cref="IComparison{T}"/> rather than a hand-rolled measure.
/// </summary>
/// <typeparam name="T">Stored element type.</typeparam>
/// <typeparam name="TComparison">The order used to pick the maximum.</typeparam>
public readonly struct MaxMeasure<T, TComparison> : IMeasure<T, Optional<T>>
    where TComparison : IComparison<T>
{
    /// <inheritdoc/>
    public static Optional<T> Empty => Optional<T>.None;

    /// <inheritdoc/>
    public static Optional<T> Measure(T element) => Optional<T>.Some(element);

    /// <inheritdoc/>
    public static Optional<T> Combine(Optional<T> left, Optional<T> right)
    {
        if (!left.HasValue)
            return right;
        if (!right.HasValue)
            return left;
        return TComparison.Compare(left.Value, right.Value) >= 0 ? left : right;
    }
}

/// <summary>
/// Like <see cref="MinMeasure{T}"/> but ordered by a custom <typeparamref name="TComparison"/> instead of
/// <see cref="Comparer{T}.Default"/>.
/// </summary>
/// <typeparam name="T">Stored element type.</typeparam>
/// <typeparam name="TComparison">The order used to pick the minimum.</typeparam>
public readonly struct MinMeasure<T, TComparison> : IMeasure<T, Optional<T>>
    where TComparison : IComparison<T>
{
    /// <inheritdoc/>
    public static Optional<T> Empty => Optional<T>.None;

    /// <inheritdoc/>
    public static Optional<T> Measure(T element) => Optional<T>.Some(element);

    /// <inheritdoc/>
    public static Optional<T> Combine(Optional<T> left, Optional<T> right)
    {
        if (!left.HasValue)
            return right;
        if (!right.HasValue)
            return left;
        return TComparison.Compare(left.Value, right.Value) <= 0 ? left : right;
    }
}
