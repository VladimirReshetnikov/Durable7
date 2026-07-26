// A monotone predicate over a measure, supplied as a value type so a measured-tree read (<see
// cref="FingerTree{TElement, TMeasure, TMeasureOps}.TryLocate{TPredicate}(TPredicate, out TMeasure,
// out TElement)"/>) can descend to a boundary element without allocating a closure.

namespace Durable7.FingerTree;

/// <summary>
/// A monotone predicate over a measure, supplied as a value type so a measured-tree read
/// (<see cref="FingerTree{TElement, TMeasure, TMeasureOps}.TryLocate{TPredicate}(TPredicate, out TMeasure, out TElement)"/>)
/// can descend to a boundary element without allocating a closure. Implement it on a <see langword="struct"/>;
/// the generic constraint <c>where TPredicate : struct, IMeasurePredicate&lt;TMeasure&gt;</c> lets the runtime
/// monomorphize and devirtualize <see cref="Invoke"/>, so a hot membership, rank, order-statistic, or neighbor
/// query allocates nothing at all.
/// </summary>
/// <typeparam name="TMeasure">The measure (annotation) type the predicate inspects.</typeparam>
/// <remarks>
/// This is the value-type counterpart to the <see cref="Func{TMeasure, Boolean}"/> accepted by
/// <see cref="FingerTree{TElement, TMeasure, TMeasureOps}.Split(Func{TMeasure, bool})"/> and
/// <see cref="FingerTree{TElement, TMeasure, TMeasureOps}.TryLocate(Func{TMeasure, bool}, out TMeasure, out TElement)"/>:
/// the delegate form is convenient but allocates a closure when it captures, whereas a non-capturing struct
/// predicate captures its inputs in fields and allocates nothing. The predicate must be monotone over the
/// accumulated measure — once <see cref="Invoke"/> returns <see langword="true"/> for some prefix it must stay
/// <see langword="true"/> for every longer prefix — exactly as for the delegate-based split predicates.
/// </remarks>
/// <example>
/// A zero-allocation "first element whose running total reaches a threshold" query over a sum-measured tree:
/// <code>
/// readonly struct AtLeast(int threshold) : IMeasurePredicate&lt;int&gt;
/// {
///     public bool Invoke(int runningTotal) =&gt; runningTotal &gt;= threshold;
/// }
///
/// var tree = FingerTree&lt;int, int, SumMeasure&lt;int&gt;&gt;.Create(5, 1, 4, 2);
/// if (tree.TryLocate(new AtLeast(7), out var before, out var element))
/// {
///     // element is the first whose inclusion makes the running total reach 7 (here 4),
///     // and before is the summed measure of everything to its left (here 6). No allocation.
/// }
/// </code>
/// </example>
public interface IMeasurePredicate<TMeasure>
{
    /// <summary>Evaluates the predicate for the accumulated measure of a candidate prefix.</summary>
    /// <param name="measure">The accumulated measure to test.</param>
    /// <returns><see langword="true"/> once the boundary has been reached or passed; otherwise
    /// <see langword="false"/>.</returns>
    bool Invoke(TMeasure measure);
}
