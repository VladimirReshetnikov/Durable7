namespace Tools.DataStructures.FingerTree;

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
