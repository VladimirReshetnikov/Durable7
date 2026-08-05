using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

/// <summary>Describes one transition of a deterministic additive event machine.</summary>
/// <param name="NextState">The state after consuming the element.</param>
/// <param name="EventCount">The nonnegative number of logical events emitted by the transition.</param>
public readonly record struct ContextualEventTransition(int NextState, long EventCount);

/// <summary>
/// Defines a finite deterministic machine whose transitions emit nonnegative event counts.
/// </summary>
/// <typeparam name="TElement">The input-element type.</typeparam>
/// <remarks>
/// <para>
/// Implementations are phantom policy types: no instance is created. <see cref="StateCount"/> must
/// be positive and stable for the lifetime of the process. For every state in
/// <c>0..StateCount-1</c>, <see cref="Transition"/> must return a state in the same range and a
/// nonnegative event count. Calls must be deterministic and side-effect-free.
/// </para>
/// <para>
/// A transition may emit more than one event. Rank counts every emitted event, while select maps
/// each event back to its containing input element and its zero-based ordinal within that element.
/// </para>
/// </remarks>
public interface IContextualEventMachine<TElement>
{
    /// <summary>Gets the finite number of machine states.</summary>
    static abstract int StateCount { get; }

    /// <summary>Consumes one element from one valid state.</summary>
    /// <param name="state">The incoming state in <c>0..StateCount-1</c>.</param>
    /// <param name="element">The input element.</param>
    /// <returns>The outgoing state and nonnegative emitted-event count.</returns>
    static abstract ContextualEventTransition Transition(int state, TElement element);
}

/// <summary>Summarizes running a contextual event machine over a prefix.</summary>
/// <param name="FinalState">The state after the prefix.</param>
/// <param name="EventCount">The number of events emitted by the prefix.</param>
public readonly record struct ContextualPrefixSummary(int FinalState, long EventCount);

/// <summary>Identifies one contextual event and the input element that emitted it.</summary>
/// <param name="ElementIndex">The zero-based index of the emitting element.</param>
/// <param name="EventIndexInElement">The event's zero-based ordinal among events emitted by that transition.</param>
/// <param name="StateBefore">The machine state before the element.</param>
/// <param name="StateAfter">The machine state after the element.</param>
/// <param name="ElementEventCount">The total number of events emitted by that transition.</param>
public readonly record struct ContextualEventLocation(
    int ElementIndex,
    long EventIndexInElement,
    int StateBefore,
    int StateAfter,
    long ElementEventCount);

/// <summary>
/// Represents an immutable sequence indexed by events whose occurrence depends on finite left
/// context.
/// </summary>
/// <typeparam name="TElement">The stored input-element type.</typeparam>
/// <typeparam name="TMachine">The deterministic additive event-machine policy.</typeparam>
/// <remarks>
/// <para>
/// Every measured subtree caches, for every possible incoming state, the outgoing state and event
/// count produced by that subtree. Ordered composition feeds the left outgoing state into the
/// right summary. This turns a context-dependent scan into an associative measure.
/// </para>
/// <para>
/// Let <c>n</c> be the element count and <c>s</c> the fixed machine-state count. Full-sequence
/// evaluation is O(1); prefix evaluation, indexing, contextual event rank/select, insertion,
/// replacement, removal, splitting, and slicing are O(s log n) amortized; endpoint updates are
/// O(s) amortized; concatenation is O(s log(min(n,m))) amortized; enumeration is O(n). When the
/// machine is fixed, <c>s</c> is a constant, giving O(log n) rank/select and edits. Each retained
/// changed spine adds O(s log n) summary storage while unaffected structure remains shared.
/// </para>
/// <para>
/// Instances and their summaries are immutable and safe for concurrent reads. A throwing or
/// invalid machine transition, or an event-count overflow, publishes no successor and leaves every
/// older version usable.
/// </para>
/// </remarks>
public sealed class ContextualRankSequence<TElement, TMachine> : IReadOnlyList<TElement>
    where TMachine : IContextualEventMachine<TElement>
{
    private static readonly int MachineStateCount = ValidateStateCount(TMachine.StateCount);
    private static readonly Tree EmptyTree = Tree.Empty;
    private static readonly ContextualRankSequence<TElement, TMachine> EmptyInstance = new(EmptyTree);

    private readonly Tree _tree;

    private ContextualRankSequence(Tree tree)
    {
        _ = tree.Measure;
        _tree = tree;
    }

    /// <summary>Gets the canonical empty sequence.</summary>
    public static ContextualRankSequence<TElement, TMachine> Empty => EmptyInstance;

    /// <summary>Gets the machine's finite state count.</summary>
    public static int StateCount => MachineStateCount;

    /// <summary>Creates a sequence from a span in input order.</summary>
    /// <param name="items">The elements to store.</param>
    /// <returns>A sequence containing <paramref name="items"/> in order.</returns>
    /// <remarks>O(s n) time, where s is <see cref="StateCount"/>.</remarks>
    public static ContextualRankSequence<TElement, TMachine> Create(ReadOnlySpan<TElement> items) =>
        items.IsEmpty ? EmptyInstance : new(Tree.Create(items));

    /// <summary>Creates a sequence by eagerly enumerating a source once.</summary>
    /// <param name="items">The elements to store.</param>
    /// <returns>A sequence containing the enumeration results in order.</returns>
    /// <remarks>O(s n) time. Passing this exact sequence type returns it by reference.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static ContextualRankSequence<TElement, TMachine> CreateRange(IEnumerable<TElement> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items is ContextualRankSequence<TElement, TMachine> sequence)
            return sequence;
        var tree = Tree.CreateRange(items);
        return tree.IsEmpty ? EmptyInstance : new(tree);
    }

    /// <summary>Gets the number of stored elements.</summary>
    public int Count => _tree.Measure.Count;

    /// <summary>Gets whether the sequence contains no elements.</summary>
    public bool IsEmpty => _tree.IsEmpty;

    /// <summary>Gets the element at a zero-based index.</summary>
    /// <param name="index">The element index.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the sequence.</exception>
    public TElement this[int index]
    {
        get
        {
            CheckElementIndex(index);
            _tree.TryLocate(new PositionPredicate(index), out _, out var found);
            return found!;
        }
    }

    /// <summary>Runs the machine over the whole sequence from an initial state.</summary>
    /// <param name="initialState">The incoming state.</param>
    /// <returns>The final state and emitted-event count.</returns>
    /// <remarks>O(1).</remarks>
    public ContextualPrefixSummary Evaluate(int initialState)
    {
        CheckState(initialState);
        return _tree.Measure.Evaluate(initialState);
    }

    /// <summary>Runs the machine over the first <paramref name="elementCount"/> elements.</summary>
    /// <param name="elementCount">A prefix length in <c>0..Count</c>.</param>
    /// <param name="initialState">The incoming state.</param>
    /// <returns>The state and event count at the prefix boundary.</returns>
    /// <remarks>O(s log n) amortized, and O(1) at either sequence endpoint.</remarks>
    public ContextualPrefixSummary EvaluatePrefix(int elementCount, int initialState)
    {
        CheckBoundaryIndex(elementCount);
        CheckState(initialState);
        if (elementCount == 0)
            return new(initialState, 0);
        if (elementCount == Count)
            return _tree.Measure.Evaluate(initialState);

        _tree.TryLocate(new PositionPredicate(elementCount), out var before, out _);
        return before.Evaluate(initialState);
    }

    /// <summary>Returns the number of contextual events strictly before an element boundary.</summary>
    /// <param name="elementCount">The number of elements in the prefix.</param>
    /// <param name="initialState">The state before the complete sequence.</param>
    /// <returns>The contextual event rank of the boundary.</returns>
    public long EventRank(int elementCount, int initialState) =>
        EvaluatePrefix(elementCount, initialState).EventCount;

    /// <summary>Attempts to locate a zero-based contextual event.</summary>
    /// <param name="eventIndex">The zero-based event rank.</param>
    /// <param name="initialState">The state before the complete sequence.</param>
    /// <param name="location">The emitting element and transition details when found.</param>
    /// <returns><see langword="true"/> when <paramref name="eventIndex"/> exists.</returns>
    /// <remarks>O(s log n) amortized. A negative or past-end rank returns <see langword="false"/>.</remarks>
    public bool TrySelectEvent(
        long eventIndex,
        int initialState,
        out ContextualEventLocation location)
    {
        CheckState(initialState);
        if (eventIndex < 0 || eventIndex >= _tree.Measure.EventCount(initialState))
        {
            location = default;
            return false;
        }

        var predicate = new EventPredicate(initialState, eventIndex);
        if (!_tree.TryLocate(predicate, out var before, out var element))
            throw new InvalidOperationException("The contextual event summary is inconsistent.");

        var stateBefore = before.FinalState(initialState);
        var transition = CheckedTransition(stateBefore, element!);
        location = new(
            before.Count,
            eventIndex - before.EventCount(initialState),
            stateBefore,
            transition.NextState,
            transition.EventCount);
        return true;
    }

    /// <summary>Returns a sequence with one element prepended.</summary>
    /// <param name="item">The element to prepend.</param>
    public ContextualRankSequence<TElement, TMachine> Prepend(TElement item) =>
        new(_tree.Prepend(item));

    /// <summary>Returns a sequence with one element appended.</summary>
    /// <param name="item">The element to append.</param>
    public ContextualRankSequence<TElement, TMachine> Append(TElement item) =>
        new(_tree.Append(item));

    /// <summary>Concatenates another compatible contextual sequence.</summary>
    /// <param name="other">The suffix.</param>
    /// <returns>The concatenation, reusing a nonempty operand when the other is empty.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public ContextualRankSequence<TElement, TMachine> Concat(
        ContextualRankSequence<TElement, TMachine> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (other.IsEmpty)
            return this;
        if (IsEmpty)
            return other;
        return new(_tree.Concat(other._tree));
    }

    /// <summary>Inserts one element at a zero-based boundary.</summary>
    /// <param name="index">The number of old elements before the insertion.</param>
    /// <param name="item">The inserted element.</param>
    /// <returns>The updated persistent version.</returns>
    public ContextualRankSequence<TElement, TMachine> Insert(int index, TElement item)
    {
        CheckBoundaryIndex(index);
        if (Count == int.MaxValue)
            throw new OverflowException("The sequence cannot contain more than Int32.MaxValue elements.");
        if (index == 0)
            return Prepend(item);
        if (index == Count)
            return Append(item);
        var (left, right) = _tree.Split(new PositionPredicate(index));
        return new(left.Append(item).Concat(right));
    }

    /// <summary>Replaces one element.</summary>
    /// <param name="index">The element index.</param>
    /// <param name="item">The replacement.</param>
    /// <returns>The updated persistent version.</returns>
    public ContextualRankSequence<TElement, TMachine> SetItem(int index, TElement item)
    {
        CheckElementIndex(index);
        _tree.TrySplitFind(new PositionPredicate(index), out var left, out _, out var right);
        return new(left.Append(item).Concat(right));
    }

    /// <summary>Removes one element.</summary>
    /// <param name="index">The element index.</param>
    /// <returns>The updated persistent version.</returns>
    public ContextualRankSequence<TElement, TMachine> RemoveAt(int index)
    {
        CheckElementIndex(index);
        _tree.TrySplitFind(new PositionPredicate(index), out var left, out _, out var right);
        var result = left.Concat(right);
        return result.IsEmpty ? EmptyInstance : new(result);
    }

    /// <summary>Splits at a zero-based element boundary.</summary>
    /// <param name="index">The number of elements in the left result.</param>
    /// <returns>The prefix and suffix.</returns>
    public (
        ContextualRankSequence<TElement, TMachine> Left,
        ContextualRankSequence<TElement, TMachine> Right) SplitAt(int index)
    {
        CheckBoundaryIndex(index);
        if (index == 0)
            return (EmptyInstance, this);
        if (index == Count)
            return (this, EmptyInstance);
        var (left, right) = _tree.Split(new PositionPredicate(index));
        return (new(left), new(right));
    }

    /// <summary>Returns a contiguous persistent slice.</summary>
    /// <param name="index">The first element index.</param>
    /// <param name="count">The number of retained elements.</param>
    /// <returns>The selected range.</returns>
    public ContextualRankSequence<TElement, TMachine> GetRange(int index, int count)
    {
        CheckRange(index, count);
        if (count == 0)
            return EmptyInstance;
        if (count == Count)
            return this;
        var (_, tail) = SplitAt(index);
        return tail.SplitAt(count).Left;
    }

    /// <summary>Copies the elements to an array in sequence order.</summary>
    /// <returns>A new array.</returns>
    public TElement[] ToArray() => _tree.ToArray();

    /// <summary>Gets an iterator in sequence order.</summary>
    /// <returns>The underlying persistent tree iterator.</returns>
    public IEnumerator<TElement> GetEnumerator() => _tree.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private static int ValidateStateCount(int stateCount) => stateCount > 0
        ? stateCount
        : throw new InvalidOperationException("A contextual event machine must have at least one state.");

    private static ContextualEventTransition CheckedTransition(int state, TElement element)
    {
        var transition = TMachine.Transition(state, element);
        if ((uint)transition.NextState >= (uint)MachineStateCount)
            throw new InvalidOperationException("The contextual event machine returned an invalid next state.");
        if (transition.EventCount < 0)
            throw new InvalidOperationException("The contextual event machine returned a negative event count.");
        return transition;
    }

    private static void CheckState(int state)
    {
        if ((uint)state >= (uint)MachineStateCount)
            throw new ArgumentOutOfRangeException(nameof(state), state, "State is outside the machine state range.");
    }

    private void CheckElementIndex(int index)
    {
        if ((uint)index >= (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(index), index, "Index is outside the sequence.");
    }

    private void CheckBoundaryIndex(int index)
    {
        if ((uint)index > (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(index), index, "Index is outside the sequence boundaries.");
    }

    private void CheckRange(int index, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (index > Count)
            throw new ArgumentOutOfRangeException(nameof(index), index, "Index is outside the sequence boundaries.");
        if (count > Count - index)
            throw new ArgumentOutOfRangeException(nameof(count), count, "The range extends past the sequence.");
    }

    private sealed class Summary
    {
        internal static readonly Summary Empty = CreateEmpty();

        private readonly int[] _nextStates;
        private readonly long[] _eventCounts;

        private Summary(int count, int[] nextStates, long[] eventCounts)
        {
            Count = count;
            _nextStates = nextStates;
            _eventCounts = eventCounts;
        }

        internal int Count { get; }

        internal int FinalState(int initialState) => _nextStates[initialState];

        internal long EventCount(int initialState) => _eventCounts[initialState];

        internal ContextualPrefixSummary Evaluate(int initialState) =>
            new(_nextStates[initialState], _eventCounts[initialState]);

        internal static Summary ForElement(TElement element)
        {
            var next = new int[MachineStateCount];
            var events = new long[MachineStateCount];
            for (var state = 0; state < MachineStateCount; state++)
            {
                var transition = CheckedTransition(state, element);
                next[state] = transition.NextState;
                events[state] = transition.EventCount;
            }
            return new(1, next, events);
        }

        internal static Summary Combine(Summary left, Summary right)
        {
            if (left.Count == 0)
                return right;
            if (right.Count == 0)
                return left;

            var next = new int[MachineStateCount];
            var events = new long[MachineStateCount];
            for (var state = 0; state < MachineStateCount; state++)
            {
                var middle = left._nextStates[state];
                next[state] = right._nextStates[middle];
                events[state] = checked(left._eventCounts[state] + right._eventCounts[middle]);
            }
            return new(checked(left.Count + right.Count), next, events);
        }

        private static Summary CreateEmpty()
        {
            var next = new int[MachineStateCount];
            for (var state = 0; state < MachineStateCount; state++)
                next[state] = state;
            return new(0, next, new long[MachineStateCount]);
        }
    }

    private readonly struct SummaryMeasure : IMeasure<TElement, Summary>
    {
        public static Summary Empty => Summary.Empty;

        public static Summary Combine(Summary left, Summary right) => Summary.Combine(left, right);

        public static Summary Measure(TElement element) => Summary.ForElement(element);
    }

    private readonly struct PositionPredicate(int index) : IMeasurePredicate<Summary>
    {
        public bool Invoke(Summary measure) => measure.Count > index;
    }

    private readonly struct EventPredicate(int initialState, long eventIndex) : IMeasurePredicate<Summary>
    {
        public bool Invoke(Summary measure) => measure.EventCount(initialState) > eventIndex;
    }

    private sealed class Tree
    {
        private readonly FingerTree<TElement, Summary, SummaryMeasure> _value;

        private Tree(FingerTree<TElement, Summary, SummaryMeasure> value) => _value = value;

        internal static Tree Empty { get; } = new(FingerTree<TElement, Summary, SummaryMeasure>.Empty);

        internal bool IsEmpty => _value.IsEmpty;

        internal Summary Measure => _value.Measure;

        internal static Tree Create(ReadOnlySpan<TElement> items) =>
            new(FingerTree<TElement, Summary, SummaryMeasure>.Create(items));

        internal static Tree CreateRange(IEnumerable<TElement> items) =>
            new(FingerTree<TElement, Summary, SummaryMeasure>.CreateRange(items));

        internal Tree Prepend(TElement item) => new(_value.Prepend(item));

        internal Tree Append(TElement item) => new(_value.Append(item));

        internal Tree Concat(Tree other) => new(_value.Concat(other._value));

        internal (Tree Left, Tree Right) Split<TPredicate>(TPredicate predicate)
            where TPredicate : struct, IMeasurePredicate<Summary>
        {
            var (left, right) = _value.Split(predicate);
            return (new(left), new(right));
        }

        internal bool TrySplitFind<TPredicate>(
            TPredicate predicate,
            out Tree left,
            [MaybeNullWhen(false)] out TElement found,
            out Tree right)
            where TPredicate : struct, IMeasurePredicate<Summary>
        {
            var result = _value.TrySplitFind(predicate, out var leftTree, out found, out var rightTree);
            left = new(leftTree);
            right = new(rightTree);
            return result;
        }

        internal bool TryLocate<TPredicate>(
            TPredicate predicate,
            out Summary before,
            [MaybeNullWhen(false)] out TElement found)
            where TPredicate : struct, IMeasurePredicate<Summary> =>
            _value.TryLocate(predicate, out before, out found);

        internal TElement[] ToArray() => _value.ToArray();

        internal IEnumerator<TElement> GetEnumerator() => _value.GetEnumerator();
    }
}
