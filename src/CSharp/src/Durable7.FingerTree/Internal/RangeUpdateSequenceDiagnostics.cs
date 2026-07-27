// Counters the range-update sequence records, for tests that a pending tag was pushed down only
// when a read needed to see through it.

using System.Runtime.CompilerServices;

namespace Durable7.FingerTree;

/// <summary>
/// Counters the range-update sequence records, for tests that a pending tag was pushed down only
/// when a read needed to see through it.
/// </summary>
internal static class RangeUpdateSequenceDiagnostics
{
    [ThreadStatic]
    private static RangeUpdateSequenceDiagnosticSession? s_current;

    internal static bool IsSessionActive
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get => s_current is not null;
    }

    /// <summary>Starts collecting diagnostics; disposing the returned session restores the previous state.</summary>
    internal static RangeUpdateSequenceDiagnosticSession BeginSession()
    {
        var session = new RangeUpdateSequenceDiagnosticSession(s_current);
        s_current = session;
        return session;
    }

    /// <summary>Counts one node visit.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordNodeVisit() => s_current?.RecordNodeVisit();

    /// <summary>Counts one node allocation.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordNodeAllocation() => s_current?.RecordNodeAllocation();

    /// <summary>Counts one facade allocation.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordFacadeAllocation() => s_current?.RecordFacadeAllocation();

    /// <summary>Counts one rotation.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordRotation() => s_current?.RecordRotation();

    /// <summary>Counts one push.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordPush() => s_current?.RecordPush();

    /// <summary>Counts one subtree application.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordSubtreeApplication() => s_current?.RecordSubtreeApplication();

    /// <summary>Counts one element-measure callback.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordElementMeasureCallback() => s_current?.RecordElementMeasureCallback();

    /// <summary>Counts one measure-combine callback.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordMeasureCombineCallback() => s_current?.RecordMeasureCombineCallback();

    /// <summary>Counts one identity test callback.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordIdentityTestCallback() => s_current?.RecordIdentityTestCallback();

    /// <summary>Counts one tag compose callback.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordTagComposeCallback() => s_current?.RecordTagComposeCallback();

    /// <summary>Counts one element apply callback.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordElementApplyCallback() => s_current?.RecordElementApplyCallback();

    /// <summary>Counts one measure apply callback.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordMeasureApplyCallback() => s_current?.RecordMeasureApplyCallback();

    /// <summary>Stops collecting diagnostics and restores the previous state.</summary>
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

/// <summary>
/// A scope over which range-update counters are collected; disposing it restores the previous
/// state.
/// </summary>
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

    /// <summary>Creates a new range update sequence diagnostic session.</summary>
    internal RangeUpdateSequenceDiagnosticSession(RangeUpdateSequenceDiagnosticSession? previous) =>
        _previous = previous;

    /// <summary>Gets the sequence version this cursor is positioned in.</summary>
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

    /// <summary>Counts one node visit.</summary>
    internal void RecordNodeVisit() => _nodeVisits = checked(_nodeVisits + 1);

    /// <summary>Counts one node allocation.</summary>
    internal void RecordNodeAllocation() => _nodeAllocations = checked(_nodeAllocations + 1);

    /// <summary>Counts one facade allocation.</summary>
    internal void RecordFacadeAllocation() => _facadeAllocations = checked(_facadeAllocations + 1);

    /// <summary>Counts one rotation.</summary>
    internal void RecordRotation() => _rotations = checked(_rotations + 1);

    /// <summary>Counts one push.</summary>
    internal void RecordPush() => _pushes = checked(_pushes + 1);

    /// <summary>Counts one subtree application.</summary>
    internal void RecordSubtreeApplication() => _subtreeApplications = checked(_subtreeApplications + 1);

    /// <summary>Counts one element-measure callback.</summary>
    internal void RecordElementMeasureCallback() =>
        _elementMeasureCallbacks = checked(_elementMeasureCallbacks + 1);

    /// <summary>Counts one measure-combine callback.</summary>
    internal void RecordMeasureCombineCallback() =>
        _measureCombineCallbacks = checked(_measureCombineCallbacks + 1);

    /// <summary>Counts one identity test callback.</summary>
    internal void RecordIdentityTestCallback() =>
        _identityTestCallbacks = checked(_identityTestCallbacks + 1);

    /// <summary>Counts one tag compose callback.</summary>
    internal void RecordTagComposeCallback() =>
        _tagComposeCallbacks = checked(_tagComposeCallbacks + 1);

    /// <summary>Counts one element apply callback.</summary>
    internal void RecordElementApplyCallback() =>
        _elementApplyCallbacks = checked(_elementApplyCallbacks + 1);

    /// <summary>Counts one measure apply callback.</summary>
    internal void RecordMeasureApplyCallback() =>
        _measureApplyCallbacks = checked(_measureApplyCallbacks + 1);

    /// <summary>Releases the resources this value holds.</summary>
    public void Dispose()
    {
        if (_disposed)
            return;

        RangeUpdateSequenceDiagnostics.EndSession(this, _previous);
        _disposed = true;
    }
}

/// <summary>The counts one range-update operation produced.</summary>
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
    /// <summary>
    /// Gets how many times the policy was invoked, which is what an operation's callback bound is asserted against.
    /// </summary>
    internal long PolicyCallbacks => checked(
        ElementMeasureCallbacks
        + MeasureCombineCallbacks
        + IdentityTestCallbacks
        + TagComposeCallbacks
        + ElementApplyCallbacks
        + MeasureApplyCallbacks);
}
