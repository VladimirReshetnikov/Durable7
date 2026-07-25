using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

internal readonly record struct IntervalMapEntry<TEndpoint, TValue>(
    Interval<TEndpoint> Interval,
    TValue Value);

internal readonly record struct IntervalMapAnnotation<TEndpoint>(
    int Count,
    Optional<Interval<TEndpoint>> LastInterval,
    Optional<TEndpoint> MaxHigh);

internal readonly struct IntervalMapMeasure<TEndpoint, TValue> :
    IMeasure<IntervalMapEntry<TEndpoint, TValue>, IntervalMapAnnotation<TEndpoint>>
{
    public static IntervalMapAnnotation<TEndpoint> Empty =>
        new(0, Optional<Interval<TEndpoint>>.None, Optional<TEndpoint>.None);

    public static IntervalMapAnnotation<TEndpoint> Measure(IntervalMapEntry<TEndpoint, TValue> element) =>
        new(1, Optional<Interval<TEndpoint>>.Some(element.Interval), Optional<TEndpoint>.Some(element.Interval.High));

    public static IntervalMapAnnotation<TEndpoint> Combine(
        IntervalMapAnnotation<TEndpoint> left,
        IntervalMapAnnotation<TEndpoint> right) =>
        new(
            checked(left.Count + right.Count),
            right.LastInterval.HasValue ? right.LastInterval : left.LastInterval,
            CombineMax(left.MaxHigh, right.MaxHigh));

    private static Optional<TEndpoint> CombineMax(Optional<TEndpoint> left, Optional<TEndpoint> right)
    {
        if (!left.HasValue)
            return right;
        if (!right.HasValue)
            return left;
        return Comparer<TEndpoint>.Default.Compare(left.Value, right.Value) >= 0 ? left : right;
    }
}

/// <summary>
/// Represents an immutable map from comparer-distinct closed intervals to payload values, with
/// logarithmic exact-key and overlap queries.
/// </summary>
/// <typeparam name="TEndpoint">The endpoint type, ordered by <see cref="Comparer{T}.Default"/>.</typeparam>
/// <typeparam name="TValue">The payload type.</typeparam>
/// <remarks>
/// Intervals are validated and ordered lexicographically by <c>(Low, High)</c>. Exactly one payload
/// is stored for each endpoint-comparer-equivalent interval, while distinct intervals may overlap.
/// The measured finger-tree annotation also retains the maximum high endpoint, enabling interval
/// pruning without a separate index. Instances are immutable and safe for concurrent reads when
/// the endpoint and value comparers are safe for concurrent use.
/// </remarks>
public sealed partial class PersistentIntervalMap<TEndpoint, TValue> :
    IEnumerable<KeyValuePair<Interval<TEndpoint>, TValue>>
{
    private static readonly PersistentIntervalMap<TEndpoint, TValue> EmptyInstance = new(
        FingerTree<IntervalMapEntry<TEndpoint, TValue>, IntervalMapAnnotation<TEndpoint>,
            IntervalMapMeasure<TEndpoint, TValue>>.Empty,
        EqualityComparer<TValue>.Default);

    private readonly FingerTree<IntervalMapEntry<TEndpoint, TValue>, IntervalMapAnnotation<TEndpoint>,
        IntervalMapMeasure<TEndpoint, TValue>> _tree;

    private PersistentIntervalMap(
        FingerTree<IntervalMapEntry<TEndpoint, TValue>, IntervalMapAnnotation<TEndpoint>,
            IntervalMapMeasure<TEndpoint, TValue>> tree,
        IEqualityComparer<TValue> valueComparer)
    {
        _tree = tree;
        ValueComparer = valueComparer;
    }

    /// <summary>Gets the shared empty map using the default value comparer.</summary>
    public static PersistentIntervalMap<TEndpoint, TValue> Empty => EmptyInstance;

    /// <summary>Gets the number of interval/payload entries.</summary>
    public int Count => _tree.Measure.Count;

    /// <summary>Gets whether the map contains no entries.</summary>
    public bool IsEmpty => _tree.IsEmpty;

    /// <summary>Gets the equality comparer used to recognize value-preserving updates.</summary>
    public IEqualityComparer<TValue> ValueComparer { get; }

    /// <summary>Gets stored intervals in ascending lexicographic endpoint order.</summary>
    public IEnumerable<Interval<TEndpoint>> Keys => EnumerateKeys();

    /// <summary>Gets payloads in ascending lexicographic interval order.</summary>
    public IEnumerable<TValue> Values => EnumerateValues();

    /// <summary>Creates an empty map using the supplied value comparer.</summary>
    public static PersistentIntervalMap<TEndpoint, TValue> Create(
        IEqualityComparer<TValue>? valueComparer = null)
    {
        valueComparer ??= EqualityComparer<TValue>.Default;
        return ReferenceEquals(valueComparer, EqualityComparer<TValue>.Default)
            ? Empty
            : new(
                FingerTree<IntervalMapEntry<TEndpoint, TValue>, IntervalMapAnnotation<TEndpoint>,
                    IntervalMapMeasure<TEndpoint, TValue>>.Empty,
                valueComparer);
    }

    /// <summary>Creates a map from entries in enumeration order, with later distinct values winning.</summary>
    public static PersistentIntervalMap<TEndpoint, TValue> CreateRange(
        IEnumerable<KeyValuePair<Interval<TEndpoint>, TValue>> entries,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var result = Create(valueComparer);
        foreach (var (interval, value) in entries)
            result = result.SetItem(interval, value);
        return result;
    }

    /// <summary>Gets the payload associated with an interval key.</summary>
    public TValue this[Interval<TEndpoint> interval] =>
        TryGetValue(interval, out var value)
            ? value
            : throw new KeyNotFoundException("The interval was not present in the map.");

    /// <summary>Determines whether an endpoint-comparer-equivalent interval is present.</summary>
    public bool ContainsKey(Interval<TEndpoint> interval) => TryFindExact(interval, out _);

    /// <summary>Tries to retrieve the payload associated with an equivalent interval.</summary>
    public bool TryGetValue(
        Interval<TEndpoint> interval,
        [MaybeNullWhen(false)] out TValue value)
    {
        if (TryFindExact(interval, out var entry))
        {
            value = entry.Value;
            return true;
        }

        value = default;
        return false;
    }

    /// <summary>Tries to retrieve the originally stored interval representative.</summary>
    public bool TryGetKey(Interval<TEndpoint> equalInterval, out Interval<TEndpoint> actualInterval)
    {
        if (TryFindExact(equalInterval, out var entry))
        {
            actualInterval = entry.Interval;
            return true;
        }

        actualInterval = default;
        return false;
    }

    /// <summary>Adds an interval and payload, throwing when an equivalent interval already exists.</summary>
    public PersistentIntervalMap<TEndpoint, TValue> Add(Interval<TEndpoint> interval, TValue value)
    {
        ValidateInterval(interval, nameof(interval));
        var (left, right) = SplitAt(interval);
        if (right.TryViewLeft(out var found, out _)
            && CompareIntervals(found.Interval, interval) == 0)
        {
            throw new ArgumentException("An equivalent interval is already present.", nameof(interval));
        }

        CheckRoomForOneMore();
        return new(left.Append(new(interval, value)).Concat(right), ValueComparer);
    }

    /// <summary>Attempts to add an interval and payload when the interval is absent.</summary>
    public bool TryAdd(
        Interval<TEndpoint> interval,
        TValue value,
        out PersistentIntervalMap<TEndpoint, TValue> result)
    {
        ValidateInterval(interval, nameof(interval));
        var (left, right) = SplitAt(interval);
        if (right.TryViewLeft(out var found, out _)
            && CompareIntervals(found.Interval, interval) == 0)
        {
            result = this;
            return false;
        }

        CheckRoomForOneMore();
        result = new(left.Append(new(interval, value)).Concat(right), ValueComparer);
        return true;
    }

    /// <summary>Adds or replaces an interval's payload while retaining its stored interval representative.</summary>
    public PersistentIntervalMap<TEndpoint, TValue> SetItem(Interval<TEndpoint> interval, TValue value)
    {
        ValidateInterval(interval, nameof(interval));
        var (left, right) = SplitAt(interval);
        if (right.TryViewLeft(out var found, out var tail)
            && CompareIntervals(found.Interval, interval) == 0)
        {
            if (ValueComparer.Equals(found.Value, value))
                return this;
            return new(left.Append(new(found.Interval, value)).Concat(tail), ValueComparer);
        }

        CheckRoomForOneMore();
        return new(left.Append(new(interval, value)).Concat(right), ValueComparer);
    }

    /// <summary>Removes an interval key, returning the receiver when it is absent.</summary>
    public PersistentIntervalMap<TEndpoint, TValue> Remove(Interval<TEndpoint> interval) =>
        TryRemove(interval, out var result, out _) ? result : this;

    /// <summary>Attempts to remove an interval key and returns its payload.</summary>
    public bool TryRemove(
        Interval<TEndpoint> interval,
        out PersistentIntervalMap<TEndpoint, TValue> result,
        [MaybeNullWhen(false)] out TValue value)
    {
        ValidateInterval(interval, nameof(interval));
        var (left, right) = SplitAt(interval);
        if (!right.TryViewLeft(out var found, out var tail)
            || CompareIntervals(found.Interval, interval) != 0)
        {
            result = this;
            value = default;
            return false;
        }

        result = Wrap(left.Concat(tail), ValueComparer);
        value = found.Value;
        return true;
    }

    /// <summary>Returns a value-comparer-preserving empty map.</summary>
    public PersistentIntervalMap<TEndpoint, TValue> Clear() =>
        IsEmpty ? this : Create(ValueComparer);

    /// <summary>Finds one entry whose closed interval overlaps the query.</summary>
    public bool TryFindOverlap(
        Interval<TEndpoint> query,
        out KeyValuePair<Interval<TEndpoint>, TValue> match)
    {
        ValidateInterval(query, nameof(query));
        var comparer = Comparer<TEndpoint>.Default;
        if (_tree.TryLocate(new IntervalMapMaxHighAtLeast<TEndpoint>(query.Low), out _, out var candidate)
            && comparer.Compare(candidate.Interval.Low, query.High) <= 0)
        {
            match = ToPair(candidate);
            return true;
        }

        match = default;
        return false;
    }

    /// <summary>Finds one entry whose closed interval contains the supplied point.</summary>
    public bool TryFindContaining(
        TEndpoint point,
        out KeyValuePair<Interval<TEndpoint>, TValue> match) =>
        TryFindOverlap(new(point, point), out match);

    /// <summary>Returns all entries overlapping the query in ascending interval order.</summary>
    public IReadOnlyList<KeyValuePair<Interval<TEndpoint>, TValue>> FindOverlaps(
        Interval<TEndpoint> query)
    {
        ValidateInterval(query, nameof(query));
        var results = new List<KeyValuePair<Interval<TEndpoint>, TValue>>();
        var (candidates, _) = _tree.Split(new IntervalMapLastLowAbove<TEndpoint>(query.High));

        // Each split discards a maximal prefix whose high endpoints are all too small. The remaining
        // first entry is a hit; continuing with its suffix makes the traversal output-sensitive.
        while (candidates.TrySplitFind(
            new IntervalMapMaxHighAtLeast<TEndpoint>(query.Low),
            out _,
            out var hit,
            out var after))
        {
            results.Add(ToPair(hit));
            candidates = after;
        }

        return results;
    }

    /// <summary>Counts entries overlapping the query without materializing them.</summary>
    public int CountOverlaps(Interval<TEndpoint> query)
    {
        ValidateInterval(query, nameof(query));
        var count = 0;
        var (candidates, _) = _tree.Split(new IntervalMapLastLowAbove<TEndpoint>(query.High));
        while (candidates.TrySplitFind(
            new IntervalMapMaxHighAtLeast<TEndpoint>(query.Low),
            out _,
            out _,
            out var after))
        {
            count++;
            candidates = after;
        }

        return count;
    }

    /// <summary>Copies entries into a fresh array in ascending interval order.</summary>
    public KeyValuePair<Interval<TEndpoint>, TValue>[] ToArray()
    {
        var result = new KeyValuePair<Interval<TEndpoint>, TValue>[Count];
        var index = 0;
        foreach (var entry in this)
            result[index++] = entry;
        return result;
    }

    /// <summary>Returns an enumerator in ascending lexicographic interval order.</summary>
    public IEnumerator<KeyValuePair<Interval<TEndpoint>, TValue>> GetEnumerator() =>
        EnumeratePairs().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal void ValidateInvariants()
    {
        var comparer = Comparer<TEndpoint>.Default;
        var hasPrevious = false;
        var previous = default(Interval<TEndpoint>);
        var count = 0;
        var maximum = default(TEndpoint);
        var hasMaximum = false;

        foreach (var entry in _tree)
        {
            ValidateInterval(entry.Interval, nameof(entry.Interval));
            if (hasPrevious && CompareIntervals(previous, entry.Interval) >= 0)
                throw new InvalidOperationException("Interval-map keys are not strictly ordered.");
            previous = entry.Interval;
            hasPrevious = true;
            count = checked(count + 1);
            if (!hasMaximum || comparer.Compare(entry.Interval.High, maximum!) > 0)
            {
                maximum = entry.Interval.High;
                hasMaximum = true;
            }
        }

        var measure = _tree.Measure;
        if (measure.Count != count)
            throw new InvalidOperationException("The interval-map count annotation is inconsistent.");
        if (hasPrevious && (!measure.LastInterval.HasValue
            || CompareIntervals(measure.LastInterval.Value, previous) != 0))
            throw new InvalidOperationException("The interval-map last-key annotation is inconsistent.");
        if (hasMaximum && (!measure.MaxHigh.HasValue
            || comparer.Compare(measure.MaxHigh.Value, maximum!) != 0))
            throw new InvalidOperationException("The interval-map maximum-high annotation is inconsistent.");
    }

    private bool TryFindExact(
        Interval<TEndpoint> interval,
        out IntervalMapEntry<TEndpoint, TValue> entry)
    {
        ValidateInterval(interval, nameof(interval));
        return _tree.TryLocate(new IntervalMapLastKeyAtLeast<TEndpoint>(interval), out _, out entry)
            && CompareIntervals(entry.Interval, interval) == 0;
    }

    private (
        FingerTree<IntervalMapEntry<TEndpoint, TValue>, IntervalMapAnnotation<TEndpoint>,
            IntervalMapMeasure<TEndpoint, TValue>> Left,
        FingerTree<IntervalMapEntry<TEndpoint, TValue>, IntervalMapAnnotation<TEndpoint>,
            IntervalMapMeasure<TEndpoint, TValue>> Right) SplitAt(Interval<TEndpoint> interval) =>
        _tree.Split(new IntervalMapLastKeyAtLeast<TEndpoint>(interval));

    private IEnumerable<KeyValuePair<Interval<TEndpoint>, TValue>> EnumeratePairs()
    {
        foreach (var entry in _tree)
            yield return ToPair(entry);
    }

    private IEnumerable<Interval<TEndpoint>> EnumerateKeys()
    {
        foreach (var entry in _tree)
            yield return entry.Interval;
    }

    private IEnumerable<TValue> EnumerateValues()
    {
        foreach (var entry in _tree)
            yield return entry.Value;
    }

    private void CheckRoomForOneMore()
    {
        if (Count == int.MaxValue)
            throw new OverflowException("The operation would exceed Int32.MaxValue entries.");
    }

    private static void ValidateInterval(Interval<TEndpoint> interval, string parameterName)
    {
        if (Comparer<TEndpoint>.Default.Compare(interval.Low, interval.High) > 0)
            throw new ArgumentException("An interval's low endpoint must not exceed its high endpoint.", parameterName);
    }

    private static int CompareIntervals(Interval<TEndpoint> left, Interval<TEndpoint> right)
    {
        var comparer = Comparer<TEndpoint>.Default;
        var low = comparer.Compare(left.Low, right.Low);
        return low != 0 ? low : comparer.Compare(left.High, right.High);
    }

    private static KeyValuePair<Interval<TEndpoint>, TValue> ToPair(
        IntervalMapEntry<TEndpoint, TValue> entry) =>
        KeyValuePair.Create(entry.Interval, entry.Value);

    private static PersistentIntervalMap<TEndpoint, TValue> Wrap(
        FingerTree<IntervalMapEntry<TEndpoint, TValue>, IntervalMapAnnotation<TEndpoint>,
            IntervalMapMeasure<TEndpoint, TValue>> tree,
        IEqualityComparer<TValue> valueComparer) =>
        tree.IsEmpty ? Create(valueComparer) : new(tree, valueComparer);
}

internal readonly struct IntervalMapLastKeyAtLeast<TEndpoint>(Interval<TEndpoint> target) :
    IMeasurePredicate<IntervalMapAnnotation<TEndpoint>>
{
    public bool Invoke(IntervalMapAnnotation<TEndpoint> measure)
    {
        if (!measure.LastInterval.HasValue)
            return false;
        var comparer = Comparer<TEndpoint>.Default;
        var low = comparer.Compare(measure.LastInterval.Value.Low, target.Low);
        return low > 0 || (low == 0 && comparer.Compare(measure.LastInterval.Value.High, target.High) >= 0);
    }
}

internal readonly struct IntervalMapLastLowAbove<TEndpoint>(TEndpoint high) :
    IMeasurePredicate<IntervalMapAnnotation<TEndpoint>>
{
    public bool Invoke(IntervalMapAnnotation<TEndpoint> measure) =>
        measure.LastInterval.HasValue
        && Comparer<TEndpoint>.Default.Compare(measure.LastInterval.Value.Low, high) > 0;
}

internal readonly struct IntervalMapMaxHighAtLeast<TEndpoint>(TEndpoint low) :
    IMeasurePredicate<IntervalMapAnnotation<TEndpoint>>
{
    public bool Invoke(IntervalMapAnnotation<TEndpoint> measure) =>
        measure.MaxHigh.HasValue
        && Comparer<TEndpoint>.Default.Compare(measure.MaxHigh.Value, low) >= 0;
}
