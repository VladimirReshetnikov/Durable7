namespace Tools.DataStructures.FingerTree;

/// <summary>
/// The middle subtree of a measured deep node: either an already-computed tree of grouping nodes or a shared
/// memoized suspension of a pending spine repair.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type, threaded through every level for flattening.</typeparam>
/// <typeparam name="TNode">Element type of the middle tree itself — a grouping node one level below the owning
/// deep node.</typeparam>
/// <typeparam name="TMeasure">Measure (annotation) type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider used to combine measures.</typeparam>
/// <remarks>
/// <para>
/// This realizes Hinze and Paterson's strict-language strategy for the <em>general</em> measured tree: only the
/// middle subtree of each deep node is suspended, so a tree of size n carries only Θ(log n) suspensions.
/// Deferring the deep spine repair until an operation actually descends that far, and memoizing the forced
/// result, is what makes the amortized bounds hold under fully persistent (branching) use — repeatedly
/// operating on a retained version pays each suspension's cost once, not once per branch.
/// </para>
/// <para>
/// Unlike <see cref="FingerTreeDeque{T}"/>'s size-measured middle, this carries no group-subtraction shortcut:
/// the measure of a pop suspension cannot be recovered without forcing it (a general monoid has no inverse), so
/// the owning deep node's measure is itself lazily memoized (see <see cref="PendingMeasured{TElement, TNode,
/// TMeasure, TMonoid}.TryMeasureWithoutForcing"/>). A push suspension's measure, by contrast, is the monoidal
/// sum of its forced source and the pushed node, computable without forcing the structure.
/// </para>
/// <para>A single-field readonly struct, so storing a computed middle costs no allocation beyond the tree.</para>
/// </remarks>
internal readonly struct MeasuredMiddle<TElement, TNode, TMeasure, TMonoid>
    where TNode : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <summary>Either a computed <see cref="MeasuredTree{TElement, TNode, TMeasure, TMonoid}"/> or a suspension.</summary>
    private readonly object _state;

    /// <summary>Wraps an already-computed middle tree.</summary>
    /// <param name="computed">The computed tree of nodes.</param>
    public MeasuredMiddle(MeasuredTree<TElement, TNode, TMeasure, TMonoid> computed) => _state = computed;

    /// <summary>Wraps a memoized suspension.</summary>
    /// <param name="suspension">The shared suspension cell.</param>
    public MeasuredMiddle(LazyMeasuredMiddle<TElement, TNode, TMeasure, TMonoid> suspension) => _state = suspension;

    /// <summary>Gets a computed empty middle.</summary>
    public static MeasuredMiddle<TElement, TNode, TMeasure, TMonoid> Empty =>
        new(EmptyMeasuredTree<TElement, TNode, TMeasure, TMonoid>.Instance);

    /// <summary>
    /// Returns the middle tree, running and memoizing the pending operation on first use. O(1) when already
    /// computed; otherwise the cost of the pending operation, which may force a bounded chain of older
    /// suspensions down the spine.
    /// </summary>
    public MeasuredTree<TElement, TNode, TMeasure, TMonoid> Force() =>
        _state is MeasuredTree<TElement, TNode, TMeasure, TMonoid> computed
            ? computed
            : ((LazyMeasuredMiddle<TElement, TNode, TMeasure, TMonoid>)_state).Force();

    /// <summary>
    /// Returns the middle's measure, forcing the suspension only when it is a pop (whose measure a general
    /// monoid cannot recover by subtraction). A computed middle or a push suspension answers without forcing
    /// the structure.
    /// </summary>
    public TMeasure Measure
    {
        get
        {
            if (_state is MeasuredTree<TElement, TNode, TMeasure, TMonoid> computed)
                return computed.Measure;

            var cell = (LazyMeasuredMiddle<TElement, TNode, TMeasure, TMonoid>)_state;
            return cell.Measure;
        }
    }
}

/// <summary>
/// A shared, thread-safe, memoize-on-first-force suspension of a measured middle tree.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TNode">Element type of the suspended middle tree.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
/// <remarks>
/// Forcing publishes the result with a compare-exchange; concurrent forcers may duplicate the bounded pending
/// computation, but all converge on the single published tree, so no reader observes a partially initialized
/// node or two different element sequences. After publication the pending-operation object becomes unreachable
/// through this cell.
/// </remarks>
internal sealed class LazyMeasuredMiddle<TElement, TNode, TMeasure, TMonoid>
    where TNode : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <summary>Either the pending operation (before forcing) or the computed tree (after forcing).</summary>
    private object _state;

    /// <summary>Initializes the suspension with its pending operation.</summary>
    /// <param name="pending">The deferred middle-tree computation.</param>
    public LazyMeasuredMiddle(PendingMeasured<TElement, TNode, TMeasure, TMonoid> pending) => _state = pending;

    /// <summary>Returns the suspended tree, computing and publishing it on first use.</summary>
    public MeasuredTree<TElement, TNode, TMeasure, TMonoid> Force()
    {
        var state = Volatile.Read(ref _state);
        if (state is MeasuredTree<TElement, TNode, TMeasure, TMonoid> computed)
            return computed;

        RopeCursorDiagnostics.RecordForcedSuspension();
        var result = ((PendingMeasured<TElement, TNode, TMeasure, TMonoid>)state).Run();
        var witnessed = Interlocked.CompareExchange(ref _state, result, state);
        return ReferenceEquals(witnessed, state) ? result : (MeasuredTree<TElement, TNode, TMeasure, TMonoid>)witnessed;
    }

    /// <summary>
    /// Returns the suspended middle's measure. A push pending answers in O(1) from its forced source and pushed
    /// node without forcing the structure; a pop pending forces (and memoizes) the structure, then reads its
    /// measure.
    /// </summary>
    public TMeasure Measure
    {
        get
        {
            var state = Volatile.Read(ref _state);
            if (state is MeasuredTree<TElement, TNode, TMeasure, TMonoid> computed)
                return computed.Measure;

            return ((PendingMeasured<TElement, TNode, TMeasure, TMonoid>)state).TryMeasureWithoutForcing(out var measure)
                ? measure
                : Force().Measure;
        }
    }
}

/// <summary>
/// A deferred measured-middle computation held by a <see cref="LazyMeasuredMiddle{TElement, TNode, TMeasure,
/// TMonoid}"/> cell. Pending operations are immutable and pure: running one twice produces structurally
/// identical trees, which is what makes the benign forcing race safe.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TNode">Element type of the resulting middle tree.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
internal abstract class PendingMeasured<TElement, TNode, TMeasure, TMonoid>
    where TNode : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <summary>Runs the deferred computation. O(1) own work plus any forcing of captured suspensions.</summary>
    public abstract MeasuredTree<TElement, TNode, TMeasure, TMonoid> Run();

    /// <summary>
    /// Attempts to return the resulting middle's measure without forcing the structure. Returns
    /// <see langword="true"/> for push operations (the monoidal sum of the forced source and the pushed node);
    /// <see langword="false"/> for pop operations, whose measure a general monoid cannot recover by subtraction.
    /// </summary>
    /// <param name="measure">The computed measure when available; otherwise <see langword="default"/>.</param>
    public abstract bool TryMeasureWithoutForcing(out TMeasure measure);
}

/// <summary>
/// Deferred push of one node onto the front of a computed middle tree, created by a prefix-overflow on
/// <see cref="MeasuredTree{TElement, TChild, TMeasure, TMonoid}.Cons"/>.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TNode">Element type of the middle tree.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
/// <param name="source">The computed middle the owning deep node had before the overflow.</param>
/// <param name="node">The node pushed down by the overflow.</param>
/// <remarks>
/// The source is forced by the creator, never captured as a nested suspension. This is the original paper's
/// "forcing the middle subtree m in the recursive case of cons to avoid building a chain of suspensions"; it
/// keeps every suspension exactly one deferred operation deep, bounding the forcing recursion by the tree
/// depth while leaving the amortized analysis unaffected.
/// </remarks>
internal sealed class PendingMeasuredPushFront<TElement, TNode, TMeasure, TMonoid>(
    MeasuredTree<TElement, TNode, TMeasure, TMonoid> source,
    TNode node)
    : PendingMeasured<TElement, TNode, TMeasure, TMonoid>
    where TNode : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <inheritdoc/>
    public override MeasuredTree<TElement, TNode, TMeasure, TMonoid> Run() => source.Cons(node);

    /// <inheritdoc/>
    public override bool TryMeasureWithoutForcing(out TMeasure measure)
    {
        measure = TMonoid.Combine(node.Measure, source.Measure);
        return true;
    }
}

/// <summary>
/// Deferred push of one node onto the back of a computed middle tree, created by a suffix-overflow on
/// <see cref="MeasuredTree{TElement, TChild, TMeasure, TMonoid}.Snoc"/>.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TNode">Element type of the middle tree.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
/// <param name="source">The computed middle the owning deep node had before the overflow.</param>
/// <param name="node">The node pushed down by the overflow.</param>
/// <remarks>The source is forced by the creator; see <see cref="PendingMeasuredPushFront{TElement, TNode,
/// TMeasure, TMonoid}"/> for why.</remarks>
internal sealed class PendingMeasuredPushBack<TElement, TNode, TMeasure, TMonoid>(
    MeasuredTree<TElement, TNode, TMeasure, TMonoid> source,
    TNode node)
    : PendingMeasured<TElement, TNode, TMeasure, TMonoid>
    where TNode : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <inheritdoc/>
    public override MeasuredTree<TElement, TNode, TMeasure, TMonoid> Run() => source.Snoc(node);

    /// <inheritdoc/>
    public override bool TryMeasureWithoutForcing(out TMeasure measure)
    {
        measure = TMonoid.Combine(source.Measure, node.Measure);
        return true;
    }
}

/// <summary>
/// Deferred removal of the first node of a computed middle tree, created when a prefix-underflow on
/// <see cref="MeasuredTree{TElement, TChild, TMeasure, TMonoid}.TryViewLeft"/> pulls the head node up.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TNode">Element type of the middle tree.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
/// <param name="source">The computed middle whose head node was exposed at the level above.</param>
/// <remarks>
/// A general monoid cannot subtract the removed node's measure, so this operation's measure is unavailable
/// without forcing; <see cref="TryMeasureWithoutForcing"/> returns <see langword="false"/>.
/// </remarks>
internal sealed class PendingMeasuredPopFront<TElement, TNode, TMeasure, TMonoid>(
    MeasuredTree<TElement, TNode, TMeasure, TMonoid> source)
    : PendingMeasured<TElement, TNode, TMeasure, TMonoid>
    where TNode : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <inheritdoc/>
    public override MeasuredTree<TElement, TNode, TMeasure, TMonoid> Run()
    {
        source.TryViewLeft(out _, out var rest);
        return rest;
    }

    /// <inheritdoc/>
    public override bool TryMeasureWithoutForcing(out TMeasure measure)
    {
        measure = default!;
        return false;
    }
}

/// <summary>
/// Deferred removal of the last node of a computed middle tree, created when a suffix-underflow on
/// <see cref="MeasuredTree{TElement, TChild, TMeasure, TMonoid}.TryViewRight"/> pulls the last node up.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type.</typeparam>
/// <typeparam name="TNode">Element type of the middle tree.</typeparam>
/// <typeparam name="TMeasure">Measure type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider.</typeparam>
/// <param name="source">The computed middle whose last node was exposed at the level above.</param>
/// <remarks>Its measure is unavailable without forcing; see <see cref="PendingMeasuredPopFront{TElement, TNode,
/// TMeasure, TMonoid}"/>.</remarks>
internal sealed class PendingMeasuredPopBack<TElement, TNode, TMeasure, TMonoid>(
    MeasuredTree<TElement, TNode, TMeasure, TMonoid> source)
    : PendingMeasured<TElement, TNode, TMeasure, TMonoid>
    where TNode : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <inheritdoc/>
    public override MeasuredTree<TElement, TNode, TMeasure, TMonoid> Run()
    {
        source.TryViewRight(out _, out var rest);
        return rest;
    }

    /// <inheritdoc/>
    public override bool TryMeasureWithoutForcing(out TMeasure measure)
    {
        measure = default!;
        return false;
    }
}
