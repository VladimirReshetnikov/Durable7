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
    public override (MeasuredTree<TElement, TChild, TMeasure, TMonoid> Left, TChild Hit, MeasuredTree<TElement, TChild, TMeasure, TMonoid> Right)
        SplitTree(Func<TMeasure, bool> predicate, TMeasure accumulator) =>
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
    public override (MeasuredTree<TElement, TChild, TMeasure, TMonoid> Left, TChild Hit, MeasuredTree<TElement, TChild, TMeasure, TMonoid> Right)
        SplitTree(Func<TMeasure, bool> predicate, TMeasure accumulator) =>
        (Empty, Element, Empty);
}

/// <summary>A deep level of a measured finger tree, with a cached combined measure.</summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TChild">Element type at this level.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
internal sealed class DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>
    : MeasuredTree<TElement, TChild, TMeasure, TMonoid>
    where TChild : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <summary>Prefix buffer; one through four elements.</summary>
    public readonly TChild[] Prefix;

    /// <summary>Middle tree of grouping nodes, one level closer to the leaves.</summary>
    public readonly MeasuredTree<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid> Middle;

    /// <summary>Suffix buffer; one through four elements.</summary>
    public readonly TChild[] Suffix;

    /// <summary>Builds a deep level and caches its combined measure in O(1).</summary>
    /// <param name="prefix">One through four prefix elements.</param>
    /// <param name="middle">Middle tree of nodes.</param>
    /// <param name="suffix">One through four suffix elements.</param>
    public DeepMeasuredTree(
        TChild[] prefix,
        MeasuredTree<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid> middle,
        TChild[] suffix)
    {
        Prefix = prefix;
        Middle = middle;
        Suffix = suffix;
        Measure = TMonoid.Combine(TMonoid.Combine(CombineAll(prefix), middle.Measure), CombineAll(suffix));
    }

    /// <inheritdoc/>
    public override TMeasure Measure { get; }

    /// <inheritdoc/>
    public override bool IsEmpty => false;

    /// <inheritdoc/>
    public override TChild First => Prefix[0];

    /// <inheritdoc/>
    public override TChild Last => Suffix[^1];

    /// <inheritdoc/>
    public override MeasuredTree<TElement, TChild, TMeasure, TMonoid> Cons(TChild value)
    {
        if (Prefix.Length < 4)
            return Deep([value, .. Prefix], Middle, Suffix);

        var pushed = new MeasuredNode<TElement, TChild, TMeasure, TMonoid>([Prefix[1], Prefix[2], Prefix[3]]);
        return Deep([value, Prefix[0]], Middle.Cons(pushed), Suffix);
    }

    /// <inheritdoc/>
    public override MeasuredTree<TElement, TChild, TMeasure, TMonoid> Snoc(TChild value)
    {
        if (Suffix.Length < 4)
            return Deep(Prefix, Middle, [.. Suffix, value]);

        var pushed = new MeasuredNode<TElement, TChild, TMeasure, TMonoid>([Suffix[0], Suffix[1], Suffix[2]]);
        return Deep(Prefix, Middle.Snoc(pushed), [Suffix[3], value]);
    }

    /// <inheritdoc/>
    public override bool TryViewLeft(out TChild head, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest)
    {
        head = Prefix[0];
        rest = Prefix.Length > 1
            ? Deep(Prefix[1..], Middle, Suffix)
            : DeepLeft([], Middle, Suffix);
        return true;
    }

    /// <inheritdoc/>
    public override bool TryViewRight(out TChild last, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest)
    {
        last = Suffix[^1];
        rest = Suffix.Length > 1
            ? Deep(Prefix, Middle, Suffix[..^1])
            : DeepRight(Prefix, Middle, []);
        return true;
    }

    /// <inheritdoc/>
    public override void Flatten(ICollection<TElement> sink)
    {
        foreach (var child in Prefix)
            child.Flatten(sink);
        Middle.Flatten(sink);
        foreach (var child in Suffix)
            child.Flatten(sink);
    }

    /// <inheritdoc/>
    public override (MeasuredTree<TElement, TChild, TMeasure, TMonoid> Left, TChild Hit, MeasuredTree<TElement, TChild, TMeasure, TMonoid> Right)
        SplitTree(Func<TMeasure, bool> predicate, TMeasure accumulator)
    {
        var beforeMiddle = TMonoid.Combine(accumulator, CombineAll(Prefix));
        if (predicate(beforeMiddle))
        {
            var (before, hit, after) = SplitBuffer(predicate, accumulator, Prefix);
            return (FromBuffer(before), hit, DeepLeft(after, Middle, Suffix));
        }

        var afterMiddle = TMonoid.Combine(beforeMiddle, Middle.Measure);
        if (predicate(afterMiddle))
        {
            var (middleLeft, node, middleRight) = Middle.SplitTree(predicate, beforeMiddle);
            var accBeforeNode = TMonoid.Combine(beforeMiddle, middleLeft.Measure);
            var (before, hit, after) = SplitBuffer(predicate, accBeforeNode, node.Children);
            return (DeepRight(Prefix, middleLeft, before), hit, DeepLeft(after, middleRight, Suffix));
        }

        var (suffixBefore, suffixHit, suffixAfter) = SplitBuffer(predicate, afterMiddle, Suffix);
        return (DeepRight(Prefix, Middle, suffixBefore), suffixHit, FromBuffer(suffixAfter));
    }
}
