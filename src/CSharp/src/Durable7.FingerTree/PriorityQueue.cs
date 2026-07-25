using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

/// <summary>
/// The measure of a priority queue range: the entry count and the minimum priority present.
/// </summary>
/// <typeparam name="TPriority">Priority type.</typeparam>
/// <param name="Count">Number of entries summarized.</param>
/// <param name="Min">The least priority (by <see cref="Comparer{TPriority}.Default"/>), or
/// <see cref="Optional{TPriority}.None"/> when the range is empty.</param>
public readonly record struct PriorityAggregate<TPriority>(int Count, Optional<TPriority> Min);

/// <summary>
/// Measures a priority-queue entry by its priority: count adds and the minimum priority (by
/// <see cref="Comparer{TPriority}.Default"/>) accumulates, so the whole-tree measure exposes the count and
/// the front-of-queue priority in O(1).
/// </summary>
/// <typeparam name="TElement">Stored element type.</typeparam>
/// <typeparam name="TPriority">Priority type, ordered by <see cref="Comparer{TPriority}.Default"/>.</typeparam>
public readonly struct PriorityMeasure<TElement, TPriority>
    : IMeasure<(TElement Element, TPriority Priority), PriorityAggregate<TPriority>>
{
    /// <inheritdoc/>
    public static PriorityAggregate<TPriority> Empty => new(0, Optional<TPriority>.None);

    /// <inheritdoc/>
    public static PriorityAggregate<TPriority> Measure((TElement Element, TPriority Priority) element) =>
        new(1, Optional<TPriority>.Some(element.Priority));

    /// <inheritdoc/>
    public static PriorityAggregate<TPriority> Combine(PriorityAggregate<TPriority> left, PriorityAggregate<TPriority> right) =>
        new(left.Count + right.Count, MinPriority(left.Min, right.Min));

    private static Optional<TPriority> MinPriority(Optional<TPriority> left, Optional<TPriority> right)
    {
        if (!left.HasValue)
            return right;
        if (!right.HasValue)
            return left;
        return Comparer<TPriority>.Default.Compare(left.Value, right.Value) <= 0 ? left : right;
    }
}

/// <summary>
/// An immutable, persistent, <em>meldable</em> minimum-priority queue backed by a measured finger tree: the
/// entry with the least priority is dequeued first, with O(1) amortized enqueue and O(log(min(n, m))) melding
/// of two queues.
/// </summary>
/// <typeparam name="TElement">Stored element type.</typeparam>
/// <typeparam name="TPriority">Priority type, ordered by <see cref="Comparer{TPriority}.Default"/>.</typeparam>
/// <remarks>
/// <para>
/// Entries are stored in insertion order and the front is located through the <see cref="PriorityMeasure{TElement, TPriority}"/>
/// minimum, so enqueue is a cheap append and two queues meld by concatenation — the distinguishing strength
/// of a finger-tree priority queue over a heap. Among entries of equal least priority the earliest-enqueued
/// is dequeued first (stable / FIFO).
/// </para>
/// <para>
/// The queue orders by <see cref="Comparer{TPriority}.Default"/> and dequeues the minimum (a min-priority
/// queue's <c>Combine</c> depends on the order, so unlike the sorted collections it cannot accept a runtime
/// comparer). For a maximum-priority queue, use a priority type whose natural order is reversed.
/// </para>
/// <para>
/// Complexity: <see cref="Count"/>, <see cref="IsEmpty"/>, <see cref="TryPeekPriority"/> are O(1);
/// <see cref="Enqueue"/> is O(1) amortized; <see cref="TryPeek"/> and <see cref="TryDequeue"/> are O(log n);
/// <see cref="Meld"/> is O(log(min(n, m))). Enumeration is in an unspecified (insertion) order. Instances are
/// immutable and safe for concurrent reads.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// var q = PriorityQueue&lt;string, int&gt;.Empty
///     .Enqueue("low", 5)
///     .Enqueue("urgent", 1)
///     .Enqueue("mid", 3);
///
/// while (q.TryDequeue(out var item, out var priority, out q))
///     Console.WriteLine($"{priority}: {item}");   // 1: urgent, 3: mid, 5: low
/// </code>
/// </example>
public sealed class PriorityQueue<TElement, TPriority> : IReadOnlyCollection<(TElement Element, TPriority Priority)>
{
    private static readonly PriorityQueue<TElement, TPriority> EmptyInstance =
        new(FingerTree<(TElement Element, TPriority Priority), PriorityAggregate<TPriority>, PriorityMeasure<TElement, TPriority>>.Empty);

    private readonly FingerTree<(TElement Element, TPriority Priority), PriorityAggregate<TPriority>, PriorityMeasure<TElement, TPriority>> _tree;

    private PriorityQueue(
        FingerTree<(TElement Element, TPriority Priority), PriorityAggregate<TPriority>, PriorityMeasure<TElement, TPriority>> tree) =>
        _tree = tree;

    /// <summary>Gets the empty priority queue.</summary>
    public static PriorityQueue<TElement, TPriority> Empty => EmptyInstance;

    /// <summary>Creates a priority queue from element-priority pairs. O(n).</summary>
    /// <param name="items">Element-priority pairs to enqueue.</param>
    /// <returns>A queue containing all pairs.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static PriorityQueue<TElement, TPriority> CreateRange(IEnumerable<(TElement Element, TPriority Priority)> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        var tree = FingerTree<(TElement Element, TPriority Priority), PriorityAggregate<TPriority>, PriorityMeasure<TElement, TPriority>>.Empty;
        foreach (var item in items)
            tree = tree.Append(item);
        return Wrap(tree);
    }

    /// <summary>Gets the number of entries. O(1) amortized; the first read of a fresh spine may force memoized deferred work.</summary>
    public int Count => _tree.Measure.Count;

    /// <summary>Gets a value indicating whether the queue is empty. O(1).</summary>
    public bool IsEmpty => _tree.IsEmpty;

    /// <summary>Adds an element with the given priority. O(1) amortized.</summary>
    /// <param name="element">Element to enqueue.</param>
    /// <param name="priority">Its priority (smaller dequeues first).</param>
    /// <returns>A queue including the new entry.</returns>
    /// <exception cref="OverflowException">The queue already holds <see cref="int.MaxValue"/> entries.</exception>
    public PriorityQueue<TElement, TPriority> Enqueue(TElement element, TPriority priority)
    {
        CheckRoomForOneMore();
        return new(_tree.Append((element, priority)));
    }

    /// <summary>Reads the least priority present without removing anything. O(1) amortized; the first read of a fresh spine may force memoized deferred work.</summary>
    /// <param name="priority">The least priority when non-empty; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when the queue is non-empty; otherwise <see langword="false"/>.</returns>
    public bool TryPeekPriority([MaybeNullWhen(false)] out TPriority priority)
    {
        var min = _tree.Measure.Min;
        priority = min.HasValue ? min.Value : default;
        return min.HasValue;
    }

    /// <summary>Reads the front entry (least priority) without removing it. O(log n).</summary>
    /// <param name="element">The front element when non-empty; otherwise <see langword="default"/>.</param>
    /// <param name="priority">Its priority when non-empty; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when the queue is non-empty; otherwise <see langword="false"/>.</returns>
    public bool TryPeek([MaybeNullWhen(false)] out TElement element, [MaybeNullWhen(false)] out TPriority priority)
    {
        if (TryFindFront(out var entry))
        {
            element = entry.Element;
            priority = entry.Priority;
            return true;
        }

        element = default;
        priority = default;
        return false;
    }

    /// <summary>Removes and returns the front entry (least priority). O(log n).</summary>
    /// <param name="element">The removed front element when non-empty; otherwise <see langword="default"/>.</param>
    /// <param name="priority">Its priority when non-empty; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining queue, or <see cref="Empty"/>.</param>
    /// <returns><see langword="true"/> when an entry was removed; otherwise <see langword="false"/>.</returns>
    public bool TryDequeue(
        [MaybeNullWhen(false)] out TElement element,
        [MaybeNullWhen(false)] out TPriority priority,
        out PriorityQueue<TElement, TPriority> rest)
    {
        var min = _tree.Measure.Min;
        if (!min.HasValue)
        {
            element = default;
            priority = default;
            rest = EmptyInstance;
            return false;
        }

        var comparer = Comparer<TPriority>.Default;
        var target = min.Value;
        _tree.TrySplitFind(new PriorityFrontPredicate<TPriority>(comparer, target), out var before, out var entry, out var after);
        element = entry.Element;
        priority = entry.Priority;
        rest = Wrap(before.Concat(after));
        return true;
    }

    /// <summary>Melds this queue with <paramref name="other"/>. O(log(min(n, m))).</summary>
    /// <param name="other">The queue to combine with this one.</param>
    /// <returns>A queue containing all entries of both.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The combined count would exceed <see cref="int.MaxValue"/>.</exception>
    public PriorityQueue<TElement, TPriority> Meld(PriorityQueue<TElement, TPriority> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (other._tree.IsEmpty)
            return this;
        if (_tree.IsEmpty)
            return other;
        if (other.Count > int.MaxValue - Count)
            throw CountOverflowError();
        return new(_tree.Concat(other._tree));
    }

    /// <summary>Copies the entries to a new array in an unspecified order. O(n).</summary>
    /// <returns>An array of the entries.</returns>
    public (TElement Element, TPriority Priority)[] ToArray() => _tree.ToArray();

    /// <summary>Returns an enumerator over the entries in an unspecified (insertion) order.</summary>
    /// <returns>An enumerator over the entries.</returns>
    public IEnumerator<(TElement Element, TPriority Priority)> GetEnumerator() => _tree.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private bool TryFindFront(out (TElement Element, TPriority Priority) entry)
    {
        var min = _tree.Measure.Min;
        if (!min.HasValue)
        {
            entry = default;
            return false;
        }

        var comparer = Comparer<TPriority>.Default;
        var target = min.Value;
        _tree.TryLocate(new PriorityFrontPredicate<TPriority>(comparer, target), out _, out entry);
        return true;
    }

    private static PriorityQueue<TElement, TPriority> Wrap(
        FingerTree<(TElement Element, TPriority Priority), PriorityAggregate<TPriority>, PriorityMeasure<TElement, TPriority>> tree) =>
        tree.IsEmpty ? EmptyInstance : new(tree);

    private void CheckRoomForOneMore()
    {
        if (Count == int.MaxValue)
            throw CountOverflowError();
    }

    private static OverflowException CountOverflowError() =>
        new("The operation would create a queue with more than Int32.MaxValue entries.");
}
