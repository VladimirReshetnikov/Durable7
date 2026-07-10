namespace Tools.DataStructures.FingerTree;

/// <summary>The empty level of a measured finger tree.</summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TChild">Element type at this level.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
internal sealed class EmptyMeasuredTree<TElement, TChild, TMeasure, TMonoid>
    : MeasuredTree<TElement, TChild, TMeasure, TMonoid>
    where TChild : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <summary>Gets the canonical empty tree for this level.</summary>
    public static readonly EmptyMeasuredTree<TElement, TChild, TMeasure, TMonoid> Instance = new();

    private EmptyMeasuredTree()
    {
    }

    /// <inheritdoc/>
    public override TMeasure Measure => TMonoid.Empty;

    /// <inheritdoc/>
    public override bool IsEmpty => true;

    /// <inheritdoc/>
    public override TChild First => throw EmptyAccess();

    /// <inheritdoc/>
    public override TChild Last => throw EmptyAccess();

    /// <inheritdoc/>
    public override MeasuredTree<TElement, TChild, TMeasure, TMonoid> Cons(TChild value) => Single(value);

    /// <inheritdoc/>
    public override MeasuredTree<TElement, TChild, TMeasure, TMonoid> Snoc(TChild value) => Single(value);

    /// <inheritdoc/>
    public override bool TryViewLeft(out TChild head, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest)
    {
        head = default!;
        rest = this;
        return false;
    }

    /// <inheritdoc/>
    public override bool TryViewRight(out TChild last, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest)
    {
        last = default!;
        rest = this;
        return false;
    }

    /// <inheritdoc/>
    public override void Flatten(ICollection<TElement> sink)
    {
    }

    /// <inheritdoc/>
    public override int ValidateAndCount() => 0;

    /// <inheritdoc/>
    public override int ChildCount => 0;

    /// <inheritdoc/>
    public override bool TryGetChild(int index, out TElement leaf, out IEnumerationBlock<TElement>? block) =>
        throw EmptyAccess();

    /// <inheritdoc/>
    public override (MeasuredTree<TElement, TChild, TMeasure, TMonoid> Left, TChild Hit, MeasuredTree<TElement, TChild, TMeasure, TMonoid> Right)
        SplitTree<TPredicate>(TPredicate predicate, TMeasure accumulator) =>
        throw EmptyAccess();

    /// <inheritdoc/>
    public override (TMeasure MeasureBefore, TChild Hit) LocateTree<TPredicate>(TPredicate predicate, TMeasure accumulator) =>
        throw EmptyAccess();

    private static InvalidOperationException EmptyAccess() =>
        new("Internal invariant violation: element access on an empty measured finger-tree level.");
}

/// <summary>A one-element level of a measured finger tree.</summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TChild">Element type at this level.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
/// <param name="element">The single stored element.</param>
internal sealed class SingleMeasuredTree<TElement, TChild, TMeasure, TMonoid>(TChild element)
    : MeasuredTree<TElement, TChild, TMeasure, TMonoid>
    where TChild : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <summary>The single stored element.</summary>
    public readonly TChild Element = element;

    /// <inheritdoc/>
    public override TMeasure Measure => Element.Measure;

    /// <inheritdoc/>
    public override bool IsEmpty => false;

    /// <inheritdoc/>
    public override TChild First => Element;

    /// <inheritdoc/>
    public override TChild Last => Element;

    /// <inheritdoc/>
    public override MeasuredTree<TElement, TChild, TMeasure, TMonoid> Cons(TChild value) =>
        Deep([value], EmptyMiddle, [Element]);

    /// <inheritdoc/>
    public override MeasuredTree<TElement, TChild, TMeasure, TMonoid> Snoc(TChild value) =>
        Deep([Element], EmptyMiddle, [value]);

    /// <inheritdoc/>
    public override bool TryViewLeft(out TChild head, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest)
    {
        head = Element;
        rest = Empty;
        return true;
    }

    /// <inheritdoc/>
    public override bool TryViewRight(out TChild last, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest)
    {
        last = Element;
        rest = Empty;
        return true;
    }

    /// <inheritdoc/>
    public override void Flatten(ICollection<TElement> sink) => Element.Flatten(sink);

    /// <inheritdoc/>
    public override int ValidateAndCount()
    {
        var count = Element.ValidateAndCount();
        if (!EqualityComparer<TMeasure>.Default.Equals(Element.Measure, Measure))
            throw new InvalidOperationException("Measured finger-tree invariant violated: a single tree's cached measure is stale.");
        return count;
    }

    /// <inheritdoc/>
    public override int ChildCount => 1;

    /// <inheritdoc/>
    public override bool TryGetChild(int index, out TElement leaf, out IEnumerationBlock<TElement>? block)
    {
        if (index != 0)
            throw new ArgumentOutOfRangeException(nameof(index), index, "The single measured tree has one child.");
        return Element.TryGetLeaf(out leaf, out block);
    }

    /// <inheritdoc/>
    public override (MeasuredTree<TElement, TChild, TMeasure, TMonoid> Left, TChild Hit, MeasuredTree<TElement, TChild, TMeasure, TMonoid> Right)
        SplitTree<TPredicate>(TPredicate predicate, TMeasure accumulator) =>
        (Empty, Element, Empty);

    /// <inheritdoc/>
    public override (TMeasure MeasureBefore, TChild Hit) LocateTree<TPredicate>(TPredicate predicate, TMeasure accumulator) =>
        (accumulator, Element);
}

/// <summary>
/// A deep level of a measured finger tree: a one-through-four element prefix, a lazily memoized middle tree of
/// grouping nodes, and a one-through-four element suffix, with a lazily memoized combined measure.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TChild">Element type at this level.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
/// <remarks>
/// The middle is held behind a <see cref="MeasuredMiddle{TElement, TNode, TMeasure, TMonoid}"/> suspension so
/// deep spine repairs are deferred and memoized — the strict-language realization of Hinze and Paterson's lazy
/// finger tree that makes the amortized bounds hold under branching persistence. The measure is computed on
/// first read and memoized: constructing a deep node never forces its middle (a push suspension answers its
/// measure arithmetically; a pop suspension defers it), so the spine stays lazy. The first read of a measure
/// may force a chain of suspensions O(log n) deep; subsequent reads are O(1).
/// </remarks>
internal sealed class DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>
    : MeasuredTree<TElement, TChild, TMeasure, TMonoid>
    where TChild : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <summary>Prefix buffer; one through four elements.</summary>
    public readonly TChild[] Prefix;

    /// <summary>Suffix buffer; one through four elements.</summary>
    public readonly TChild[] Suffix;

    /// <summary>Middle tree of grouping nodes, one level closer to the leaves, possibly suspended.</summary>
    private readonly MeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid> _middle;

    /// <summary>Memoized boxed measure; <see langword="null"/> until first read, then the boxed <typeparamref name="TMeasure"/>.</summary>
    private object? _measureBox;

    /// <summary>Builds a deep level without forcing its middle or computing its measure.</summary>
    /// <param name="prefix">One through four prefix elements.</param>
    /// <param name="middle">Middle tree of nodes, computed or suspended.</param>
    /// <param name="suffix">One through four suffix elements.</param>
    public DeepMeasuredTree(
        TChild[] prefix,
        MeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid> middle,
        TChild[] suffix)
    {
        Prefix = prefix;
        _middle = middle;
        Suffix = suffix;
    }

    /// <inheritdoc/>
    public override TMeasure Measure
    {
        get
        {
            if (Volatile.Read(ref _measureBox) is { } existing)
                return (TMeasure)existing;

            // Reading _middle.Measure forces only a pop suspension (push answers arithmetically); the result is
            // memoized once via compare-exchange, so concurrent readers converge on a single boxed value.
            var measure = TMonoid.Combine(TMonoid.Combine(CombineAll(Prefix), _middle.Measure), CombineAll(Suffix));
            var boxed = (object)measure!;
            var witnessed = Interlocked.CompareExchange(ref _measureBox, boxed, null);
            return witnessed is null ? measure : (TMeasure)witnessed;
        }
    }

    /// <inheritdoc/>
    public override bool IsEmpty => false;

    /// <inheritdoc/>
    public override TChild First => Prefix[0];

    /// <inheritdoc/>
    public override TChild Last => Suffix[^1];

    /// <summary>Forces and returns the middle tree of nodes. O(1) when already computed.</summary>
    public MeasuredTree<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid> ForceMiddle() =>
        _middle.Force();

    /// <inheritdoc/>
    public override MeasuredTree<TElement, TChild, TMeasure, TMonoid> Cons(TChild value)
    {
        if (Prefix.Length < 4)
            return new DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>([value, .. Prefix], _middle, Suffix);

        // Overflow: force the current middle (keeping the new suspension one operation deep) and defer the push.
        var pushed = new MeasuredNode<TElement, TChild, TMeasure, TMonoid>([Prefix[1], Prefix[2], Prefix[3]]);
        var suspended = new MeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(
            new LazyMeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(
                new PendingMeasuredPushFront<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(ForceMiddle(), pushed)));
        return new DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>([value, Prefix[0]], suspended, Suffix);
    }

    /// <inheritdoc/>
    public override MeasuredTree<TElement, TChild, TMeasure, TMonoid> Snoc(TChild value)
    {
        if (Suffix.Length < 4)
            return new DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>(Prefix, _middle, [.. Suffix, value]);

        var pushed = new MeasuredNode<TElement, TChild, TMeasure, TMonoid>([Suffix[0], Suffix[1], Suffix[2]]);
        var suspended = new MeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(
            new LazyMeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(
                new PendingMeasuredPushBack<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(ForceMiddle(), pushed)));
        return new DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>(Prefix, suspended, [Suffix[3], value]);
    }

    /// <inheritdoc/>
    public override bool TryViewLeft(out TChild head, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest)
    {
        head = Prefix[0];
        if (Prefix.Length > 1)
        {
            rest = new DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>(Prefix[1..], _middle, Suffix);
            return true;
        }

        // Prefix exhausted: pull the head node up from the middle, deferring the pop so it is paid once even
        // under branching. The source is forced first, keeping the new suspension one operation deep.
        var middle = ForceMiddle();
        if (middle.IsEmpty)
        {
            rest = FromBuffer(Suffix);
            return true;
        }

        var node = middle.First;
        var popped = new MeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(
            new LazyMeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(
                new PendingMeasuredPopFront<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(middle)));
        rest = new DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>(node.Children, popped, Suffix);
        return true;
    }

    /// <inheritdoc/>
    public override bool TryViewRight(out TChild last, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest)
    {
        last = Suffix[^1];
        if (Suffix.Length > 1)
        {
            rest = new DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>(Prefix, _middle, Suffix[..^1]);
            return true;
        }

        var middle = ForceMiddle();
        if (middle.IsEmpty)
        {
            rest = FromBuffer(Prefix);
            return true;
        }

        var node = middle.Last;
        var popped = new MeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(
            new LazyMeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(
                new PendingMeasuredPopBack<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(middle)));
        rest = new DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>(Prefix, popped, node.Children);
        return true;
    }

    /// <inheritdoc/>
    public override void Flatten(ICollection<TElement> sink)
    {
        foreach (var child in Prefix)
            child.Flatten(sink);
        ForceMiddle().Flatten(sink);
        foreach (var child in Suffix)
            child.Flatten(sink);
    }

    /// <inheritdoc/>
    public override int ValidateAndCount()
    {
        if (Prefix.Length is < 1 or > 4)
            throw new InvalidOperationException($"Measured finger-tree invariant violated: a deep prefix has arity {Prefix.Length}.");
        if (Suffix.Length is < 1 or > 4)
            throw new InvalidOperationException($"Measured finger-tree invariant violated: a deep suffix has arity {Suffix.Length}.");

        var count = 0;
        var expected = TMonoid.Empty;
        foreach (var child in Prefix)
        {
            count += child.ValidateAndCount();
            expected = TMonoid.Combine(expected, child.Measure);
        }

        var middle = ForceMiddle();
        count += middle.ValidateAndCount();
        expected = TMonoid.Combine(expected, middle.Measure);

        foreach (var child in Suffix)
        {
            count += child.ValidateAndCount();
            expected = TMonoid.Combine(expected, child.Measure);
        }

        if (!EqualityComparer<TMeasure>.Default.Equals(expected, Measure))
            throw new InvalidOperationException("Measured finger-tree invariant violated: a deep tree's cached measure is stale.");

        return count;
    }

    /// <inheritdoc/>
    public override int ChildCount => Prefix.Length + 1 + Suffix.Length;

    /// <inheritdoc/>
    public override bool TryGetChild(int index, out TElement leaf, out IEnumerationBlock<TElement>? block)
    {
        if (index < Prefix.Length)
            return Prefix[index].TryGetLeaf(out leaf, out block);

        if (index == Prefix.Length)
        {
            leaf = default!;
            block = ForceMiddle();
            return false;
        }

        return Suffix[index - Prefix.Length - 1].TryGetLeaf(out leaf, out block);
    }

    /// <inheritdoc/>
    public override (MeasuredTree<TElement, TChild, TMeasure, TMonoid> Left, TChild Hit, MeasuredTree<TElement, TChild, TMeasure, TMonoid> Right)
        SplitTree<TPredicate>(TPredicate predicate, TMeasure accumulator)
    {
        var beforeMiddle = TMonoid.Combine(accumulator, CombineAll(Prefix));
        if (predicate.Invoke(beforeMiddle))
        {
            var (before, hit, after) = SplitBuffer(predicate, accumulator, Prefix);
            return (FromBuffer(before), hit, DeepLeft(after, ForceMiddle(), Suffix));
        }

        var middle = ForceMiddle();
        var afterMiddle = TMonoid.Combine(beforeMiddle, middle.Measure);
        if (predicate.Invoke(afterMiddle))
        {
            var (middleLeft, node, middleRight) = middle.SplitTree(predicate, beforeMiddle);
            var accBeforeNode = TMonoid.Combine(beforeMiddle, middleLeft.Measure);
            var (before, hit, after) = SplitBuffer(predicate, accBeforeNode, node.Children);
            return (DeepRight(Prefix, middleLeft, before), hit, DeepLeft(after, middleRight, Suffix));
        }

        var (suffixBefore, suffixHit, suffixAfter) = SplitBuffer(predicate, afterMiddle, Suffix);
        return (DeepRight(Prefix, middle, suffixBefore), suffixHit, FromBuffer(suffixAfter));
    }

    /// <inheritdoc/>
    public override (TMeasure MeasureBefore, TChild Hit) LocateTree<TPredicate>(TPredicate predicate, TMeasure accumulator)
    {
        var beforeMiddle = TMonoid.Combine(accumulator, CombineAll(Prefix));
        if (predicate.Invoke(beforeMiddle))
            return LocateBuffer(predicate, accumulator, Prefix);

        var middle = ForceMiddle();
        var afterMiddle = TMonoid.Combine(beforeMiddle, middle.Measure);
        if (predicate.Invoke(afterMiddle))
        {
            var (nodeBefore, node) = middle.LocateTree(predicate, beforeMiddle);
            return LocateBuffer(predicate, nodeBefore, node.Children);
        }

        return LocateBuffer(predicate, afterMiddle, Suffix);
    }
}
