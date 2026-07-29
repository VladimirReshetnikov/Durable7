using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

/// <summary>
/// Defines a constant-time-composable monotone action on heap priorities.
/// </summary>
/// <typeparam name="TPriority">The priority type.</typeparam>
/// <typeparam name="TAction">The immutable action value type.</typeparam>
/// <remarks>
/// Implementations must provide a semantic identity and associative composition, where
/// <c>Compose(outer, inner)</c> denotes <c>outer(inner(priority))</c>. Every action must be
/// monotone for the comparer retained by its heap: if <c>p &lt;= q</c>, then
/// <c>Apply(action, p) &lt;= Apply(action, q)</c>. Identity checks, composition, and application
/// are part of the heap's advertised bounds and must each take O(1) time. Results must remain
/// deterministic and semantically stable for the lifetime of every heap version.
/// </remarks>
public interface IMonotoneHeapAction<TPriority, TAction>
{
    /// <summary>Gets the semantic identity action.</summary>
    TAction Identity { get; }

    /// <summary>Returns whether an action is the semantic identity.</summary>
    bool IsIdentity(TAction action);

    /// <summary>Composes two actions as <c>outer(inner(priority))</c>.</summary>
    TAction Compose(TAction outer, TAction inner);

    /// <summary>Applies an action to one priority.</summary>
    TPriority Apply(TAction action, TPriority priority);
}

/// <summary>Identifies an action policy whose monotonicity is tied to one comparer object.</summary>
/// <typeparam name="TPriority">The priority type.</typeparam>
/// <typeparam name="TAction">The immutable action value type.</typeparam>
public interface IComparerBoundMonotoneHeapAction<TPriority, TAction>
    : IMonotoneHeapAction<TPriority, TAction>
{
    /// <summary>Gets the exact comparer for which this policy is monotone.</summary>
    IComparer<TPriority> Comparer { get; }
}

/// <summary>Pairs a payload with its current logical priority.</summary>
/// <typeparam name="TElement">The payload type.</typeparam>
/// <typeparam name="TPriority">The priority type.</typeparam>
/// <param name="Element">The payload.</param>
/// <param name="Priority">The logical priority.</param>
public readonly record struct MonotoneHeapEntry<TElement, TPriority>(
    TElement Element,
    TPriority Priority);

/// <summary>
/// Represents a lower bound, an upper bound, both bounds, or the identity over an ordered domain.
/// </summary>
/// <typeparam name="T">The ordered value type.</typeparam>
/// <remarks>
/// Values are created by <see cref="OrderClampPolicy{T}"/> so that two-bound actions are validated
/// with the same comparer used for application and composition.
/// </remarks>
public readonly struct OrderClamp<T> : IEquatable<OrderClamp<T>>
{
    private readonly T _constantValue;
    private readonly T _lowerBound;
    private readonly T _upperBound;

    internal OrderClamp(T constantValue)
    {
        IsConstant = true;
        _constantValue = constantValue;
        HasLowerBound = false;
        _lowerBound = default!;
        HasUpperBound = false;
        _upperBound = default!;
    }

    internal OrderClamp(bool hasLowerBound, T lowerBound, bool hasUpperBound, T upperBound)
    {
        IsConstant = false;
        _constantValue = default!;
        HasLowerBound = hasLowerBound;
        _lowerBound = lowerBound;
        HasUpperBound = hasUpperBound;
        _upperBound = upperBound;
    }

    /// <summary>Gets whether this action is an exact constant function.</summary>
    public bool IsConstant { get; }

    /// <summary>Gets the constant result.</summary>
    /// <exception cref="InvalidOperationException">The action is not constant.</exception>
    public T ConstantValue => IsConstant
        ? _constantValue
        : throw new InvalidOperationException("The clamp is not constant.");

    /// <summary>Gets whether this action has a lower bound.</summary>
    public bool HasLowerBound { get; }

    /// <summary>Gets whether this action has an upper bound.</summary>
    public bool HasUpperBound { get; }

    /// <summary>Gets the lower bound.</summary>
    /// <exception cref="InvalidOperationException">The action has no lower bound.</exception>
    public T LowerBound => HasLowerBound
        ? _lowerBound
        : throw new InvalidOperationException("The clamp has no lower bound.");

    /// <summary>Gets the upper bound.</summary>
    /// <exception cref="InvalidOperationException">The action has no upper bound.</exception>
    public T UpperBound => HasUpperBound
        ? _upperBound
        : throw new InvalidOperationException("The clamp has no upper bound.");

    internal T LowerBoundOrDefault => _lowerBound;

    internal T UpperBoundOrDefault => _upperBound;

    internal T ConstantValueOrDefault => _constantValue;

    /// <inheritdoc />
    public bool Equals(OrderClamp<T> other) =>
        IsConstant == other.IsConstant &&
        (!IsConstant || EqualityComparer<T>.Default.Equals(_constantValue, other._constantValue)) &&
        HasLowerBound == other.HasLowerBound &&
        HasUpperBound == other.HasUpperBound &&
        (!HasLowerBound || EqualityComparer<T>.Default.Equals(_lowerBound, other._lowerBound)) &&
        (!HasUpperBound || EqualityComparer<T>.Default.Equals(_upperBound, other._upperBound));

    /// <inheritdoc />
    public override bool Equals(object? obj) => obj is OrderClamp<T> other && Equals(other);

    /// <inheritdoc />
    public override int GetHashCode()
    {
        var hash = new HashCode();
        hash.Add(IsConstant);
        if (IsConstant)
            hash.Add(_constantValue);
        hash.Add(HasLowerBound);
        if (HasLowerBound)
            hash.Add(_lowerBound);
        hash.Add(HasUpperBound);
        if (HasUpperBound)
            hash.Add(_upperBound);
        return hash.ToHashCode();
    }

    /// <summary>Tests two clamp values for equality.</summary>
    public static bool operator ==(OrderClamp<T> left, OrderClamp<T> right) => left.Equals(right);

    /// <summary>Tests two clamp values for inequality.</summary>
    public static bool operator !=(OrderClamp<T> left, OrderClamp<T> right) => !left.Equals(right);
}

/// <summary>
/// Supplies the closed monotone action family <c>x -&gt; clamp(x, lower, upper)</c>.
/// </summary>
/// <typeparam name="T">The ordered value type.</typeparam>
/// <remarks>
/// Missing bounds denote negative or positive infinity. Composition remains one constant-sized
/// clamp or an explicit constant function. The explicit constant case preserves exact results even
/// when a comparer treats distinct priority values as equal.
/// </remarks>
public sealed class OrderClampPolicy<T>
    : IComparerBoundMonotoneHeapAction<T, OrderClamp<T>>
{
    /// <summary>Initializes a clamp policy with the default comparer.</summary>
    public OrderClampPolicy()
        : this(null)
    {
    }

    /// <summary>Initializes a clamp policy with a retained comparer.</summary>
    /// <param name="comparer">The comparer, or <see langword="null"/> for the default.</param>
    public OrderClampPolicy(IComparer<T>? comparer)
    {
        Comparer = comparer ?? Comparer<T>.Default;
    }

    /// <summary>Gets the retained comparer.</summary>
    public IComparer<T> Comparer { get; }

    /// <inheritdoc />
    public OrderClamp<T> Identity => default;

    /// <summary>Creates a floor action.</summary>
    public OrderClamp<T> AtLeast(T lowerBound) => new(true, lowerBound, false, default!);

    /// <summary>Creates a cap action.</summary>
    public OrderClamp<T> AtMost(T upperBound) => new(false, default!, true, upperBound);

    /// <summary>Creates an exact constant action.</summary>
    public OrderClamp<T> Constant(T value) => new(value);

    /// <summary>Creates a two-sided clamp action.</summary>
    /// <exception cref="ArgumentException">The lower bound compares greater than the upper bound.</exception>
    public OrderClamp<T> Between(T lowerBound, T upperBound)
    {
        if (Comparer.Compare(lowerBound, upperBound) > 0)
            throw new ArgumentException("The lower bound must not compare greater than the upper bound.");
        return new OrderClamp<T>(true, lowerBound, true, upperBound);
    }

    /// <inheritdoc />
    public bool IsIdentity(OrderClamp<T> action) =>
        !action.IsConstant && !action.HasLowerBound && !action.HasUpperBound;

    /// <inheritdoc />
    public OrderClamp<T> Compose(OrderClamp<T> outer, OrderClamp<T> inner)
    {
        if (IsIdentity(outer))
            return inner;
        if (IsIdentity(inner))
            return outer;

        if (outer.IsConstant)
            return outer;
        if (inner.IsConstant)
            return Constant(Apply(outer, inner.ConstantValueOrDefault));

        if (inner.HasUpperBound && outer.HasLowerBound &&
            Comparer.Compare(inner.UpperBoundOrDefault, outer.LowerBoundOrDefault) < 0)
        {
            return Constant(outer.LowerBoundOrDefault);
        }
        if (inner.HasLowerBound && outer.HasUpperBound &&
            Comparer.Compare(inner.LowerBoundOrDefault, outer.UpperBoundOrDefault) > 0)
        {
            return Constant(outer.UpperBoundOrDefault);
        }

        var hasLower = inner.HasLowerBound || outer.HasLowerBound;
        var lower = !inner.HasLowerBound
            ? outer.LowerBoundOrDefault
            : !outer.HasLowerBound ||
              Comparer.Compare(inner.LowerBoundOrDefault, outer.LowerBoundOrDefault) >= 0
                ? inner.LowerBoundOrDefault
                : outer.LowerBoundOrDefault;
        var hasUpper = inner.HasUpperBound || outer.HasUpperBound;
        var upper = !inner.HasUpperBound
            ? outer.UpperBoundOrDefault
            : !outer.HasUpperBound ||
              Comparer.Compare(inner.UpperBoundOrDefault, outer.UpperBoundOrDefault) <= 0
                ? inner.UpperBoundOrDefault
                : outer.UpperBoundOrDefault;
        return new OrderClamp<T>(hasLower, lower, hasUpper, upper);
    }

    /// <inheritdoc />
    public T Apply(OrderClamp<T> action, T priority)
    {
        if (action.IsConstant)
            return action.ConstantValueOrDefault;
        if (action.HasLowerBound && Comparer.Compare(priority, action.LowerBoundOrDefault) < 0)
            return action.LowerBoundOrDefault;
        if (action.HasUpperBound && Comparer.Compare(priority, action.UpperBoundOrDefault) > 0)
            return action.UpperBoundOrDefault;
        return priority;
    }
}

/// <summary>
/// Represents an immutable Brodal-Okasaki heap with lazily composable monotone priority actions.
/// </summary>
/// <typeparam name="TElement">The payload type.</typeparam>
/// <typeparam name="TPriority">The priority type.</typeparam>
/// <typeparam name="TAction">The immutable action value type.</typeparam>
/// <remarks>
/// <para>
/// Insert, minimum, and meld are O(1) worst-case; delete-min is O(log n) worst-case. Applying one
/// action to every current priority is O(1) worst-case and allocates O(1) structure. Enumeration is
/// Theta(n). These bounds include O(1)-time action-policy calls and priority comparisons.
/// </para>
/// <para>
/// Actions are stored recursively on trees and forest spines. This is load-bearing: a transformed
/// heap can later receive an untransformed insertion or meld with a differently transformed heap,
/// including when an action such as a clamp has no inverse. Tags are composed only on components
/// already exposed by the underlying bootstrapped skew-binomial algorithm.
/// </para>
/// <para>
/// The action policy is trusted. Violating its identity, associative-composition, monotonicity, or
/// O(1)-operation contract invalidates the semantic and complexity guarantees. Every update is
/// persistent and leaves all source heaps usable. Composition must preserve the policy's observable
/// priority semantics, including any representative distinctions that callers care about.
/// The comparer must likewise remain a stable total preorder over retained priorities. Mutating
/// comparer-, policy-, or priority-observable state can retroactively invalidate existing versions.
/// </para>
/// </remarks>
[DebuggerDisplay("Count = {Count}, Minimum = {MinimumForDebugger}")]
public sealed class PersistentMonotoneActionHeap<TElement, TPriority, TAction>
    : IEnumerable<MonotoneHeapEntry<TElement, TPriority>>
{
    private readonly Tree? _root;

    private PersistentMonotoneActionHeap(
        Tree? root,
        int count,
        IComparer<TPriority> comparer,
        IMonotoneHeapAction<TPriority, TAction> actionPolicy)
    {
        _root = root;
        Count = count;
        Comparer = comparer;
        ActionPolicy = actionPolicy;
    }

    /// <summary>Gets the number of entries.</summary>
    public int Count { get; }

    /// <summary>Gets whether the heap is empty.</summary>
    public bool IsEmpty => _root is null;

    /// <summary>Gets the retained priority comparer.</summary>
    public IComparer<TPriority> Comparer { get; }

    /// <summary>Gets the retained action policy.</summary>
    public IMonotoneHeapAction<TPriority, TAction> ActionPolicy { get; }

    /// <summary>Gets a minimum entry in O(1) worst-case time.</summary>
    /// <exception cref="InvalidOperationException">The heap is empty.</exception>
    public MonotoneHeapEntry<TElement, TPriority> Minimum => _root is null
        ? throw new InvalidOperationException("The heap is empty.")
        : EntryOf(_root);

    private object? MinimumForDebugger => _root is null ? null : EntryOf(_root);

    internal object? RootIdentity => _root;

    /// <summary>Creates an empty heap retaining an action policy and comparer.</summary>
    /// <param name="actionPolicy">The monotone action policy.</param>
    /// <param name="comparer">The priority comparer, or <see langword="null"/> for the default.</param>
    public static PersistentMonotoneActionHeap<TElement, TPriority, TAction> Create(
        IMonotoneHeapAction<TPriority, TAction> actionPolicy,
        IComparer<TPriority>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(actionPolicy);
        if (actionPolicy is IComparerBoundMonotoneHeapAction<TPriority, TAction> boundPolicy)
        {
            comparer ??= boundPolicy.Comparer;
            if (!ReferenceEquals(comparer, boundPolicy.Comparer))
            {
                throw new ArgumentException(
                    "The comparer must be the exact comparer retained by the action policy.",
                    nameof(comparer));
            }
        }
        else
        {
            comparer ??= Comparer<TPriority>.Default;
        }
        return new PersistentMonotoneActionHeap<TElement, TPriority, TAction>(
            null,
            0,
            comparer,
            actionPolicy);
    }

    /// <summary>Creates a heap from entries in O(n) time by repeated worst-case-O(1) insertion.</summary>
    public static PersistentMonotoneActionHeap<TElement, TPriority, TAction> CreateRange(
        IEnumerable<MonotoneHeapEntry<TElement, TPriority>> entries,
        IMonotoneHeapAction<TPriority, TAction> actionPolicy,
        IComparer<TPriority>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var result = Create(actionPolicy, comparer);
        foreach (var entry in entries)
            result = result.Insert(entry.Element, entry.Priority);
        return result;
    }

    /// <summary>Tries to get a minimum entry in O(1) worst-case time.</summary>
    public bool TryGetMinimum(
        [MaybeNullWhen(false)] out MonotoneHeapEntry<TElement, TPriority> minimum)
    {
        if (_root is null)
        {
            minimum = default;
            return false;
        }
        minimum = EntryOf(_root);
        return true;
    }

    /// <summary>Inserts an entry in O(1) worst-case time.</summary>
    public PersistentMonotoneActionHeap<TElement, TPriority, TAction> Insert(
        TElement element,
        TPriority priority)
    {
        var singleton = new Tree(0, element, priority, null, ActionPolicy.Identity);
        if (_root is null)
            return New(singleton, 1);

        Tree root;
        if (LessOrEqual(singleton, _root))
        {
            root = new Tree(
                0,
                element,
                priority,
                Cons(_root, null),
                ActionPolicy.Identity);
        }
        else
        {
            var exposed = Expose(_root);
            root = new Tree(
                0,
                exposed.Element,
                exposed.Priority,
                SkewInsert(singleton, exposed.Children),
                ActionPolicy.Identity);
        }
        return New(root, checked(Count + 1));
    }

    /// <summary>
    /// Applies an action lazily to every current priority in O(1) worst-case time.
    /// Future insertions are not retroactively transformed.
    /// </summary>
    public PersistentMonotoneActionHeap<TElement, TPriority, TAction> TransformAll(TAction action)
    {
        if (_root is null || ActionPolicy.IsIdentity(action))
            return this;
        return New(TagTree(_root, action), Count);
    }

    /// <summary>Melds two heaps in O(1) worst-case time.</summary>
    /// <exception cref="ArgumentException">
    /// The heaps do not retain the same comparer and action-policy objects.
    /// </exception>
    public PersistentMonotoneActionHeap<TElement, TPriority, TAction> Meld(
        PersistentMonotoneActionHeap<TElement, TPriority, TAction> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(Comparer, other.Comparer) ||
            !ReferenceEquals(ActionPolicy, other.ActionPolicy))
        {
            throw new ArgumentException(
                "Heap melding requires the same comparer and action-policy objects.",
                nameof(other));
        }
        if (_root is null)
            return other;
        if (other._root is null)
            return this;

        var leftWins = LessOrEqual(_root, other._root);
        var winner = Expose(leftWins ? _root : other._root);
        var loser = leftWins ? other._root : _root;
        var root = new Tree(
            0,
            winner.Element,
            winner.Priority,
            SkewInsert(loser, winner.Children),
            ActionPolicy.Identity);
        return New(root, checked(Count + other.Count));
    }

    /// <summary>Removes a minimum entry in O(log n) worst-case time.</summary>
    /// <exception cref="InvalidOperationException">The heap is empty.</exception>
    public PersistentMonotoneActionHeap<TElement, TPriority, TAction> DeleteMinimum()
    {
        if (_root is null)
            throw new InvalidOperationException("The heap is empty.");

        var root = Expose(_root);
        if (root.Children is null)
            return Create(ActionPolicy, Comparer);

        var (taggedMinimum, remainder) = GetMinimum(root.Children);
        var minimum = Expose(taggedMinimum);
        var (zeros, trees, forest) = SplitForest(
            minimum.Rank,
            null,
            null,
            minimum.Children);
        var merged = SkewMeld(SkewMeld(trees, remainder), forest);
        foreach (var zero in ToReverseArray(zeros))
            merged = SkewInsert(zero, merged);
        return New(
            new Tree(
                0,
                minimum.Element,
                minimum.Priority,
                merged,
                ActionPolicy.Identity),
            checked(Count - 1));
    }

    /// <summary>Tries to remove a minimum entry in O(log n) worst-case time.</summary>
    public bool TryDeleteMinimum(
        [MaybeNullWhen(false)] out MonotoneHeapEntry<TElement, TPriority> minimum,
        out PersistentMonotoneActionHeap<TElement, TPriority, TAction> remainder)
    {
        if (_root is null)
        {
            minimum = default;
            remainder = this;
            return false;
        }
        minimum = EntryOf(_root);
        remainder = DeleteMinimum();
        return true;
    }

    /// <summary>
    /// Validates rank encodings, logical heap order through every pending action, and count in O(n).
    /// </summary>
    public PersistentMonotoneActionHeapStatistics ValidateStructure()
    {
        if (_root is null)
        {
            if (Count != 0)
                throw new InvalidOperationException("An empty action heap has a nonzero count.");
            return new PersistentMonotoneActionHeapStatistics(0, 0, 0, 0, 0);
        }
        if (_root.Rank != 0)
            throw new InvalidOperationException("The global heap root must have rank zero.");

        var taggedComponents = 0;
        var normalizedRoot = Expose(_root);
        var rootForestLength = ValidateSkewForest(normalizedRoot.Children);
        var pending = new Stack<TreeFrame>();
        pending.Push(new TreeFrame(_root, 1));
        var logicalCount = 0;
        var maximumRank = 0;
        var maximumDepth = 0;
        while (pending.TryPop(out var frame))
        {
            var tree = ObserveTagAndExpose(frame.Tree, ref taggedComponents);
            if (tree.Rank < 0)
                throw new InvalidOperationException("An action-heap tree has a negative rank.");
            logicalCount = checked(logicalCount + 1);
            maximumRank = Math.Max(maximumRank, tree.Rank);
            maximumDepth = Math.Max(maximumDepth, frame.Depth);
            ValidateSkewForest(ValidateFusedChildren(tree));
            for (var forest = tree.Children; forest is not null; forest = ForestTail(forest))
            {
                if (!ActionPolicy.IsIdentity(forest.Action))
                    taggedComponents++;
                var child = ForestHead(forest);
                if (!LessOrEqual(tree, child))
                    throw new InvalidOperationException("An action-heap child outranks its parent.");
                pending.Push(new TreeFrame(child, checked(frame.Depth + 1)));
            }
        }
        if (logicalCount != Count)
            throw new InvalidOperationException("The action heap's logical count is invalid.");
        return new PersistentMonotoneActionHeapStatistics(
            Count,
            rootForestLength,
            maximumRank,
            maximumDepth,
            taggedComponents);
    }

    /// <summary>Returns an O(n)-time enumerator in unspecified structural order.</summary>
    public IEnumerator<MonotoneHeapEntry<TElement, TPriority>> GetEnumerator() =>
        Enumerate().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private IEnumerable<MonotoneHeapEntry<TElement, TPriority>> Enumerate()
    {
        if (_root is null)
            yield break;
        var stack = new Stack<Tree>();
        stack.Push(_root);
        while (stack.TryPop(out var tagged))
        {
            var tree = Expose(tagged);
            yield return new MonotoneHeapEntry<TElement, TPriority>(tree.Element, tree.Priority);
            for (var forest = tree.Children; forest is not null; forest = ForestTail(forest))
                stack.Push(ForestHead(forest));
        }
    }

    private PersistentMonotoneActionHeap<TElement, TPriority, TAction> New(Tree? root, int count) =>
        new(root, count, Comparer, ActionPolicy);

    private MonotoneHeapEntry<TElement, TPriority> EntryOf(Tree tree) =>
        new(tree.Element, ActionPolicy.Apply(tree.Action, tree.Priority));

    private bool LessOrEqual(Tree left, Tree right) =>
        Comparer.Compare(
            ActionPolicy.Apply(left.Action, left.Priority),
            ActionPolicy.Apply(right.Action, right.Priority)) <= 0;

    private Tree TagTree(Tree tree, TAction outer)
    {
        if (ActionPolicy.IsIdentity(outer))
            return tree;
        return new Tree(
            tree.Rank,
            tree.Element,
            tree.Priority,
            tree.Children,
            ActionPolicy.Compose(outer, tree.Action));
    }

    private Forest TagForest(Forest forest, TAction outer)
    {
        if (ActionPolicy.IsIdentity(outer))
            return forest;
        return new Forest(
            forest.RawHead,
            forest.RawTail,
            ActionPolicy.Compose(outer, forest.Action));
    }

    private Tree Expose(Tree tree)
    {
        if (ActionPolicy.IsIdentity(tree.Action))
            return tree;
        return new Tree(
            tree.Rank,
            tree.Element,
            ActionPolicy.Apply(tree.Action, tree.Priority),
            tree.Children is null ? null : TagForest(tree.Children, tree.Action),
            ActionPolicy.Identity);
    }

    private Tree ObserveTagAndExpose(Tree tree, ref int taggedComponents)
    {
        if (!ActionPolicy.IsIdentity(tree.Action))
            taggedComponents++;
        return Expose(tree);
    }

    private Forest Cons(Tree head, Forest? tail) =>
        new(head, tail, ActionPolicy.Identity);

    private Tree ForestHead(Forest forest) => TagTree(forest.RawHead, forest.Action);

    private Forest? ForestTail(Forest forest) => forest.RawTail is null
        ? null
        : TagForest(forest.RawTail, forest.Action);

    private Forest SkewInsert(Tree tree, Forest? forest)
    {
        if (forest is not null)
        {
            var tail = ForestTail(forest);
            if (tail is not null && forest.RawHead.Rank == tail.RawHead.Rank)
            {
                return Cons(
                    SkewLink(tree, ForestHead(forest), ForestHead(tail)),
                    ForestTail(tail));
            }
        }
        return Cons(tree, forest);
    }

    private Tree SkewLink(Tree zero, Tree first, Tree second)
    {
        Debug.Assert(first.Rank == second.Rank);
        if (LessOrEqual(first, zero) && LessOrEqual(first, second))
        {
            var winner = Expose(first);
            return new Tree(
                first.Rank + 1,
                winner.Element,
                winner.Priority,
                Cons(zero, Cons(second, winner.Children)),
                ActionPolicy.Identity);
        }
        if (LessOrEqual(second, zero) && LessOrEqual(second, first))
        {
            var winner = Expose(second);
            return new Tree(
                second.Rank + 1,
                winner.Element,
                winner.Priority,
                Cons(zero, Cons(first, winner.Children)),
                ActionPolicy.Identity);
        }
        var exposedZero = Expose(zero);
        return new Tree(
            first.Rank + 1,
            exposedZero.Element,
            exposedZero.Priority,
            Cons(first, Cons(second, exposedZero.Children)),
            ActionPolicy.Identity);
    }

    private Tree Link(Tree first, Tree second)
    {
        Debug.Assert(first.Rank == second.Rank);
        var firstWins = LessOrEqual(first, second);
        var winner = Expose(firstWins ? first : second);
        var loser = firstWins ? second : first;
        return new Tree(
            winner.Rank + 1,
            winner.Element,
            winner.Priority,
            Cons(loser, winner.Children),
            ActionPolicy.Identity);
    }

    private (Tree Minimum, Forest? Remainder) GetMinimum(Forest forest)
    {
        var head = ForestHead(forest);
        var tail = ForestTail(forest);
        if (tail is null)
            return (head, null);
        var (minimum, remainder) = GetMinimum(tail);
        return LessOrEqual(head, minimum)
            ? (head, tail)
            : (minimum, Cons(head, remainder));
    }

    private (Forest? Zeros, Forest? Trees, Forest? Forest) SplitForest(
        int rank,
        Forest? zeros,
        Forest? trees,
        Forest? forest)
    {
        if (rank == 0)
            return (zeros, trees, forest);
        if (forest is null)
            throw new InvalidOperationException("Invalid skew-binomial forest invariant.");

        var first = ForestHead(forest);
        var tail = ForestTail(forest);
        if (rank == 1 && tail is null)
            return (zeros, Cons(first, trees), null);
        if (tail is null)
            throw new InvalidOperationException("Invalid skew-binomial forest invariant.");

        var second = ForestHead(tail);
        var rest = ForestTail(tail);
        if (rank == 1)
        {
            return second.Rank == 0
                ? (Cons(first, zeros), Cons(second, trees), rest)
                : (zeros, Cons(first, trees), Cons(second, rest));
        }
        if (first.Rank == second.Rank)
            return (zeros, Cons(first, Cons(second, trees)), rest);
        return first.Rank == 0
            ? SplitForest(rank - 1, Cons(first, zeros), Cons(second, trees), rest)
            : SplitForest(rank - 1, zeros, Cons(first, trees), Cons(second, rest));
    }

    private Forest? SkewMeld(Forest? left, Forest? right) =>
        UnionUnique(Uniquify(left), Uniquify(right));

    private Forest? Uniquify(Forest? forest) => forest is null
        ? null
        : InsertRanked(ForestHead(forest), Uniquify(ForestTail(forest)));

    private Forest InsertRanked(Tree tree, Forest? forest)
    {
        if (forest is null)
            return Cons(tree, null);
        var head = ForestHead(forest);
        if (tree.Rank < head.Rank)
            return Cons(tree, forest);
        return InsertRanked(Link(tree, head), ForestTail(forest));
    }

    private Forest? UnionUnique(Forest? left, Forest? right)
    {
        if (left is null)
            return right;
        if (right is null)
            return left;
        var leftHead = ForestHead(left);
        var rightHead = ForestHead(right);
        if (leftHead.Rank < rightHead.Rank)
            return Cons(leftHead, UnionUnique(ForestTail(left), right));
        if (leftHead.Rank > rightHead.Rank)
            return Cons(rightHead, UnionUnique(left, ForestTail(right)));
        return InsertRanked(
            Link(leftHead, rightHead),
            UnionUnique(ForestTail(left), ForestTail(right)));
    }

    private Tree[] ToReverseArray(Forest? forest)
    {
        var trees = new List<Tree>();
        for (; forest is not null; forest = ForestTail(forest))
            trees.Add(ForestHead(forest));
        trees.Reverse();
        return [.. trees];
    }

    private Forest? ValidateFusedChildren(Tree tree)
    {
        var rank = tree.Rank;
        var forest = tree.Children;
        while (rank > 0)
        {
            if (forest is null)
                throw new InvalidOperationException("A ranked action-heap tree is missing children.");
            var first = ForestHead(forest);
            var tail = ForestTail(forest);
            if (rank == 1)
            {
                if (first.Rank != 0)
                    throw new InvalidOperationException("A rank-one tree must begin with rank zero.");
                if (tail is null)
                    return null;
                return ForestHead(tail).Rank == 0 ? ForestTail(tail) : tail;
            }
            if (tail is null)
                throw new InvalidOperationException("A ranked action-heap tree has incomplete children.");
            var second = ForestHead(tail);
            if (first.Rank == second.Rank)
            {
                if (first.Rank != rank - 1)
                    throw new InvalidOperationException("A skew-linked tree has invalid child ranks.");
                return ForestTail(tail);
            }
            if (first.Rank == 0)
            {
                if (second.Rank != rank - 1)
                    throw new InvalidOperationException("A skew-linked tree has an invalid ranked child.");
                forest = ForestTail(tail);
            }
            else
            {
                if (first.Rank != rank - 1)
                    throw new InvalidOperationException("A linked tree has an invalid ranked child.");
                forest = tail;
            }
            rank--;
        }
        return forest;
    }

    private int ValidateSkewForest(Forest? forest)
    {
        var length = 0;
        var previousRank = -1;
        var duplicate = false;
        for (; forest is not null; forest = ForestTail(forest))
        {
            var rank = forest.RawHead.Rank;
            if (rank < 0)
                throw new InvalidOperationException("An action-heap forest contains a negative rank.");
            if (rank < previousRank)
                throw new InvalidOperationException("Action-heap forest ranks are not nondecreasing.");
            if (rank == previousRank)
            {
                if (length != 1 || duplicate)
                    throw new InvalidOperationException("Only the first two forest ranks may be equal.");
                duplicate = true;
            }
            previousRank = rank;
            length++;
        }
        return length;
    }

    private readonly record struct TreeFrame(Tree Tree, int Depth);

    private sealed class Tree(
        int rank,
        TElement element,
        TPriority priority,
        Forest? children,
        TAction action)
    {
        internal int Rank { get; } = rank;
        internal TElement Element { get; } = element;
        internal TPriority Priority { get; } = priority;
        internal Forest? Children { get; } = children;
        internal TAction Action { get; } = action;
    }

    private sealed class Forest(Tree rawHead, Forest? rawTail, TAction action)
    {
        internal Tree RawHead { get; } = rawHead;
        internal Forest? RawTail { get; } = rawTail;
        internal TAction Action { get; } = action;
    }
}

/// <summary>Describes a validated persistent monotone-action heap representation.</summary>
/// <param name="Count">The logical entry count.</param>
/// <param name="RootForestLength">The number of trees directly below the global root.</param>
/// <param name="MaximumRank">The largest skew-binomial rank.</param>
/// <param name="MaximumDepth">The deepest global-root-to-tree path.</param>
/// <param name="TaggedComponentCount">
/// The number of logical pending tree/forest-tag observations made during validation. This is not
/// a distinct-object count because exposing one uniform forest tag produces tagged logical views.
/// </param>
public readonly record struct PersistentMonotoneActionHeapStatistics(
    int Count,
    int RootForestLength,
    int MaximumRank,
    int MaximumDepth,
    int TaggedComponentCount);
