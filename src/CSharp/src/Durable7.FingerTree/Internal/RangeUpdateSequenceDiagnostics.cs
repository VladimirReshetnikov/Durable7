using System.Runtime.CompilerServices;

namespace Durable7.FingerTree;

internal static class RangeUpdateSequenceDiagnostics
{
    [ThreadStatic]
    private static RangeUpdateSequenceDiagnosticSession? s_current;

    internal static bool IsSessionActive
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get => s_current is not null;
    }

    internal static RangeUpdateSequenceDiagnosticSession BeginSession()
    {
        var session = new RangeUpdateSequenceDiagnosticSession(s_current);
        s_current = session;
        return session;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordNodeVisit() => s_current?.RecordNodeVisit();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordNodeAllocation() => s_current?.RecordNodeAllocation();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordFacadeAllocation() => s_current?.RecordFacadeAllocation();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordRotation() => s_current?.RecordRotation();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordPush() => s_current?.RecordPush();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordSubtreeApplication() => s_current?.RecordSubtreeApplication();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordElementMeasureCallback() => s_current?.RecordElementMeasureCallback();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordMeasureCombineCallback() => s_current?.RecordMeasureCombineCallback();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordIdentityTestCallback() => s_current?.RecordIdentityTestCallback();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordTagComposeCallback() => s_current?.RecordTagComposeCallback();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordElementApplyCallback() => s_current?.RecordElementApplyCallback();

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordMeasureApplyCallback() => s_current?.RecordMeasureApplyCallback();

    internal static void EndSession(
        RangeUpdateSequenceDiagnosticSession session,
        RangeUpdateSequenceDiagnosticSession? previous)
    {
        if (!ReferenceEquals(s_current, session))
        {
            throw new InvalidOperationException(
                "Range-update diagnostic sessions must be disposed in stack order.");
        }

        s_current = previous;
    }
}

internal sealed class RangeUpdateSequenceDiagnosticSession : IDisposable
{
    private readonly RangeUpdateSequenceDiagnosticSession? _previous;
    private bool _disposed;
    private long _nodeVisits;
    private long _nodeAllocations;
    private long _facadeAllocations;
    private long _rotations;
    private long _pushes;
    private long _subtreeApplications;
    private long _elementMeasureCallbacks;
    private long _measureCombineCallbacks;
    private long _identityTestCallbacks;
    private long _tagComposeCallbacks;
    private long _elementApplyCallbacks;
    private long _measureApplyCallbacks;

    internal RangeUpdateSequenceDiagnosticSession(RangeUpdateSequenceDiagnosticSession? previous) =>
        _previous = previous;

    internal RangeUpdateSequenceOperationDiagnostics Snapshot => new(
        NodeVisits: _nodeVisits,
        NodeAllocations: _nodeAllocations,
        FacadeAllocations: _facadeAllocations,
        Rotations: _rotations,
        Pushes: _pushes,
        SubtreeApplications: _subtreeApplications,
        ElementMeasureCallbacks: _elementMeasureCallbacks,
        MeasureCombineCallbacks: _measureCombineCallbacks,
        IdentityTestCallbacks: _identityTestCallbacks,
        TagComposeCallbacks: _tagComposeCallbacks,
        ElementApplyCallbacks: _elementApplyCallbacks,
        MeasureApplyCallbacks: _measureApplyCallbacks);

    internal void RecordNodeVisit() => _nodeVisits = checked(_nodeVisits + 1);

    internal void RecordNodeAllocation() => _nodeAllocations = checked(_nodeAllocations + 1);

    internal void RecordFacadeAllocation() => _facadeAllocations = checked(_facadeAllocations + 1);

    internal void RecordRotation() => _rotations = checked(_rotations + 1);

    internal void RecordPush() => _pushes = checked(_pushes + 1);

    internal void RecordSubtreeApplication() => _subtreeApplications = checked(_subtreeApplications + 1);

    internal void RecordElementMeasureCallback() =>
        _elementMeasureCallbacks = checked(_elementMeasureCallbacks + 1);

    internal void RecordMeasureCombineCallback() =>
        _measureCombineCallbacks = checked(_measureCombineCallbacks + 1);

    internal void RecordIdentityTestCallback() =>
        _identityTestCallbacks = checked(_identityTestCallbacks + 1);

    internal void RecordTagComposeCallback() =>
        _tagComposeCallbacks = checked(_tagComposeCallbacks + 1);

    internal void RecordElementApplyCallback() =>
        _elementApplyCallbacks = checked(_elementApplyCallbacks + 1);

    internal void RecordMeasureApplyCallback() =>
        _measureApplyCallbacks = checked(_measureApplyCallbacks + 1);

    public void Dispose()
    {
        if (_disposed)
            return;

        RangeUpdateSequenceDiagnostics.EndSession(this, _previous);
        _disposed = true;
    }
}

internal readonly record struct RangeUpdateSequenceOperationDiagnostics(
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
    internal long PolicyCallbacks => checked(
        ElementMeasureCallbacks
        + MeasureCombineCallbacks
        + IdentityTestCallbacks
        + TagComposeCallbacks
        + ElementApplyCallbacks
        + MeasureApplyCallbacks);
}
