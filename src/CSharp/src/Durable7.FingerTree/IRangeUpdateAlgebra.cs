namespace Durable7.FingerTree;

/// <summary>
/// Defines an ordered sequence measure together with a monoid of lazy range-update tags acting on
/// both individual elements and cached subtree measures.
/// </summary>
/// <typeparam name="TElement">The sequence element type.</typeparam>
/// <typeparam name="TMeasure">The cached ordered-measure type.</typeparam>
/// <typeparam name="TTag">The range-update tag type.</typeparam>
/// <remarks>
/// <para>
/// Implementations provide the algebra used by
/// <see cref="RangeUpdateSequence{TElement,TMeasure,TTag,TOps}"/>. In addition to the monoid laws
/// inherited from <see cref="IMeasure{TElement,TMeasure}"/>, tags must form a monoid under
/// <see cref="Compose"/> and must act consistently on elements and measures.
/// </para>
/// <para>
/// <see cref="Compose"/> has an intentionally directional contract:
/// <c>Compose(newer, older)</c> represents applying <c>older</c> first and <c>newer</c> second.
/// For all tags <c>p</c>, <c>q</c>, and <c>r</c>, elements <c>x</c>, measures <c>m</c>, and valid
/// subtree counts <c>n</c>, implementations must obey:
/// </para>
/// <list type="bullet">
/// <item><description><c>IsIdentity(IdentityTag)</c> is <see langword="true"/>.</description></item>
/// <item><description><c>Compose(IdentityTag, p) = p = Compose(p, IdentityTag)</c>.</description></item>
/// <item><description><c>ApplyElement(IdentityTag, x) = x</c>.</description></item>
/// <item><description><c>ApplyMeasure(IdentityTag, m, n) = m</c>.</description></item>
/// <item><description>
/// <c>Compose(r, Compose(q, p)) = Compose(Compose(r, q), p)</c>.
/// </description></item>
/// <item><description>
/// <c>ApplyElement(Compose(q, p), x) = ApplyElement(q, ApplyElement(p, x))</c>.
/// </description></item>
/// <item><description>
/// <c>ApplyMeasure(Compose(q, p), m, n) = ApplyMeasure(q, ApplyMeasure(p, m, n), n)</c>.
/// </description></item>
/// <item><description>
/// Applying a tag to a singleton measure agrees with measuring the tagged element.
/// </description></item>
/// <item><description>
/// Tag action distributes over ordered <see cref="IMonoid{TMeasure}.Combine"/> when the left and
/// right subtree counts are supplied, including noncommutative measures.
/// </description></item>
/// <item><description>
/// Applying any tag to <see cref="IMonoid{TMeasure}.Empty"/> with count zero returns that empty
/// measure.
/// </description></item>
/// </list>
/// <para>
/// Every tag for which <see cref="IsIdentity"/> returns <see langword="true"/> must obey the same
/// element, measure, and composition equations as <see cref="IdentityTag"/>, even when it is a
/// value-distinct representation. The collection relies on this stronger recognition contract for
/// no-op identity and for canonical removal of composed identity tags.
/// </para>
/// <para>
/// Operations are expected to be deterministic, side-effect-free, and safe for the invocation
/// pattern chosen by the caller. The collection cannot prove these algebraic or purity laws;
/// violating them produces unspecified logical results.
/// </para>
/// </remarks>
public interface IRangeUpdateAlgebra<TElement, TMeasure, TTag>
    : IMeasure<TElement, TMeasure>
{
    /// <summary>Gets the identity of the range-update tag monoid.</summary>
    /// <remarks>
    /// This value is an algebraic identity, not a storage sentinel. The collection represents the
    /// absence of a pending tag separately and does not require <typeparamref name="TTag"/> equality.
    /// </remarks>
    static abstract TTag IdentityTag { get; }

    /// <summary>Determines whether a tag is algebraically equivalent to the identity tag.</summary>
    /// <param name="tag">The tag to inspect.</param>
    /// <returns>
    /// <see langword="true"/> when applying or composing <paramref name="tag"/> has identity
    /// behavior; otherwise, <see langword="false"/>.
    /// </returns>
    static abstract bool IsIdentity(TTag tag);

    /// <summary>Composes two range-update tags in application order.</summary>
    /// <param name="newer">The tag applied second.</param>
    /// <param name="older">The tag applied first.</param>
    /// <returns>A tag equivalent to applying <paramref name="older"/> and then <paramref name="newer"/>.</returns>
    static abstract TTag Compose(TTag newer, TTag older);

    /// <summary>Applies a tag to one element.</summary>
    /// <param name="tag">The tag to apply.</param>
    /// <param name="element">The element's current logical value.</param>
    /// <returns>The element after applying <paramref name="tag"/>.</returns>
    static abstract TElement ApplyElement(TTag tag, TElement element);

    /// <summary>Applies a tag to a cached measure without enumerating the measured elements.</summary>
    /// <param name="tag">The tag to apply.</param>
    /// <param name="measure">The current logical measure.</param>
    /// <param name="count">The number of elements represented by <paramref name="measure"/>.</param>
    /// <returns>The measure after applying <paramref name="tag"/> to all <paramref name="count"/> elements.</returns>
    static abstract TMeasure ApplyMeasure(TTag tag, TMeasure measure, int count);
}
