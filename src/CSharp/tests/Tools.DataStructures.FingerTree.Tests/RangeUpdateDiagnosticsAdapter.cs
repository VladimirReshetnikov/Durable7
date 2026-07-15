using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Stable test-side projection of the evolving internal range-update diagnostics. All assumptions about
/// internal member names live in this file so the behavioral tests remain independent of diagnostic shape.
/// </summary>
internal readonly record struct RangeUpdateStructureSnapshot(
    int Count,
    int NodeCount,
    int Height,
    int MaximumAbsoluteBalanceFactor,
    int PendingTagNodeCount,
    int MaximumPendingTagDepth)
{
    internal bool HasDetailedStatistics => Height >= 0;
}

/// <summary>Test-side projection of deterministic per-operation counters.</summary>
internal readonly record struct RangeUpdateOperationSnapshot(
    long NodeVisits,
    long NodeAllocations,
    long FacadeAllocations,
    long Rotations,
    long Pushes,
    long SubtreeApplications,
    long ElementMeasureCallbacks,
    long MeasureCombineCallbacks,
    long IdentityTestCallbacks,
    long TagComposeCallbacks,
    long ElementApplyCallbacks,
    long MeasureApplyCallbacks)
{
    internal long PolicyCallbacks =>
        ElementMeasureCallbacks
        + MeasureCombineCallbacks
        + IdentityTestCallbacks
        + TagComposeCallbacks
        + ElementApplyCallbacks
        + MeasureApplyCallbacks;
}

/// <summary>Single bridge to internal invariant, sharing, and operation diagnostics.</summary>
internal static class RangeUpdateDiagnosticsAdapter
{
    internal static RangeUpdateStructureSnapshot Validate<TElement, TMeasure, TTag, TOps>(
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> sequence,
        Func<TMeasure, TMeasure, bool> measureEquals)
        where TOps : IRangeUpdateAlgebra<TElement, TMeasure, TTag>
    {
        var report = sequence.ValidateInvariants(measureEquals);

        return new RangeUpdateStructureSnapshot(
            report.Count,
            report.NodeCount,
            report.Height,
            report.MaximumAbsoluteBalanceFactor,
            report.PendingTagNodeCount,
            report.MaximumPendingTagDepth);
    }

    internal static object? RootIdentity<TElement, TMeasure, TTag, TOps>(
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> sequence)
        where TOps : IRangeUpdateAlgebra<TElement, TMeasure, TTag> =>
        sequence.RootIdentityForDiagnostics;

    internal static int CountSharedNodes<TElement, TMeasure, TTag, TOps>(
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> first,
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> second)
        where TOps : IRangeUpdateAlgebra<TElement, TMeasure, TTag> =>
        first.CountSharedNodesForDiagnostics(second);

    internal static (TResult Result, RangeUpdateOperationSnapshot Diagnostics) Observe<TResult>(
        Func<TResult> operation)
    {
        ArgumentNullException.ThrowIfNull(operation);
        using var session = RangeUpdateSequenceDiagnostics.BeginSession();
        var result = operation();
        var snapshot = session.Snapshot;
        return (
            result,
            new RangeUpdateOperationSnapshot(
                snapshot.NodeVisits,
                snapshot.NodeAllocations,
                snapshot.FacadeAllocations,
                snapshot.Rotations,
                snapshot.Pushes,
                snapshot.SubtreeApplications,
                snapshot.ElementMeasureCallbacks,
                snapshot.MeasureCombineCallbacks,
                snapshot.IdentityTestCallbacks,
                snapshot.TagComposeCallbacks,
                snapshot.ElementApplyCallbacks,
                snapshot.MeasureApplyCallbacks));
    }
}
