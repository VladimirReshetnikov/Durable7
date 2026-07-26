// The monoid and measure contracts every measured structure is parameterized on. A measure must be
// associative with a two-sided identity, because the tree combines cached measures in whatever
// grouping its shape happens to have.

namespace Durable7.FingerTree;

/// <summary>
/// A monoid over <typeparamref name="TMeasure"/>: an associative <see cref="Combine"/> with identity
/// <see cref="Empty"/>. Supplied as a type argument so the measure algebra is resolved statically, with no
/// per-call allocation or virtual dispatch — the C# encoding of a Haskell <c>Monoid</c> instance.
/// </summary>
/// <typeparam name="TMeasure">The measure (annotation) type.</typeparam>
/// <remarks>
/// <para>Implementations must satisfy the monoid laws for every value:</para>
/// <list type="bullet">
/// <item><description>identity — <c>Combine(Empty, x)</c> equals <c>x</c> equals <c>Combine(x, Empty)</c>;</description></item>
/// <item><description>associativity — <c>Combine(Combine(a, b), c)</c> equals <c>Combine(a, Combine(b, c))</c>.</description></item>
/// </list>
/// <para>
/// A measured <see cref="FingerTree{TElement, TMeasure, TMeasureOps}"/> caches combined measures throughout
/// its structure and relies on these laws; violating them yields unspecified results.
/// </para>
/// </remarks>
public interface IMonoid<TMeasure>
{
    /// <summary>Gets the identity element of <see cref="Combine"/>.</summary>
    static abstract TMeasure Empty { get; }

    /// <summary>Combines two measures. Must be associative with identity <see cref="Empty"/>.</summary>
    /// <param name="left">Left measure.</param>
    /// <param name="right">Right measure.</param>
    /// <returns>The combined measure of <paramref name="left"/> followed by <paramref name="right"/>.</returns>
    static abstract TMeasure Combine(TMeasure left, TMeasure right);
}

/// <summary>
/// Assigns a monoidal <typeparamref name="TMeasure"/> to each <typeparamref name="TElement"/>, making the
/// element type storable in a measured
/// <see cref="FingerTree{TElement, TMeasure, TMeasureOps}"/>.
/// </summary>
/// <typeparam name="TElement">Stored element type.</typeparam>
/// <typeparam name="TMeasure">Measure (annotation) type.</typeparam>
/// <remarks>
/// The measure of a sequence is the <see cref="IMonoid{TMeasure}.Combine"/> of its elements' measures in
/// order. Choosing the measure is what specializes the tree: an element-count measure makes it a positional
/// sequence (<see cref="SizeMeasure{TElement}"/>); a maximum-priority measure makes it a mergeable priority
/// queue; a maximum-key measure makes it an ordered search tree; a count-plus-key product measure makes it
/// an order-statistic tree.
/// </remarks>
/// <example>
/// A maximum-priority measure turning the tree into a mergeable priority queue:
/// <code>
/// public readonly struct MaxPriority : IMeasure&lt;int, int&gt;
/// {
///     public static int Empty =&gt; int.MinValue;          // identity for Max
///     public static int Combine(int a, int b) =&gt; Math.Max(a, b);
///     public static int Measure(int element) =&gt; element;
/// }
/// // In FingerTree&lt;int, int, MaxPriority&gt;, Measure is the maximum element; the call
/// //   tree.TrySplitFind(m =&gt; m &gt;= tree.Measure, out var lo, out var max, out var hi)
/// // isolates an occurrence of that maximum (max), with lo and hi the elements around it.
/// </code>
/// </example>
public interface IMeasure<TElement, TMeasure> : IMonoid<TMeasure>
{
    /// <summary>Returns the measure of a single element.</summary>
    /// <param name="element">Element to measure.</param>
    /// <returns>The element's measure.</returns>
    static abstract TMeasure Measure(TElement element);
}

/// <summary>
/// The element-count measure: every element measures <c>1</c> and measures add, so a tree's measure is its
/// length and it behaves as a positional sequence. Because the accumulated measure of a prefix is its
/// length, <c>Split(accumulated =&gt; accumulated &gt; index)</c> places exactly the first <c>index</c>
/// elements in the left half; the element at zero-based <c>index</c> begins the right half.
/// </summary>
/// <typeparam name="TElement">Stored element type.</typeparam>
public readonly struct SizeMeasure<TElement> : IMeasure<TElement, int>
{
    /// <inheritdoc/>
    public static int Empty => 0;

    /// <inheritdoc/>
    public static int Combine(int left, int right) => left + right;

    /// <inheritdoc/>
    public static int Measure(TElement element) => 1;
}
