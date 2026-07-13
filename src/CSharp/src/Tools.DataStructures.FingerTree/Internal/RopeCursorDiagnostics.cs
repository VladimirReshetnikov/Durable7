using System.Runtime.CompilerServices;

namespace Tools.DataStructures.FingerTree;

internal static class RopeCursorDiagnostics
{
    [ThreadStatic]
    private static RopeCursorDiagnosticSession? s_current;

    internal static RopeCursorDiagnosticSession BeginSession()
    {
        var session = new RopeCursorDiagnosticSession(s_current);
        s_current = session;
        return session;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordNodeVisits(int count = 1) => s_current?.RecordNodeVisits(count);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordSpineAllocation(int count = 1) => s_current?.RecordSpineAllocation(count);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordForcedSuspension(int count = 1) => s_current?.RecordForcedSuspension(count);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordFocusCopy(int elements) => s_current?.RecordFocusCopy(elements);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordCarryCopy(int elements) => s_current?.RecordCarryCopy(elements);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordWrapperAllocation(int count = 1) => s_current?.RecordWrapperAllocation(count);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordSnapshotNormalization(int count = 1) => s_current?.RecordSnapshotNormalization(count);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordElementMeasureCallback(int count = 1) => s_current?.RecordElementMeasureCallback(count);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordMeasureCombineCallback(int count = 1) => s_current?.RecordMeasureCombineCallback(count);

    internal static void EndSession(RopeCursorDiagnosticSession session, RopeCursorDiagnosticSession? previous)
    {
        if (!ReferenceEquals(s_current, session))
            throw new InvalidOperationException("Rope diagnostic sessions must be disposed in stack order.");

        s_current = previous;
    }
}

internal sealed class RopeCursorDiagnosticSession : IDisposable
{
    private readonly RopeCursorDiagnosticSession? _previous;
    private bool _disposed;
    private long _nodeVisits;
    private long _spineAllocations;
    private long _forcedSuspensions;
    private long _focusCopies;
    private long _focusElementsCopied;
    private long _carryCopies;
    private long _carryElementsCopied;
    private long _wrapperAllocations;
    private long _snapshotNormalizations;
    private long _elementMeasureCallbacks;
    private long _measureCombineCallbacks;

    internal RopeCursorDiagnosticSession(RopeCursorDiagnosticSession? previous) => _previous = previous;

    internal RopeCursorOperationDiagnostics Snapshot => new(
        NodeVisits: _nodeVisits,
        SpineAllocations: _spineAllocations,
        ForcedSuspensions: _forcedSuspensions,
        FocusCopies: _focusCopies,
        FocusElementsCopied: _focusElementsCopied,
        CarryCopies: _carryCopies,
        CarryElementsCopied: _carryElementsCopied,
        WrapperAllocations: _wrapperAllocations,
        SnapshotNormalizations: _snapshotNormalizations,
        ElementMeasureCallbacks: _elementMeasureCallbacks,
        MeasureCombineCallbacks: _measureCombineCallbacks);

    internal void RecordNodeVisits(int count) => _nodeVisits = checked(_nodeVisits + count);

    internal void RecordSpineAllocation(int count) => _spineAllocations = checked(_spineAllocations + count);

    internal void RecordForcedSuspension(int count) => _forcedSuspensions = checked(_forcedSuspensions + count);

    internal void RecordFocusCopy(int elements)
    {
        _focusCopies++;
        _focusElementsCopied = checked(_focusElementsCopied + elements);
    }

    internal void RecordCarryCopy(int elements)
    {
        _carryCopies++;
        _carryElementsCopied = checked(_carryElementsCopied + elements);
    }

    internal void RecordWrapperAllocation(int count) => _wrapperAllocations = checked(_wrapperAllocations + count);

    internal void RecordSnapshotNormalization(int count) => _snapshotNormalizations = checked(_snapshotNormalizations + count);

    internal void RecordElementMeasureCallback(int count) => _elementMeasureCallbacks = checked(_elementMeasureCallbacks + count);

    internal void RecordMeasureCombineCallback(int count) => _measureCombineCallbacks = checked(_measureCombineCallbacks + count);

    public void Dispose()
    {
        if (_disposed)
            return;

        RopeCursorDiagnostics.EndSession(this, _previous);
        _disposed = true;
    }
}

internal readonly record struct RopeCursorOperationDiagnostics(
    long NodeVisits,
    long SpineAllocations,
    long ForcedSuspensions,
    long FocusCopies,
    long FocusElementsCopied,
    long CarryCopies,
    long CarryElementsCopied,
    long WrapperAllocations,
    long SnapshotNormalizations,
    long ElementMeasureCallbacks,
    long MeasureCombineCallbacks);
