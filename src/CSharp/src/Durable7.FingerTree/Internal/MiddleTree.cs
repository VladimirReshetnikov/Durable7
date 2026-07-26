// The middle subtree of a deep node: either an already-computed tree or a shared memoized
// suspension.

namespace Durable7.FingerTree;

/// <summary>
/// The middle subtree of a deep node: either an already-computed tree or a shared memoized suspension.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the middle tree itself (one level below the owning deep node).</typeparam>
/// <remarks>
/// <para>
/// This realizes the original finger-tree paper's strict-language strategy: "we need only suspend the middle
/// subtree of each Deep node, so only Θ(log n) suspensions are required in a tree of size n." Suspending the
/// middle is what makes the amortized bounds hold under fully persistent (branching) use: deep spine repairs
/// are deferred until an operation actually descends that far, and memoization shares the forced result
/// among every version holding the same suspension.
/// </para>
/// <para>
/// A single-field readonly struct, so storing a computed middle costs no allocation beyond the tree itself.
/// </para>
/// </remarks>
internal readonly struct MiddleTree<T, TChild> where TChild : ITreeElement<T, TChild>
{
    /// <summary>Either a computed <see cref="Tree{T, TChild}"/> or a <see cref="LazyMiddle{T, TChild}"/> suspension.</summary>
    private readonly object _state;

    /// <summary>Wraps an already-computed middle tree.</summary>
    /// <param name="computed">The computed tree.</param>
    public MiddleTree(Tree<T, TChild> computed) => _state = computed;

    /// <summary>Wraps a memoized suspension.</summary>
    /// <param name="suspension">The shared suspension cell.</param>
    public MiddleTree(LazyMiddle<T, TChild> suspension) => _state = suspension;

    /// <summary>Gets a computed empty middle.</summary>
    public static MiddleTree<T, TChild> Empty => new(EmptyTree<T, TChild>.Instance);

    /// <summary>
    /// Returns the middle tree, running and memoizing the pending operation on first use.
    /// </summary>
    /// <remarks>
    /// O(1) when already computed; otherwise the cost of the pending operation, which may itself force a
    /// chain of older suspensions. Each suspension's result is computed once per cell in quiescent use —
    /// racing forcers may duplicate the bounded computation but all converge on the single published tree —
    /// which is what the persistent amortized analysis charges for.
    /// </remarks>
    public Tree<T, TChild> Force() =>
        _state is Tree<T, TChild> computed ? computed : ((LazyMiddle<T, TChild>)_state).Force();
}

/// <summary>
/// A shared, thread-safe, memoize-on-first-force suspension of a middle tree.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the suspended middle tree.</typeparam>
/// <remarks>
/// Forcing publishes the result with a compare-exchange; concurrent forcers may duplicate the bounded
/// pending computation, but all of them converge on the single published tree, so no reader can observe a
/// partially initialized node or two different element sequences. After publication the pending-operation
/// object (and the older structure it captured) becomes unreachable through this cell.
/// </remarks>
internal sealed class LazyMiddle<T, TChild> where TChild : ITreeElement<T, TChild>
{
    /// <summary>Either the pending operation (before forcing) or the computed tree (after forcing).</summary>
    private object _state;

    /// <summary>Initializes the suspension with its pending operation.</summary>
    /// <param name="pending">The deferred middle-tree computation.</param>
    public LazyMiddle(PendingMiddle<T, TChild> pending) => _state = pending;

    /// <summary>Returns the suspended tree, computing and publishing it on first use.</summary>
    public Tree<T, TChild> Force()
    {
        var state = Volatile.Read(ref _state);
        if (state is Tree<T, TChild> computed)
            return computed;

        var result = ((PendingMiddle<T, TChild>)state).Run();
        var witnessed = Interlocked.CompareExchange(ref _state, result, state);
        return ReferenceEquals(witnessed, state) ? result : (Tree<T, TChild>)witnessed;
    }
}

/// <summary>
/// A deferred middle-tree computation held by a <see cref="LazyMiddle{T, TChild}"/> cell.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the resulting middle tree.</typeparam>
/// <remarks>
/// Pending operations are immutable and pure: running one twice produces structurally identical trees, which
/// is what makes the benign forcing race of <see cref="LazyMiddle{T, TChild}"/> safe.
/// </remarks>
internal abstract class PendingMiddle<T, TChild> where TChild : ITreeElement<T, TChild>
{
    /// <summary>Runs the deferred computation. O(1) own work plus any forcing of captured suspensions.</summary>
    public abstract Tree<T, TChild> Run();
}

/// <summary>
/// Deferred push of one element onto the front of a computed middle tree, created by an endpoint-insertion
/// overflow.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the middle tree.</typeparam>
/// <param name="source">The computed middle the owning deep node had before the overflow.</param>
/// <param name="element">The node pushed down by the overflow.</param>
/// <remarks>
/// The source is forced by the creator, never captured as a nested suspension. This is the original paper's
/// "forcing the middle subtree m in the recursive case of cons to avoid building a chain of suspensions";
/// it keeps every suspension exactly one deferred operation deep, bounding the forcing recursion by the
/// tree depth while leaving the amortized analysis unaffected.
/// </remarks>
internal sealed class PendingPushFront<T, TChild>(Tree<T, TChild> source, TChild element)
    : PendingMiddle<T, TChild>
    where TChild : ITreeElement<T, TChild>
{
    /// <inheritdoc/>
    public override Tree<T, TChild> Run() => source.Cons(element);
}

/// <summary>
/// Deferred push of one element onto the back of a computed middle tree, created by an endpoint-insertion
/// overflow.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the middle tree.</typeparam>
/// <param name="source">The computed middle the owning deep node had before the overflow.</param>
/// <param name="element">The node pushed down by the overflow.</param>
/// <remarks>
/// The source is forced by the creator, never captured as a nested suspension; see
/// <see cref="PendingPushFront{T, TChild}"/> for why.
/// </remarks>
internal sealed class PendingPushBack<T, TChild>(Tree<T, TChild> source, TChild element)
    : PendingMiddle<T, TChild>
    where TChild : ITreeElement<T, TChild>
{
    /// <inheritdoc/>
    public override Tree<T, TChild> Run() => source.Snoc(element);
}

/// <summary>
/// Deferred removal of the first element of a computed middle tree, created when an endpoint removal or a
/// split reconstruction pulls a <see cref="Node2{T, TChild}"/> head out of the middle.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the middle tree.</typeparam>
/// <param name="source">The computed middle whose head node was exposed at the level above.</param>
internal sealed class PendingPopFront<T, TChild>(Tree<T, TChild> source) : PendingMiddle<T, TChild>
    where TChild : ITreeElement<T, TChild>
{
    /// <inheritdoc/>
    public override Tree<T, TChild> Run() => source.RemoveFirst(out _);
}

/// <summary>
/// Deferred removal of the last element of a computed middle tree, created when an endpoint removal or a
/// split reconstruction pulls a <see cref="Node2{T, TChild}"/> tail out of the middle.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the middle tree.</typeparam>
/// <param name="source">The computed middle whose last node was exposed at the level above.</param>
internal sealed class PendingPopBack<T, TChild>(Tree<T, TChild> source) : PendingMiddle<T, TChild>
    where TChild : ITreeElement<T, TChild>
{
    /// <inheritdoc/>
    public override Tree<T, TChild> Run() => source.RemoveLast(out _);
}
