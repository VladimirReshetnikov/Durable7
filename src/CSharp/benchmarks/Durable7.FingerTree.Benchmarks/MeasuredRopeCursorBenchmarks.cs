using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Configs;
using TextCursor = Durable7.FingerTree.MeasuredRopeCursor<char, int, Durable7.FingerTree.NewlineMeasure>;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>Deterministic newline distributions used by the C2 measured-text gates.</summary>
public enum MeasuredTextNewlineDensity
{
    /// <summary>Places one newline every 256 UTF-16 elements.</summary>
    Sparse,

    /// <summary>Places one newline every eight UTF-16 elements.</summary>
    Dense,
}

/// <summary>
/// Compares indexed measured-rope replacement histories with the public persistent measured cursor
/// over the locked document, locality, snapshot-cadence, and newline-density matrix.
/// </summary>
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class MeasuredRopeCursorBenchmarks
{
    private MeasuredRope<char, int, NewlineMeasure> _rope = null!;
    private int[] _positions = null!;

    /// <summary>Gets or sets the number of UTF-16 elements in the source document.</summary>
    [Params(1_024, 65_536, 1_048_576)]
    public int DocumentSize { get; set; }

    /// <summary>Gets or sets the local replacement window.</summary>
    [Params(1, 8, 256)]
    public int LocalityWindow { get; set; }

    /// <summary>Gets or sets the number of replacements between canonical snapshots.</summary>
    [Params(1, 16, 256)]
    public int SnapshotCadence { get; set; }

    /// <summary>Gets or sets the deterministic newline distribution.</summary>
    [Params(MeasuredTextNewlineDensity.Sparse, MeasuredTextNewlineDensity.Dense)]
    public MeasuredTextNewlineDensity NewlineDensity { get; set; }

    internal MeasuredRopeCursorDiagnosticReport Diagnostics { get; private set; }

    /// <summary>
    /// Creates identical inputs for both timed lanes, proves their result parity, and collects
    /// callback diagnostics outside the timed region.
    /// </summary>
    [GlobalSetup]
    public void Setup()
    {
        var data = MeasuredRopeCursorBenchmarkWorkload.CreateText(DocumentSize, NewlineDensity);
        _rope = MeasuredRope<char, int, NewlineMeasure>.Create(data);
        _positions = RopeCursorBenchmarkWorkload.CreatePositions(
            DocumentSize,
            LocalityWindow,
            Axis2BenchmarkPolicy.MeasuredCursorGateEditCount);

        var indexed = MeasuredRopeCursorBenchmarkWorkload.RunIndexed<NewlineMeasure>(
            _rope,
            _positions,
            SnapshotCadence);
        var cursor = MeasuredRopeCursorBenchmarkWorkload.RunCursor<NewlineMeasure>(
            _rope,
            _positions,
            SnapshotCadence);
        MeasuredRopeCursorBenchmarkWorkload.RequireEquivalent(indexed, cursor, "local-edit setup");

        Diagnostics = MeasuredRopeCursorBenchmarkWorkload.CollectDiagnostics(
            data,
            _positions,
            SnapshotCadence);
    }

    /// <summary>Emits callback evidence after the timed job has finished.</summary>
    [GlobalCleanup]
    public void Cleanup() => Console.WriteLine(Diagnostics.ToMachineReadableLine(
        "local-edit",
        DocumentSize,
        LocalityWindow,
        NewlineDensity));

    /// <summary>Runs 256 replacements through ordinary indexed persistent updates.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "LocalEdit")]
    public MeasuredRope<char, int, NewlineMeasure> IndexedMeasuredRopeEditBurst() =>
        MeasuredRopeCursorBenchmarkWorkload.RunIndexed<NewlineMeasure>(
            _rope,
            _positions,
            SnapshotCadence);

    /// <summary>
    /// Runs the identical replacement positions and snapshot cadence through the public measured cursor.
    /// </summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "LocalEdit")]
    public MeasuredRope<char, int, NewlineMeasure> PublicMeasuredCursorEditBurst() =>
        MeasuredRopeCursorBenchmarkWorkload.RunCursor<NewlineMeasure>(
            _rope,
            _positions,
            SnapshotCadence);
}

/// <summary>
/// The compact C2 edit shipment gate: 64K UTF-16 elements, 256 replacements, locality eight, and
/// snapshot cadence sixteen. Both locked newline distributions run under the same full job.
/// </summary>
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class MeasuredRopeCursorGateBenchmarks
{
    private MeasuredRope<char, int, NewlineMeasure> _rope = null!;
    private int[] _positions = null!;

    /// <summary>Gets or sets the deterministic newline distribution.</summary>
    [Params(MeasuredTextNewlineDensity.Sparse, MeasuredTextNewlineDensity.Dense)]
    public MeasuredTextNewlineDensity NewlineDensity { get; set; }

    internal MeasuredRopeCursorDiagnosticReport Diagnostics { get; private set; }

    /// <summary>Creates and validates the exact predeclared gate history.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var data = MeasuredRopeCursorBenchmarkWorkload.CreateText(
            Axis2BenchmarkPolicy.MeasuredCursorGateDocumentSize,
            NewlineDensity);
        _rope = MeasuredRope<char, int, NewlineMeasure>.Create(data);
        _positions = RopeCursorBenchmarkWorkload.CreatePositions(
            Axis2BenchmarkPolicy.MeasuredCursorGateDocumentSize,
            Axis2BenchmarkPolicy.MeasuredCursorGateLocalityWindow,
            Axis2BenchmarkPolicy.MeasuredCursorGateEditCount);

        var indexed = MeasuredRopeCursorBenchmarkWorkload.RunIndexed<NewlineMeasure>(
            _rope,
            _positions,
            Axis2BenchmarkPolicy.MeasuredCursorGateSnapshotCadence);
        var cursor = MeasuredRopeCursorBenchmarkWorkload.RunCursor<NewlineMeasure>(
            _rope,
            _positions,
            Axis2BenchmarkPolicy.MeasuredCursorGateSnapshotCadence);
        MeasuredRopeCursorBenchmarkWorkload.RequireEquivalent(indexed, cursor, "shipment-gate setup");

        Diagnostics = MeasuredRopeCursorBenchmarkWorkload.CollectDiagnostics(
            data,
            _positions,
            Axis2BenchmarkPolicy.MeasuredCursorGateSnapshotCadence);
    }

    /// <summary>Emits the gate's callback evidence after the timed job has finished.</summary>
    [GlobalCleanup]
    public void Cleanup() => Console.WriteLine(Diagnostics.ToMachineReadableLine(
        "edit-gate",
        Axis2BenchmarkPolicy.MeasuredCursorGateDocumentSize,
        Axis2BenchmarkPolicy.MeasuredCursorGateLocalityWindow,
        NewlineDensity));

    /// <summary>Runs the gate history through indexed measured-rope replacements.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "Gate")]
    public MeasuredRope<char, int, NewlineMeasure> IndexedMeasuredRope() =>
        MeasuredRopeCursorBenchmarkWorkload.RunIndexed<NewlineMeasure>(
            _rope,
            _positions,
            Axis2BenchmarkPolicy.MeasuredCursorGateSnapshotCadence);

    /// <summary>Runs the gate history through the public measured cursor.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "Gate")]
    public MeasuredRope<char, int, NewlineMeasure> PublicMeasuredCursor() =>
        MeasuredRopeCursorBenchmarkWorkload.RunCursor<NewlineMeasure>(
            _rope,
            _positions,
            Axis2BenchmarkPolicy.MeasuredCursorGateSnapshotCadence);
}

/// <summary>
/// Measures positional seek, absolute measure seek, and UTF-16 line/column queries separately from
/// local editing. Each category has its own semantic baseline and shares one prepared input version.
/// </summary>
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class MeasuredRopeCursorQueryBenchmarks
{
    private MeasuredRope<char, int, NewlineMeasure> _rope = null!;
    private TextCursor _preparedCursor;
    private TextCursor _lineColumnCursor;
    private Func<int, bool> _delegatePredicate = null!;
    private MeasureAtLeastPredicate _structPredicate;
    private int _position;

    /// <summary>Gets the document-size matrix, or only the locked gate size when explicitly requested.</summary>
    public IEnumerable<int> DocumentSizes => string.Equals(
            Environment.GetEnvironmentVariable("D7_AXIS2_C2_GATE_ONLY"),
            "1",
            StringComparison.Ordinal)
        ? [Axis2BenchmarkPolicy.MeasuredCursorGateDocumentSize]
        : [1_024, 65_536, 1_048_576];

    /// <summary>Gets or sets the number of UTF-16 elements in the source document.</summary>
    [ParamsSource(nameof(DocumentSizes))]
    public int DocumentSize { get; set; }

    /// <summary>Gets or sets the deterministic newline distribution.</summary>
    [Params(MeasuredTextNewlineDensity.Sparse, MeasuredTextNewlineDensity.Dense)]
    public MeasuredTextNewlineDensity NewlineDensity { get; set; }

    internal MeasuredRopeCursorDiagnosticReport Diagnostics { get; private set; }

    /// <summary>Creates prepared cursors and proves every timed query returns the baseline result.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var data = MeasuredRopeCursorBenchmarkWorkload.CreateText(DocumentSize, NewlineDensity);
        _rope = MeasuredRope<char, int, NewlineMeasure>.Create(data);
        _position = (DocumentSize * 3) / 4;
        _preparedCursor = _rope.GetCursor(DocumentSize / 4);
        _lineColumnCursor = _rope.GetCursor(_position);

        var targetMeasure = Math.Max(1, _rope.Measure / 2);
        _delegatePredicate = measure => measure >= targetMeasure;
        _structPredicate = new MeasureAtLeastPredicate(targetMeasure);

        ValidateQueryParity();
        Diagnostics = MeasuredRopeCursorBenchmarkWorkload.CollectDiagnostics(
            data,
            RopeCursorBenchmarkWorkload.CreatePositions(
                DocumentSize,
                Axis2BenchmarkPolicy.MeasuredCursorGateLocalityWindow,
                Axis2BenchmarkPolicy.MeasuredCursorGateEditCount),
            Axis2BenchmarkPolicy.MeasuredCursorGateSnapshotCadence);
    }

    /// <summary>Emits callback evidence after the timed job has finished.</summary>
    [GlobalCleanup]
    public void Cleanup() => Console.WriteLine(Diagnostics.ToMachineReadableLine(
        "queries",
        DocumentSize,
        Axis2BenchmarkPolicy.MeasuredCursorGateLocalityWindow,
        NewlineDensity));

    /// <summary>Creates a source-bound cursor directly at the requested position.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "PositionSeek")]
    public TextCursor SourceGetCursorAtPosition() => _rope.GetCursor(_position);

    /// <summary>Seeks an already prepared cursor to the same absolute position.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "PositionSeek")]
    public TextCursor PreparedCursorPositionSeek() => _preparedCursor.Seek(_position);

    /// <summary>Uses the existing delegate-based measured-rope locate API.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "MeasureSeekDelegate")]
    public int ExistingMeasureLocateDelegate() => ExistingMeasureLocate(_delegatePredicate);

    /// <summary>Creates a measured cursor from the source through the delegate predicate overload.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "MeasureSeekDelegate")]
    public int SourceToCursorMeasureSeekDelegate() => SourceToCursorMeasureSeek(_delegatePredicate);

    /// <summary>Seeks a prepared cursor through the delegate predicate overload.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "MeasureSeekDelegate")]
    public int PreparedCursorMeasureSeekDelegate() => PreparedCursorMeasureSeek(_delegatePredicate);

    /// <summary>Uses the existing closure-free measured-rope locate API.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "MeasureSeekStruct")]
    public int ExistingMeasureLocateStruct() => ExistingMeasureLocate(_structPredicate);

    /// <summary>Creates a measured cursor from the source through the closure-free predicate overload.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "MeasureSeekStruct")]
    public int SourceToCursorMeasureSeekStruct() => SourceToCursorMeasureSeek(_structPredicate);

    /// <summary>Seeks a prepared cursor through the closure-free predicate overload.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "MeasureSeekStruct")]
    public int PreparedCursorMeasureSeekStruct() => PreparedCursorMeasureSeek(_structPredicate);

    /// <summary>Computes line and UTF-16 column through the existing measured-rope helper.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "LineColumn")]
    public (int Line, int Column) ExistingRopeLineColumn() => _rope.LineColumnOf(_position);

    /// <summary>Computes line and UTF-16 column from an already prepared cursor.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "LineColumn")]
    public (int Line, int Column) PreparedCursorLineColumn() => RopeText.LineColumnOf(_lineColumnCursor);

    private int ExistingMeasureLocate(Func<int, bool> predicate) =>
        _rope.TryLocateByMeasure(predicate, out var index, out var before, out var element)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(index, before, element)
            : int.MinValue;

    private int ExistingMeasureLocate<TPredicate>(TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<int> =>
        _rope.TryLocateByMeasure(predicate, out var index, out var before, out var element)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(index, before, element)
            : int.MinValue;

    private int SourceToCursorMeasureSeek(Func<int, bool> predicate) =>
        _rope.TryGetCursorByMeasure(predicate, out var cursor)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(cursor)
            : int.MinValue;

    private int SourceToCursorMeasureSeek<TPredicate>(TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<int> =>
        _rope.TryGetCursorByMeasure(predicate, out var cursor)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(cursor)
            : int.MinValue;

    private int PreparedCursorMeasureSeek(Func<int, bool> predicate) =>
        _preparedCursor.TrySeekByMeasure(predicate, out var cursor)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(cursor)
            : int.MinValue;

    private int PreparedCursorMeasureSeek<TPredicate>(TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<int> =>
        _preparedCursor.TrySeekByMeasure(predicate, out var cursor)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(cursor)
            : int.MinValue;

    private void ValidateQueryParity()
    {
        var expectedDelegate = ExistingMeasureLocate(_delegatePredicate);
        if (expectedDelegate == int.MinValue ||
            SourceToCursorMeasureSeek(_delegatePredicate) != expectedDelegate ||
            PreparedCursorMeasureSeek(_delegatePredicate) != expectedDelegate)
        {
            throw new InvalidOperationException("Delegate measure-seek lanes do not return the locate boundary.");
        }

        var expectedStruct = ExistingMeasureLocate(_structPredicate);
        if (expectedStruct != expectedDelegate ||
            SourceToCursorMeasureSeek(_structPredicate) != expectedStruct ||
            PreparedCursorMeasureSeek(_structPredicate) != expectedStruct)
        {
            throw new InvalidOperationException("Struct measure-seek lanes do not return the locate boundary.");
        }

        var direct = _rope.GetCursor(_position);
        var sought = _preparedCursor.Seek(_position);
        if (direct.Position != sought.Position || direct.Count != sought.Count ||
            direct.MeasureBefore != sought.MeasureBefore || direct.MeasureAfter != sought.MeasureAfter)
        {
            throw new InvalidOperationException("Positional seek lanes do not return the same cursor state.");
        }

        if (_rope.LineColumnOf(_position) != RopeText.LineColumnOf(_lineColumnCursor))
            throw new InvalidOperationException("Prepared-cursor line/column does not match the rope helper.");
    }
}

/// <summary>
/// Exercises each query against a freshly edited, unpublished cursor version. The explicit
/// snapshot baselines perform the same edit and canonicalization work as the cursor convenience
/// path, so this gate detects accidental duplicate normalization rather than measuring a clean memo.
/// </summary>
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class MeasuredRopeCursorDirtyQueryBenchmarks
{
    private MeasuredRope<char, int, NewlineMeasure> _rope = null!;
    private TextCursor _seedCursor;
    private Func<int, bool> _delegatePredicate = null!;
    private MeasureAtLeastPredicate _structPredicate;
    private int _position;

    /// <summary>Gets or sets the deterministic newline distribution.</summary>
    [Params(MeasuredTextNewlineDensity.Sparse, MeasuredTextNewlineDensity.Dense)]
    public MeasuredTextNewlineDensity NewlineDensity { get; set; }

    /// <summary>Builds the locked 64K gate document and validates all dirty-query result pairs.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var data = MeasuredRopeCursorBenchmarkWorkload.CreateText(
            Axis2BenchmarkPolicy.MeasuredCursorGateDocumentSize,
            NewlineDensity);
        _rope = MeasuredRope<char, int, NewlineMeasure>.Create(data);
        _seedCursor = _rope.GetCursor(_rope.Count / 4);
        _position = (_rope.Count * 3) / 4;
        var targetMeasure = Math.Max(1, _rope.Measure / 2);
        _delegatePredicate = measure => measure >= targetMeasure;
        _structPredicate = new MeasureAtLeastPredicate(targetMeasure);

        if (ExplicitSnapshotMeasureLocate(_delegatePredicate) != DirtyCursorMeasureSeek(_delegatePredicate) ||
            ExplicitSnapshotMeasureLocate(_structPredicate) != DirtyCursorMeasureSeek(_structPredicate))
        {
            throw new InvalidOperationException("Dirty measure-seek lanes do not return the snapshot boundary.");
        }

        var direct = ExplicitSnapshotGetCursorAtPosition();
        var sought = DirtyCursorPositionSeek();
        if (direct.Position != sought.Position || direct.Count != sought.Count ||
            direct.MeasureBefore != sought.MeasureBefore || direct.MeasureAfter != sought.MeasureAfter)
        {
            throw new InvalidOperationException("Dirty positional-seek lanes do not return the same logical state.");
        }

        if (ExplicitSnapshotLineColumn() != DirtyCursorLineColumn())
            throw new InvalidOperationException("Dirty line/column lanes do not return the same UTF-16 location.");
    }

    /// <summary>Explicitly snapshots a fresh edit and creates a cursor at the requested position.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "DirtyPositionSeek")]
    public TextCursor ExplicitSnapshotGetCursorAtPosition()
    {
        var dirty = CreateDirtyCursor();
        return dirty.Snapshot().GetCursor(_position);
    }

    /// <summary>Seeks the same freshly edited version directly.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "DirtyPositionSeek")]
    public TextCursor DirtyCursorPositionSeek() => CreateDirtyCursor().Seek(_position);

    /// <summary>Explicitly snapshots and locates with the delegate predicate.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "DirtyMeasureSeekDelegate")]
    public int ExplicitSnapshotMeasureLocateDelegate() => ExplicitSnapshotMeasureLocate(_delegatePredicate);

    /// <summary>Locates directly on a freshly edited version with the delegate predicate.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "DirtyMeasureSeekDelegate")]
    public int DirtyCursorMeasureSeekDelegate() => DirtyCursorMeasureSeek(_delegatePredicate);

    /// <summary>Explicitly snapshots and locates with the closure-free predicate.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "DirtyMeasureSeekStruct")]
    public int ExplicitSnapshotMeasureLocateStruct() => ExplicitSnapshotMeasureLocate(_structPredicate);

    /// <summary>Locates directly on a freshly edited version with the closure-free predicate.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "DirtyMeasureSeekStruct")]
    public int DirtyCursorMeasureSeekStruct() => DirtyCursorMeasureSeek(_structPredicate);

    /// <summary>Explicitly snapshots a fresh edit before querying its insertion gap.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C2", "DirtyLineColumn")]
    public (int Line, int Column) ExplicitSnapshotLineColumn()
    {
        var dirty = CreateDirtyCursor();
        return dirty.Snapshot().LineColumnOf(dirty.Position);
    }

    /// <summary>Queries line and UTF-16 column directly on the freshly edited cursor.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C2", "DirtyLineColumn")]
    public (int Line, int Column) DirtyCursorLineColumn()
    {
        var dirty = CreateDirtyCursor();
        return RopeText.LineColumnOf(dirty);
    }

    private TextCursor CreateDirtyCursor() => _seedCursor.Insert('x');

    private int ExplicitSnapshotMeasureLocate(Func<int, bool> predicate)
    {
        var snapshot = CreateDirtyCursor().Snapshot();
        return snapshot.TryLocateByMeasure(predicate, out var index, out var before, out var element)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(index, before, element)
            : int.MinValue;
    }

    private int ExplicitSnapshotMeasureLocate<TPredicate>(TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<int>
    {
        var snapshot = CreateDirtyCursor().Snapshot();
        return snapshot.TryLocateByMeasure(predicate, out var index, out var before, out var element)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(index, before, element)
            : int.MinValue;
    }

    private int DirtyCursorMeasureSeek(Func<int, bool> predicate) =>
        CreateDirtyCursor().TrySeekByMeasure(predicate, out var cursor)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(cursor)
            : int.MinValue;

    private int DirtyCursorMeasureSeek<TPredicate>(TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<int> =>
        CreateDirtyCursor().TrySeekByMeasure(predicate, out var cursor)
            ? MeasuredRopeCursorBenchmarkWorkload.MeasureSeekChecksum(cursor)
            : int.MinValue;
}

internal static class MeasuredRopeCursorBenchmarkWorkload
{
    internal static char[] CreateText(int length, MeasuredTextNewlineDensity density)
    {
        var newlineInterval = density switch
        {
            MeasuredTextNewlineDensity.Sparse => Axis2BenchmarkPolicy.MeasuredCursorSparseNewlineInterval,
            MeasuredTextNewlineDensity.Dense => Axis2BenchmarkPolicy.MeasuredCursorDenseNewlineInterval,
            _ => throw new ArgumentOutOfRangeException(nameof(density)),
        };
        return Axis2BenchmarkPolicy.CreateText(length, newlineInterval);
    }

    internal static MeasuredRope<char, int, TMeasureOps> RunIndexed<TMeasureOps>(
        MeasuredRope<char, int, TMeasureOps> source,
        int[] positions,
        int snapshotCadence)
        where TMeasureOps : IMeasure<char, int>
    {
        var rope = source;
        var snapshot = source;
        for (var index = 0; index < positions.Length; index++)
        {
            rope = rope.SetItem(positions[index], Replacement(index));
            if (ShouldSnapshot(index, positions.Length, snapshotCadence))
                snapshot = rope;
        }

        return snapshot;
    }

    internal static MeasuredRope<char, int, TMeasureOps> RunCursor<TMeasureOps>(
        MeasuredRope<char, int, TMeasureOps> source,
        int[] positions,
        int snapshotCadence)
        where TMeasureOps : IMeasure<char, int>
    {
        if (positions.Length == 0)
            return source;

        var cursor = source.GetCursor(positions[0]);
        var snapshot = source;
        for (var index = 0; index < positions.Length; index++)
        {
            cursor = MoveTo(cursor, positions[index]);
            cursor = cursor.ReplaceNext(Replacement(index));
            if (ShouldSnapshot(index, positions.Length, snapshotCadence))
                snapshot = cursor.Snapshot();
        }

        return snapshot;
    }

    internal static void RequireEquivalent<TMeasureOps>(
        MeasuredRope<char, int, TMeasureOps> expected,
        MeasuredRope<char, int, TMeasureOps> actual,
        string operation)
        where TMeasureOps : IMeasure<char, int>
    {
        if (expected.Count != actual.Count ||
            !EqualityComparer<int>.Default.Equals(expected.Measure, actual.Measure) ||
            !expected.SequenceEqual(actual))
        {
            throw new InvalidOperationException($"Measured cursor {operation} diverged from indexed editing.");
        }
    }

    internal static int MeasureSeekChecksum(int index, int measureBefore, char element) =>
        unchecked(((index * 397) ^ measureBefore) * 397 ^ element);

    internal static int MeasureSeekChecksum<TMeasureOps>(
        MeasuredRopeCursor<char, int, TMeasureOps> cursor)
        where TMeasureOps : IMeasure<char, int>
    {
        if (!cursor.TryPeekNext(out var element))
            throw new InvalidOperationException("A successful measure seek did not expose its boundary element.");
        return MeasureSeekChecksum(cursor.Position, cursor.MeasureBefore, element);
    }

    internal static MeasuredRopeCursorDiagnosticReport CollectDiagnostics(
        char[] data,
        int[] positions,
        int snapshotCadence)
    {
        var source = MeasuredRope<char, int, CountingNewlineMeasure>.Create(data);

        CountingNewlineMeasure.Reset();
        var indexed = RunIndexed<CountingNewlineMeasure>(source, positions, snapshotCadence);
        var indexedCallbacks = CountingNewlineMeasure.Snapshot();

        CountingNewlineMeasure.Reset();
        var (cursor, snapshotCallbacks) = RunCursorWithSnapshotCallbackAudit(
            source,
            positions,
            snapshotCadence);
        var cursorCallbacks = CountingNewlineMeasure.Snapshot();
        RequireEquivalent(indexed, cursor, "callback-diagnostic edit history");

        var targetMeasure = Math.Max(1, source.Measure / 2);
        var predicate = new MeasureAtLeastPredicate(targetMeasure);
        if (!source.TryLocateByMeasure(predicate, out var expectedIndex, out var expectedBefore, out var expectedElement))
            throw new InvalidOperationException("The callback diagnostic measure target was not found.");

        CountingNewlineMeasure.Reset();
        if (!source.TryGetCursorByMeasure(predicate, out var located))
            throw new InvalidOperationException("Source-to-cursor callback diagnostic unexpectedly missed.");
        if (MeasureSeekChecksum(located) != MeasureSeekChecksum(expectedIndex, expectedBefore, expectedElement))
            throw new InvalidOperationException("Source-to-cursor callback diagnostic found the wrong boundary.");
        var sourceSeekCallbacks = CountingNewlineMeasure.Snapshot();
        RequireSeekMeasureCeiling("Source-to-cursor", sourceSeekCallbacks);

        var prepared = source.GetCursor(source.Count / 4);
        CountingNewlineMeasure.Reset();
        if (!prepared.TrySeekByMeasure(predicate, out var sought))
            throw new InvalidOperationException("Prepared-cursor callback diagnostic unexpectedly missed.");
        if (MeasureSeekChecksum(sought) != MeasureSeekChecksum(expectedIndex, expectedBefore, expectedElement))
            throw new InvalidOperationException("Prepared-cursor callback diagnostic found the wrong boundary.");
        var preparedSeekCallbacks = CountingNewlineMeasure.Snapshot();
        RequireSeekMeasureCeiling("Prepared-cursor", preparedSeekCallbacks);

        return new MeasuredRopeCursorDiagnosticReport(
            positions.Length,
            snapshotCadence,
            indexedCallbacks,
            cursorCallbacks,
            snapshotCallbacks,
            sourceSeekCallbacks,
            preparedSeekCallbacks);
    }

    private static (
        MeasuredRope<char, int, CountingNewlineMeasure> Snapshot,
        MeasureCallbackCounts SnapshotCallbacks) RunCursorWithSnapshotCallbackAudit(
            MeasuredRope<char, int, CountingNewlineMeasure> source,
            int[] positions,
            int snapshotCadence)
    {
        if (positions.Length == 0)
            return (source, default);

        var cursor = source.GetCursor(positions[0]);
        var snapshot = source;
        long snapshotMeasureCalls = 0;
        long snapshotCombineCalls = 0;
        for (var index = 0; index < positions.Length; index++)
        {
            cursor = MoveTo(cursor, positions[index]);
            cursor = cursor.ReplaceNext(Replacement(index));
            if (!ShouldSnapshot(index, positions.Length, snapshotCadence))
                continue;

            var before = CountingNewlineMeasure.Snapshot();
            snapshot = cursor.Snapshot();
            var after = CountingNewlineMeasure.Snapshot();
            var measureDelta = after.MeasureCalls - before.MeasureCalls;
            var combineDelta = after.CombineCalls - before.CombineCalls;
            if (measureDelta != 0)
            {
                throw new InvalidOperationException(
                    $"Measured cursor Snapshot() made {measureDelta} element Measure callbacks; " +
                    "publication must consume the edit/focus caches without remeasuring elements.");
            }

            snapshotMeasureCalls += measureDelta;
            snapshotCombineCalls += combineDelta;
        }

        return (snapshot, new MeasureCallbackCounts(snapshotMeasureCalls, snapshotCombineCalls));
    }

    private static void RequireSeekMeasureCeiling(string operation, MeasureCallbackCounts callbacks)
    {
        if (callbacks.MeasureCalls <=
            Axis2BenchmarkPolicy.MeasuredCursorMaximumMeasureSeekMeasureCallbacks)
        {
            return;
        }

        throw new InvalidOperationException(
            $"{operation} seek made {callbacks.MeasureCalls} Measure callbacks; " +
            $"the locked ceiling is {Axis2BenchmarkPolicy.MeasuredCursorMaximumMeasureSeekMeasureCallbacks}.");
    }

    private static MeasuredRopeCursor<char, int, TMeasureOps> MoveTo<TMeasureOps>(
        MeasuredRopeCursor<char, int, TMeasureOps> cursor,
        int position)
        where TMeasureOps : IMeasure<char, int>
    {
        while (cursor.Position < position)
            cursor = cursor.MoveNext();
        while (cursor.Position > position)
            cursor = cursor.MovePrevious();
        return cursor;
    }

    private static char Replacement(int index) => (char)('A' + index % 26);

    private static bool ShouldSnapshot(int index, int count, int cadence) =>
        index + 1 == count || (index + 1) % cadence == 0;
}

internal readonly record struct MeasureCallbackCounts(long MeasureCalls, long CombineCalls);

internal readonly record struct MeasuredRopeCursorDiagnosticReport(
    int EditCount,
    int SnapshotCadence,
    MeasureCallbackCounts IndexedEditCallbacks,
    MeasureCallbackCounts CursorEditCallbacks,
    MeasureCallbackCounts CursorSnapshotCallbacks,
    MeasureCallbackCounts SourceMeasureSeekCallbacks,
    MeasureCallbackCounts PreparedMeasureSeekCallbacks)
{
    internal string ToMachineReadableLine(
        string scope,
        int documentSize,
        int localityWindow,
        MeasuredTextNewlineDensity newlineDensity) => FormattableString.Invariant(
            $"AXIS2_C2_CALLBACK_V1 scope={scope} document_size={documentSize} newline_density={newlineDensity.ToString().ToLowerInvariant()} locality_window={localityWindow} edit_count={EditCount} snapshot_cadence={SnapshotCadence} indexed_measure={IndexedEditCallbacks.MeasureCalls} indexed_combine={IndexedEditCallbacks.CombineCalls} cursor_measure={CursorEditCallbacks.MeasureCalls} cursor_combine={CursorEditCallbacks.CombineCalls} snapshot_measure={CursorSnapshotCallbacks.MeasureCalls} snapshot_combine={CursorSnapshotCallbacks.CombineCalls} source_seek_measure={SourceMeasureSeekCallbacks.MeasureCalls} source_seek_combine={SourceMeasureSeekCallbacks.CombineCalls} prepared_seek_measure={PreparedMeasureSeekCallbacks.MeasureCalls} prepared_seek_combine={PreparedMeasureSeekCallbacks.CombineCalls}");
}

internal readonly struct MeasureAtLeastPredicate(int target) : IMeasurePredicate<int>
{
    public bool Invoke(int measure) => measure >= target;
}

internal readonly struct CountingNewlineMeasure : IMeasure<char, int>
{
    private static long s_measureCalls;
    private static long s_combineCalls;

    public static int Empty => 0;

    public static int Measure(char value)
    {
        s_measureCalls++;
        return value == '\n' ? 1 : 0;
    }

    public static int Combine(int left, int right)
    {
        s_combineCalls++;
        return left + right;
    }

    internal static void Reset()
    {
        s_measureCalls = 0;
        s_combineCalls = 0;
    }

    internal static MeasureCallbackCounts Snapshot() => new(s_measureCalls, s_combineCalls);
}
