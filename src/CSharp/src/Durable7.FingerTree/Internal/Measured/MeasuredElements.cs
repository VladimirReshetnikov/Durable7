// The measured tree's elements: leaves holding values and nodes caching their children's combined
// measure.

namespace Durable7.FingerTree;

/// <summary>
/// A measured element at one level of a <see cref="MeasuredTree{TElement, TChild, TMeasure, TMonoid}"/>:
/// either an <see cref="MeasuredLeaf{TElement, TMeasure, TMeasureOps}"/> wrapping a stored element at the
/// outermost level, or a <see cref="MeasuredNode{TElement, TChild, TMeasure, TMonoid}"/> grouping children
/// at every deeper level.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type, threaded through all levels so the tree can flatten.</typeparam>
/// <typeparam name="TMeasure">Measure (annotation) type.</typeparam>
internal interface IMeasuredElement<TElement, TMeasure>
{
    /// <summary>Gets the cached measure of this element. O(1).</summary>
    TMeasure Measure { get; }

    /// <summary>Appends every stored leaf element beneath this element, in order, to <paramref name="sink"/>.</summary>
    /// <param name="sink">Destination collection.</param>
    void Flatten(ICollection<TElement> sink);

    /// <summary>
    /// Classifies this element as either a stored leaf value or a nested enumeration block.
    /// </summary>
    /// <param name="leaf">The stored leaf value when this element is a leaf; otherwise <see langword="default"/>.</param>
    /// <param name="block">The nested block when this element groups children; otherwise <see langword="null"/>.</param>
    /// <returns><see langword="true"/> when this element is a leaf; otherwise <see langword="false"/>.</returns>
    bool TryGetLeaf(out TElement leaf, out IEnumerationBlock<TElement>? block);

    /// <summary>Validates this element's cached metadata and returns its stored leaf count.</summary>
    int ValidateAndCount();
}

/// <summary>
/// Wraps a stored element at the outermost tree level, caching its measure computed once via
/// <typeparamref name="TMeasureOps"/>.
/// </summary>
/// <typeparam name="TElement">Stored element type.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMeasureOps">Static measure provider.</typeparam>
/// <param name="value">The stored element.</param>
internal readonly struct MeasuredLeaf<TElement, TMeasure, TMeasureOps>(TElement value)
    : IMeasuredElement<TElement, TMeasure>
    where TMeasureOps : IMeasure<TElement, TMeasure>
{
    /// <summary>Gets the stored element.</summary>
    public TElement Value { get; } = value;

    /// <inheritdoc/>
    public TMeasure Measure { get; } = TMeasureOps.Measure(value);

    /// <inheritdoc/>
    public void Flatten(ICollection<TElement> sink) => sink.Add(Value);

    /// <inheritdoc/>
    public bool TryGetLeaf(out TElement leaf, out IEnumerationBlock<TElement>? block)
    {
        leaf = Value;
        block = null;
        return true;
    }

    /// <inheritdoc/>
    public int ValidateAndCount()
    {
        var expected = TMeasureOps.Measure(Value);
        if (!EqualityComparer<TMeasure>.Default.Equals(expected, Measure))
            throw new InvalidOperationException("Measured finger-tree invariant violated: a leaf's cached measure is stale.");
        return 1;
    }
}

/// <summary>
/// A two- or three-child grouping node one level closer to the leaves, with a cached combined measure.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TChild">Element type of the children.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider for combining child measures.</typeparam>
/// <remarks>
/// A node is itself an <see cref="IMeasuredElement{TElement, TMeasure}"/>, so the middle subtree storing
/// nodes is just another <see cref="MeasuredTree{TElement, TChild, TMeasure, TMonoid}"/> one level down
/// (polymorphic recursion).
/// </remarks>
internal sealed class MeasuredNode<TElement, TChild, TMeasure, TMonoid>
    : IMeasuredElement<TElement, TMeasure>, IEnumerationBlock<TElement>
    where TChild : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <summary>The two or three children, in order.</summary>
    public readonly TChild[] Children;

    /// <summary>Creates a node from two or three children, caching the combined measure.</summary>
    /// <param name="children">Two or three children.</param>
    public MeasuredNode(TChild[] children)
    {
        Children = children;
        var measure = children[0].Measure;
        for (var i = 1; i < children.Length; i++)
            measure = TMonoid.Combine(measure, children[i].Measure);
        Measure = measure;
    }

    /// <inheritdoc/>
    public TMeasure Measure { get; }

    /// <inheritdoc/>
    public void Flatten(ICollection<TElement> sink)
    {
        foreach (var child in Children)
            child.Flatten(sink);
    }

    /// <inheritdoc/>
    public bool TryGetLeaf(out TElement leaf, out IEnumerationBlock<TElement>? block)
    {
        leaf = default!;
        block = this;
        return false;
    }

    /// <inheritdoc/>
    public int ChildCount => Children.Length;

    /// <inheritdoc/>
    public bool TryGetChild(int index, out TElement leaf, out IEnumerationBlock<TElement>? block) =>
        Children[index].TryGetLeaf(out leaf, out block);

    /// <inheritdoc/>
    public int ValidateAndCount()
    {
        if (Children.Length is < 2 or > 3)
            throw new InvalidOperationException($"Measured finger-tree invariant violated: a node has arity {Children.Length}.");

        var count = 0;
        var measure = Children[0].Measure;
        for (var i = 0; i < Children.Length; i++)
        {
            var child = Children[i];
            count += child.ValidateAndCount();
            if (i > 0)
                measure = TMonoid.Combine(measure, child.Measure);
        }

        if (!EqualityComparer<TMeasure>.Default.Equals(measure, Measure))
            throw new InvalidOperationException("Measured finger-tree invariant violated: a node's cached measure is stale.");

        return count;
    }
}
