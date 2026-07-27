// Counters the rope cursor records, for tests that a local edit reused the cached path rather than
// re-descending.

using System.Runtime.CompilerServices;

namespace Durable7.FingerTree;

/// <summary>
/// Counters the rope cursor records, for tests that a local edit reused the cached path rather than
/// re-descending.
/// </summary>
internal static class RopeCursorDiagnostics
{
    [ThreadStatic]
    private static RopeCursorDiagnosticSession? s_current;

    internal static bool IsSessionActive
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get => s_current is not null;
    }

    /// <summary>Starts collecting diagnostics; disposing the returned session restores the previous state.</summary>
    internal static RopeCursorDiagnosticSession BeginSession()
    {
        var session = new RopeCursorDiagnosticSession(s_current);
        s_current = session;
        return session;
    }

    /// <summary>Counts one node visits.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordNodeVisits(int count = 1) => s_current?.RecordNodeVisits(count);

    /// <summary>Counts one spine allocation.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordSpineAllocation(int count = 1) => s_current?.RecordSpineAllocation(count);

    /// <summary>Counts one forced suspension.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordForcedSuspension(int count = 1) => s_current?.RecordForcedSuspension(count);

    /// <summary>Counts one focus copy.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordFocusCopy(int elements) => s_current?.RecordFocusCopy(elements);

    /// <summary>Counts one carry copy.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordCarryCopy(int elements) => s_current?.RecordCarryCopy(elements);

    /// <summary>Counts one wrapper allocation.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordWrapperAllocation(int count = 1) => s_current?.RecordWrapperAllocation(count);

    /// <summary>Counts one snapshot normalization.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordSnapshotNormalization(int count = 1) => s_current?.RecordSnapshotNormalization(count);

    /// <summary>Counts one element-measure callback.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordElementMeasureCallback(int count = 1) => s_current?.RecordElementMeasureCallback(count);

    /// <summary>Counts one measure-combine callback.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static void RecordMeasureCombineCallback(int count = 1) => s_current?.RecordMeasureCombineCallback(count);

    /// <summary>Stops collecting diagnostics and restores the previous state.</summary>
    internal static void EndSession(RopeCursorDiagnosticSession session, RopeCursorDiagnosticSession? previous)
    {
        if (!ReferenceEquals(s_current, session))
            throw new InvalidOperationException("Rope diagnostic sessions must be disposed in stack order.");

        s_current = previous;
    }
}

/// <summary>
/// A scope over which rope cursor counters are collected; disposing it restores the previous state.
/// </summary>
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

    /// <summary>Creates a new rope cursor diagnostic session.</summary>
    internal RopeCursorDiagnosticSession(RopeCursorDiagnosticSession? previous) => _previous = previous;

    /// <summary>Gets the rope version this cursor is positioned in.</summary>
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

    /// <summary>Counts one node visits.</summary>
    internal void RecordNodeVisits(int count) => _nodeVisits = checked(_nodeVisits + count);

    /// <summary>Counts one spine allocation.</summary>
    internal void RecordSpineAllocation(int count) => _spineAllocations = checked(_spineAllocations + count);

    /// <summary>Counts one forced suspension.</summary>
    internal void RecordForcedSuspension(int count) => _forcedSuspensions = checked(_forcedSuspensions + count);

    /// <summary>Counts one focus copy.</summary>
    internal void RecordFocusCopy(int elements)
    {
        _focusCopies++;
        _focusElementsCopied = checked(_focusElementsCopied + elements);
    }

    /// <summary>Counts one carry copy.</summary>
    internal void RecordCarryCopy(int elements)
    {
        _carryCopies++;
        _carryElementsCopied = checked(_carryElementsCopied + elements);
    }

    /// <summary>Counts one wrapper allocation.</summary>
    internal void RecordWrapperAllocation(int count) => _wrapperAllocations = checked(_wrapperAllocations + count);

    /// <summary>Counts one snapshot normalization.</summary>
    internal void RecordSnapshotNormalization(int count) => _snapshotNormalizations = checked(_snapshotNormalizations + count);

    /// <summary>Counts one element-measure callback.</summary>
    internal void RecordElementMeasureCallback(int count) => _elementMeasureCallbacks = checked(_elementMeasureCallbacks + count);

    /// <summary>Counts one measure-combine callback.</summary>
    internal void RecordMeasureCombineCallback(int count) => _measureCombineCallbacks = checked(_measureCombineCallbacks + count);

    /// <summary>Releases the resources this value holds.</summary>
    public void Dispose()
    {
        if (_disposed)
            return;

        RopeCursorDiagnostics.EndSession(this, _previous);
        _disposed = true;
    }
}

/// <summary>The counts one rope cursor operation produced.</summary>
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
