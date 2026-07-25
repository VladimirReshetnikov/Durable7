using System.Numerics;

namespace Durable7.FingerTree;

// The public IMeasurePredicate<TMeasure> contract lives in MeasurePredicate.cs; this file holds the library's
// own non-capturing struct predicates (and the delegate adapter) that route the collections' reads through the
// zero-allocation FingerTree.TryLocate<TPredicate> overload.

/// <summary>Adapts a <see cref="Func{TMeasure, Boolean}"/> to <see cref="IMeasurePredicate{TMeasure}"/>, so the
/// closure-based public locate API shares the single generic descent. The wrapped delegate's own closure is
/// the only allocation; the wrapper itself lives on the stack.</summary>
/// <typeparam name="TMeasure">The measure type.</typeparam>
/// <param name="predicate">The delegate to evaluate.</param>
internal readonly struct FuncMeasurePredicate<TMeasure>(Func<TMeasure, bool> predicate) : IMeasurePredicate<TMeasure>
{
    /// <inheritdoc/>
    public bool Invoke(TMeasure measure) => predicate(measure);
}

/// <summary>Locates the element at a zero-based <paramref name="rank"/>: the first prefix whose element count
/// exceeds it. The order-statistic split used by every sorted collection's indexer.</summary>
/// <typeparam name="T">The element/key type carried by the <see cref="RankedKey{T}"/> measure.</typeparam>
/// <param name="rank">The zero-based rank to locate.</param>
internal readonly struct CountAbovePredicate<T>(int rank) : IMeasurePredicate<RankedKey<T>>
{
    /// <inheritdoc/>
    public bool Invoke(RankedKey<T> measure) => measure.Count > rank;
}

/// <summary>Locates the lower bound for <paramref name="key"/>: the first element comparing greater than or
/// equal to it under <paramref name="comparer"/>. Specified only over a sequence sorted by that comparer.</summary>
/// <typeparam name="T">The element/key type.</typeparam>
/// <param name="comparer">The order the sequence is sorted by.</param>
/// <param name="key">The search key.</param>
internal readonly struct KeyAtLeastPredicate<T>(IComparer<T> comparer, T key) : IMeasurePredicate<RankedKey<T>>
{
    /// <inheritdoc/>
    public bool Invoke(RankedKey<T> measure) => measure.Key.HasValue && comparer.Compare(measure.Key.Value, key) >= 0;
}

/// <summary>Locates the upper bound for <paramref name="key"/>: the first element comparing strictly greater
/// than it under <paramref name="comparer"/>. Specified only over a sequence sorted by that comparer.</summary>
/// <typeparam name="T">The element/key type.</typeparam>
/// <param name="comparer">The order the sequence is sorted by.</param>
/// <param name="key">The search key.</param>
internal readonly struct KeyAbovePredicate<T>(IComparer<T> comparer, T key) : IMeasurePredicate<RankedKey<T>>
{
    /// <inheritdoc/>
    public bool Invoke(RankedKey<T> measure) => measure.Key.HasValue && comparer.Compare(measure.Key.Value, key) > 0;
}

/// <summary>Locates the priority-queue front: the first prefix whose accumulated minimum priority reaches
/// <paramref name="target"/> under <paramref name="comparer"/>.</summary>
/// <typeparam name="TPriority">The priority type carried by the <see cref="PriorityAggregate{TPriority}"/> measure.</typeparam>
/// <param name="comparer">The priority order.</param>
/// <param name="target">The minimum priority to locate (the tree's overall minimum).</param>
internal readonly struct PriorityFrontPredicate<TPriority>(IComparer<TPriority> comparer, TPriority target)
    : IMeasurePredicate<PriorityAggregate<TPriority>>
{
    /// <inheritdoc/>
    public bool Invoke(PriorityAggregate<TPriority> measure) =>
        measure.Min.HasValue && comparer.Compare(measure.Min.Value, target) <= 0;
}

/// <summary>Locates the first interval whose accumulated maximum high endpoint reaches <paramref name="low"/>
/// under <paramref name="comparer"/> — the stabbing-query candidate of an interval tree.</summary>
/// <typeparam name="T">The endpoint type carried by the <see cref="IntervalAnnotation{T}"/> measure.</typeparam>
/// <param name="comparer">The endpoint order.</param>
/// <param name="low">The query's low endpoint.</param>
internal readonly struct MaxHighAtLeastPredicate<T>(IComparer<T> comparer, T low)
    : IMeasurePredicate<IntervalAnnotation<T>>
{
    /// <inheritdoc/>
    public bool Invoke(IntervalAnnotation<T> measure) =>
        measure.MaxHigh.HasValue && comparer.Compare(measure.MaxHigh.Value, low) >= 0;
}

/// <summary>Splits an interval tree at the first interval whose last (rightmost) low endpoint reaches
/// <paramref name="low"/> under <paramref name="comparer"/> — the insertion/removal boundary in low-endpoint
/// order.</summary>
/// <typeparam name="T">The endpoint type.</typeparam>
/// <param name="comparer">The endpoint order.</param>
/// <param name="low">The reference low endpoint.</param>
internal readonly struct LastLowAtLeastPredicate<T>(IComparer<T> comparer, T low)
    : IMeasurePredicate<IntervalAnnotation<T>>
{
    /// <inheritdoc/>
    public bool Invoke(IntervalAnnotation<T> measure) =>
        measure.LastLow.HasValue && comparer.Compare(measure.LastLow.Value, low) >= 0;
}

/// <summary>Splits an interval tree at the first interval whose last (rightmost) low endpoint exceeds
/// <paramref name="low"/> under <paramref name="comparer"/> — the upper end of a low-endpoint range.</summary>
/// <typeparam name="T">The endpoint type.</typeparam>
/// <param name="comparer">The endpoint order.</param>
/// <param name="low">The reference low endpoint.</param>
internal readonly struct LastLowAbovePredicate<T>(IComparer<T> comparer, T low)
    : IMeasurePredicate<IntervalAnnotation<T>>
{
    /// <inheritdoc/>
    public bool Invoke(IntervalAnnotation<T> measure) =>
        measure.LastLow.HasValue && comparer.Compare(measure.LastLow.Value, low) > 0;
}

// ---- Optional<T> predicates for the max/min and key-measured named operations -----------------------------
// These back the closure-free FingerTreeMeasureExtensions named ops (peek/extract max/min, key lower/upper
// bound). Each comes in a runtime-comparer flavor and a static IComparison<T> flavor.

/// <summary>Locates the first element whose value reaches <paramref name="target"/> under
/// <paramref name="comparer"/> (the max-priority front, or a key lower bound). Specified only over a sequence
/// ordered by that comparer.</summary>
/// <typeparam name="T">The value type carried by the <see cref="Optional{T}"/> measure.</typeparam>
/// <param name="comparer">The order the sequence is arranged by.</param>
/// <param name="target">The value to reach.</param>
internal readonly struct OptionalAtLeastPredicate<T>(IComparer<T> comparer, T target) : IMeasurePredicate<Optional<T>>
{
    /// <inheritdoc/>
    public bool Invoke(Optional<T> measure) => measure.HasValue && comparer.Compare(measure.Value, target) >= 0;
}

/// <summary>Locates the first element whose value is at most <paramref name="target"/> under
/// <paramref name="comparer"/> (the min-priority front). Specified only over a sequence ordered by that
/// comparer.</summary>
/// <typeparam name="T">The value type carried by the <see cref="Optional{T}"/> measure.</typeparam>
/// <param name="comparer">The order the sequence is arranged by.</param>
/// <param name="target">The value to reach from above.</param>
internal readonly struct OptionalAtMostPredicate<T>(IComparer<T> comparer, T target) : IMeasurePredicate<Optional<T>>
{
    /// <inheritdoc/>
    public bool Invoke(Optional<T> measure) => measure.HasValue && comparer.Compare(measure.Value, target) <= 0;
}

/// <summary>Locates the first element whose value strictly exceeds <paramref name="key"/> under
/// <paramref name="comparer"/> (a key upper bound). Specified only over a sequence sorted by that comparer.</summary>
/// <typeparam name="T">The value type carried by the <see cref="Optional{T}"/> measure.</typeparam>
/// <param name="comparer">The order the sequence is sorted by.</param>
/// <param name="key">The search key.</param>
internal readonly struct OptionalAbovePredicate<T>(IComparer<T> comparer, T key) : IMeasurePredicate<Optional<T>>
{
    /// <inheritdoc/>
    public bool Invoke(Optional<T> measure) => measure.HasValue && comparer.Compare(measure.Value, key) > 0;
}

/// <summary>Static-comparison counterpart of <see cref="OptionalAtLeastPredicate{T}"/>.</summary>
/// <typeparam name="T">The value type.</typeparam>
/// <typeparam name="TComparison">The static order strategy.</typeparam>
/// <param name="target">The value to reach.</param>
internal readonly struct OptionalAtLeastPredicate<T, TComparison>(T target) : IMeasurePredicate<Optional<T>>
    where TComparison : IComparison<T>
{
    /// <inheritdoc/>
    public bool Invoke(Optional<T> measure) => measure.HasValue && TComparison.Compare(measure.Value, target) >= 0;
}

/// <summary>Static-comparison counterpart of <see cref="OptionalAtMostPredicate{T}"/>.</summary>
/// <typeparam name="T">The value type.</typeparam>
/// <typeparam name="TComparison">The static order strategy.</typeparam>
/// <param name="target">The value to reach from above.</param>
internal readonly struct OptionalAtMostPredicate<T, TComparison>(T target) : IMeasurePredicate<Optional<T>>
    where TComparison : IComparison<T>
{
    /// <inheritdoc/>
    public bool Invoke(Optional<T> measure) => measure.HasValue && TComparison.Compare(measure.Value, target) <= 0;
}

/// <summary>Static-comparison counterpart of <see cref="OptionalAbovePredicate{T}"/>.</summary>
/// <typeparam name="T">The value type.</typeparam>
/// <typeparam name="TComparison">The static order strategy.</typeparam>
/// <param name="key">The search key.</param>
internal readonly struct OptionalAbovePredicate<T, TComparison>(T key) : IMeasurePredicate<Optional<T>>
    where TComparison : IComparison<T>
{
    /// <inheritdoc/>
    public bool Invoke(Optional<T> measure) => measure.HasValue && TComparison.Compare(measure.Value, key) > 0;
}

/// <summary>Static-comparison lower-bound predicate over the key component of a <see cref="RankedKey{T}"/>
/// (order-statistic) measure.</summary>
/// <typeparam name="T">The key type.</typeparam>
/// <typeparam name="TComparison">The static order strategy.</typeparam>
/// <param name="key">The search key.</param>
internal readonly struct RankedKeyAtLeastPredicate<T, TComparison>(T key) : IMeasurePredicate<RankedKey<T>>
    where TComparison : IComparison<T>
{
    /// <inheritdoc/>
    public bool Invoke(RankedKey<T> measure) => measure.Key.HasValue && TComparison.Compare(measure.Key.Value, key) >= 0;
}

/// <summary>Static-comparison upper-bound predicate over the key component of a <see cref="RankedKey{T}"/>
/// (order-statistic) measure.</summary>
/// <typeparam name="T">The key type.</typeparam>
/// <typeparam name="TComparison">The static order strategy.</typeparam>
/// <param name="key">The search key.</param>
internal readonly struct RankedKeyAbovePredicate<T, TComparison>(T key) : IMeasurePredicate<RankedKey<T>>
    where TComparison : IComparison<T>
{
    /// <inheritdoc/>
    public bool Invoke(RankedKey<T> measure) => measure.Key.HasValue && TComparison.Compare(measure.Key.Value, key) > 0;
}

/// <summary>Locates the first prefix whose running count exceeds <paramref name="index"/> over a bare
/// <see cref="int"/> size measure (the positional split of a size-component product).</summary>
/// <param name="index">The zero-based positional boundary.</param>
internal readonly struct IntAbovePredicate(int index) : IMeasurePredicate<int>
{
    /// <inheritdoc/>
    public bool Invoke(int count) => count > index;
}

/// <summary>Locates the first prefix whose running total exceeds <paramref name="threshold"/> over a
/// generic-numeric sum measure (cumulative-weight selection).</summary>
/// <typeparam name="T">A numeric measure type.</typeparam>
/// <param name="threshold">The cumulative-weight threshold.</param>
internal readonly struct SumAbovePredicate<T>(T threshold) : IMeasurePredicate<T>
    where T : IComparisonOperators<T, T, bool>
{
    /// <inheritdoc/>
    public bool Invoke(T sum) => sum > threshold;
}

// ---- Product-measure component projectors ----------------------------------------------------------------
// These lift a value-type predicate over one component of a MeasurePair to a value-type predicate over the
// whole pair, so the product-measure named ops (and caller-supplied struct predicates) stay closure-free.

/// <summary>Projects a value-type predicate onto the <em>first</em> component of a
/// <see cref="MeasurePair{TFirst, TSecond}"/>.</summary>
/// <typeparam name="TInner">The inner value-type predicate over the first component.</typeparam>
/// <typeparam name="TFirst">First component measure type.</typeparam>
/// <typeparam name="TSecond">Second component measure type.</typeparam>
/// <param name="inner">The predicate evaluated against the first component.</param>
internal readonly struct FirstComponentPredicate<TInner, TFirst, TSecond>(TInner inner)
    : IMeasurePredicate<MeasurePair<TFirst, TSecond>>
    where TInner : struct, IMeasurePredicate<TFirst>
{
    /// <inheritdoc/>
    public bool Invoke(MeasurePair<TFirst, TSecond> measure) => inner.Invoke(measure.First);
}

/// <summary>Projects a value-type predicate onto the <em>second</em> component of a
/// <see cref="MeasurePair{TFirst, TSecond}"/>.</summary>
/// <typeparam name="TInner">The inner value-type predicate over the second component.</typeparam>
/// <typeparam name="TFirst">First component measure type.</typeparam>
/// <typeparam name="TSecond">Second component measure type.</typeparam>
/// <param name="inner">The predicate evaluated against the second component.</param>
internal readonly struct SecondComponentPredicate<TInner, TFirst, TSecond>(TInner inner)
    : IMeasurePredicate<MeasurePair<TFirst, TSecond>>
    where TInner : struct, IMeasurePredicate<TSecond>
{
    /// <inheritdoc/>
    public bool Invoke(MeasurePair<TFirst, TSecond> measure) => inner.Invoke(measure.Second);
}
