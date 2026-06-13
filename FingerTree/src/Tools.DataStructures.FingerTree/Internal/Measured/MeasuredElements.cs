namespace Tools.DataStructures.FingerTree;

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
internal sealed class MeasuredNode<TElement, TChild, TMeasure, TMonoid> : IMeasuredElement<TElement, TMeasure>
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
}
