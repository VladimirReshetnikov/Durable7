// Benchmarks for the rope cursor.

using System.Text;
using BenchmarkDotNet.Attributes;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>
/// Compares the C0 cursor representations with indexed immutable editing and a mutable text
/// control over the locked document, locality, focus-capacity, and snapshot-cadence matrix.
/// </summary>
[MemoryDiagnoser]
public class RopeCursorBenchmarks
{
    private const int EditBurst = 256;
    private const int FlushChunkSize = 2_048;

    private Rope<char> _rope = null!;
    private string _text = null!;
    private int[] _positions = null!;

    /// <summary>Gets or sets the number of UTF-16 elements in the source document.</summary>
    [Params(1_024, 65_536, 1_048_576)]
    public int DocumentSize { get; set; }

    /// <summary>
    /// Gets or sets the local movement window; <see cref="int.MaxValue"/> selects absolute random
    /// seeks rather than a pathologically long sequence of unit cursor moves.
    /// </summary>
    [Params(1, 8, 256, int.MaxValue)]
    public int LocalityWindow { get; set; }

    /// <summary>Gets or sets the number of edits between materialized snapshots.</summary>
    [Params(1, 16, 256)]
    public int SnapshotCadence { get; set; }

    /// <summary>Gets or sets the active focus capacity of each C0 cursor prototype.</summary>
    [Params(16, 32, 64, 128)]
    public int FocusCapacity { get; set; }

    /// <summary>Creates identical source text and edit positions for every comparison lane.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var data = Axis2BenchmarkPolicy.CreateText(DocumentSize);
        _rope = Rope<char>.Create(data);
        _text = new string(data);
        _positions = RopeCursorBenchmarkWorkload.CreatePositions(
            DocumentSize,
            LocalityWindow,
            EditBurst);
    }

    /// <summary>Runs the replacement history through ordinary indexed persistent updates.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C0", "LocalEdit")]
    public Rope<char> IndexedRopeEditBurst() =>
        RopeCursorBenchmarkWorkload.RunIndexedRope(
            _rope,
            _positions,
            SnapshotCadence);

    /// <summary>Runs the replacement history through the immutable class cursor.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "LocalEdit")]
    public Rope<char> ClassCursorEditBurst() =>
        RopeCursorBenchmarkWorkload.RunClassCursor(
            _rope,
            _positions,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            useAbsoluteSeek: LocalityWindow == int.MaxValue);

    /// <summary>Runs the replacement history through the readonly-struct cursor.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "LocalEdit")]
    public Rope<char> StructCursorEditBurst() =>
        RopeCursorBenchmarkWorkload.RunStructCursor(
            _rope,
            _positions,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            useAbsoluteSeek: LocalityWindow == int.MaxValue);

    /// <summary>Runs the replacement history through the mutable-session cursor control.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "MutableControl")]
    public Rope<char> MutableCursorEditBurst() =>
        RopeCursorBenchmarkWorkload.RunMutableCursor(
            _rope,
            _positions,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            useAbsoluteSeek: LocalityWindow == int.MaxValue);

    /// <summary>Runs the same replacements through a mutable contiguous-buffer control.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "MutableControl")]
    public string StringBuilderEditBurst() =>
        RopeCursorBenchmarkWorkload.RunStringBuilder(
            _text,
            _positions,
            SnapshotCadence);
}

/// <summary>
/// Crosses every predeclared focus and flush candidate at the representative 64K/window-8/
/// cadence-16 workload without multiplying the complete C0 comparison matrix by another factor
/// of four.
/// </summary>
[MemoryDiagnoser]
public class RopeCursorTuningBenchmarks
{
    private const int DocumentSize = 65_536;
    private const int LocalityWindow = 8;
    private const int SnapshotCadence = 16;
    private const int EditBurst = 256;

    private Rope<char> _rope = null!;
    private int[] _positions = null!;

    /// <summary>Gets or sets the active focus capacity under evaluation.</summary>
    [Params(16, 32, 64, 128)]
    public int FocusCapacity { get; set; }

    /// <summary>Gets or sets the partial-carry flush threshold under evaluation.</summary>
    [Params(256, 512, 1_024, 2_048)]
    public int FlushChunkSize { get; set; }

    /// <summary>Creates the fixed tuning workload.</summary>
    [GlobalSetup]
    public void Setup()
    {
        _rope = Rope<char>.Create(Axis2BenchmarkPolicy.CreateText(DocumentSize));
        _positions = RopeCursorBenchmarkWorkload.CreatePositions(
            DocumentSize,
            LocalityWindow,
            EditBurst);
    }

    /// <summary>Measures the immutable class representation over the tuning cross-product.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C0", "Tuning")]
    public Rope<char> ClassCursorEditBurst() =>
        RopeCursorBenchmarkWorkload.RunClassCursor(
            _rope,
            _positions,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            useAbsoluteSeek: false);

    /// <summary>Measures the readonly-struct representation over the tuning cross-product.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "Tuning")]
    public Rope<char> StructCursorEditBurst() =>
        RopeCursorBenchmarkWorkload.RunStructCursor(
            _rope,
            _positions,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            useAbsoluteSeek: false);

    /// <summary>Measures the mutable-session control over the tuning cross-product.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "Tuning")]
    public Rope<char> MutableCursorEditBurst() =>
        RopeCursorBenchmarkWorkload.RunMutableCursor(
            _rope,
            _positions,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            useAbsoluteSeek: false);
}

/// <summary>
/// Crosses the complete focus/flush candidate matrix with a bounded carry cycle at the locked
/// 64K/window-8/cadence-16 representative point. The cycle types beyond the largest carry,
/// backspaces the run, types a second run on the other side of a fixed gap, and forward-deletes it.
/// </summary>
/// <remarks>
/// The readonly-struct wrapper is the sole measured representation because all three C0 wrappers
/// share the same focused cursor engine. This keeps the carry-tuning matrix at sixteen cases; the separate
/// replacement matrix measures wrapper overhead.
/// </remarks>
[MemoryDiagnoser]
public class RopeCursorCarryTuningBenchmarks
{
    private const int DocumentSize = 65_536;
    private const int LocalityWindow = 8;
    private const int SnapshotCadence = 16;

    // Divisible by the snapshot cadence and larger than MaxFlushSize + MaxFocusCapacity. Thus the
    // 2,048/128 candidate must publish at least one ordinary chunk on each side of the gap; every
    // smaller candidate necessarily crosses its carry boundary too.
    private const int CarryRunLength = 2_304;

    private Rope<char> _rope = null!;

    /// <summary>Gets or sets the active focus capacity under evaluation.</summary>
    [Params(16, 32, 64, 128)]
    public int FocusCapacity { get; set; }

    /// <summary>Gets or sets the partial-carry flush threshold under evaluation.</summary>
    [Params(256, 512, 1_024, 2_048)]
    public int FlushChunkSize { get; set; }

    /// <summary>
    /// Creates the fixed source and proves outside the timed region that this exact parameterized
    /// cycle flushes both carries and round-trips the source sequence.
    /// </summary>
    [GlobalSetup]
    public void Setup()
    {
        _rope = Rope<char>.Create(Axis2BenchmarkPolicy.CreateText(DocumentSize));
        _ = RopeCursorBenchmarkWorkload.RunStructCursorCarryCycle(
            _rope,
            _rope.Count / 2,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            LocalityWindow,
            CarryRunLength,
            verifyStructuralTransitions: true);
    }

    /// <summary>Measures the common focused cursor engine while both carry directions cross their flush thresholds.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "CarryTuning")]
    public Rope<char> StructCursorCarryCycle() =>
        RopeCursorBenchmarkWorkload.RunStructCursorCarryCycle(
            _rope,
            _rope.Count / 2,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            LocalityWindow,
            CarryRunLength,
            verifyStructuralTransitions: false);
}

/// <summary>
/// The predeclared C0 shipment-gate point: a 64K document, locality window eight, snapshot cadence
/// sixteen, and the lowest-copy focus/carry candidate from the exploratory matrix. This compact
/// class permits a full BenchmarkDotNet job and independent control-noise processes without
/// rerunning the 720-case exploratory surface.
/// </summary>
[MemoryDiagnoser]
public class RopeCursorGateBenchmarks
{
    private const int DocumentSize = 65_536;
    private const int LocalityWindow = 8;
    private const int SnapshotCadence = 16;
    private const int EditBurst = 256;
    private const int FocusCapacity = 16;
    private const int FlushChunkSize = 256;

    private Rope<char> _rope = null!;
    private string _text = null!;
    private int[] _positions = null!;

    /// <summary>Creates the exact shared input history for all five gate lanes.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var data = Axis2BenchmarkPolicy.CreateText(DocumentSize);
        _rope = Rope<char>.Create(data);
        _text = new string(data);
        _positions = RopeCursorBenchmarkWorkload.CreatePositions(
            DocumentSize,
            LocalityWindow,
            EditBurst);
    }

    /// <summary>Runs ordinary indexed immutable replacements at the gate cadence.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2C0", "Gate")]
    public Rope<char> IndexedRope() =>
        RopeCursorBenchmarkWorkload.RunIndexedRope(_rope, _positions, SnapshotCadence);

    /// <summary>Runs the persistent class carrier over the selected cursor configuration.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "Gate")]
    public Rope<char> ClassCursor() =>
        RopeCursorBenchmarkWorkload.RunClassCursor(
            _rope,
            _positions,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            useAbsoluteSeek: false);

    /// <summary>Runs the persistent readonly-struct carrier over the selected configuration.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "Gate")]
    public Rope<char> StructCursor() =>
        RopeCursorBenchmarkWorkload.RunStructCursor(
            _rope,
            _positions,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            useAbsoluteSeek: false);

    /// <summary>Runs the single-wrapper mutable control over the same immutable cursor engine.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "Gate", "MutableControl")]
    public Rope<char> MutableCursorControl() =>
        RopeCursorBenchmarkWorkload.RunMutableCursor(
            _rope,
            _positions,
            SnapshotCadence,
            FocusCapacity,
            FlushChunkSize,
            useAbsoluteSeek: false);

    /// <summary>Runs the mutable contiguous-buffer control with identical snapshot publication.</summary>
    [Benchmark]
    [BenchmarkCategory("Axis2C0", "Gate", "MutableControl")]
    public string StringBuilderControl() =>
        RopeCursorBenchmarkWorkload.RunStringBuilder(_text, _positions, SnapshotCadence);
}

/// <summary>
/// A generated editing workload for the rope cursor, built from a fixed seed so a run is
/// reproducible.
/// </summary>
internal static class RopeCursorBenchmarkWorkload
{
    /// <summary>Measures create positions.</summary>
    internal static int[] CreatePositions(int size, int localityWindow, int count)
    {
        var positions = new int[count];
        var center = size / 2;
        var random = new Random(Axis2BenchmarkPolicy.Seed);
        for (var index = 0; index < positions.Length; index++)
        {
            positions[index] = localityWindow == int.MaxValue
                ? random.Next(size)
                : Math.Clamp(center + (index % localityWindow) - (localityWindow / 2), 0, size - 1);
        }

        return positions;
    }

    /// <summary>Measures run indexed rope.</summary>
    internal static Rope<char> RunIndexedRope(
        Rope<char> source,
        int[] positions,
        int snapshotCadence)
    {
        var rope = source;
        var snapshot = source;
        for (var index = 0; index < positions.Length; index++)
        {
            rope = rope.SetItem(positions[index], Replacement(index));
            if (ShouldSnapshot(index, snapshotCadence))
                snapshot = rope;
        }

        return snapshot;
    }

    /// <summary>Measures run class cursor.</summary>
    internal static Rope<char> RunClassCursor(
        Rope<char> source,
        int[] positions,
        int snapshotCadence,
        int focusCapacity,
        int flushChunkSize,
        bool useAbsoluteSeek)
    {
        var cursor = source.GetClassCursorPrototype(positions[0], focusCapacity, flushChunkSize);
        var snapshot = source;
        for (var index = 0; index < positions.Length; index++)
        {
            cursor = MoveTo(cursor, positions[index], useAbsoluteSeek);
            cursor = cursor.ReplaceNext(Replacement(index));
            if (ShouldSnapshot(index, snapshotCadence))
                snapshot = cursor.Snapshot();
        }

        return snapshot;
    }

    /// <summary>Measures run struct cursor.</summary>
    internal static Rope<char> RunStructCursor(
        Rope<char> source,
        int[] positions,
        int snapshotCadence,
        int focusCapacity,
        int flushChunkSize,
        bool useAbsoluteSeek)
    {
        var cursor = source.GetStructCursorPrototype(positions[0], focusCapacity, flushChunkSize);
        var snapshot = source;
        for (var index = 0; index < positions.Length; index++)
        {
            cursor = MoveTo(cursor, positions[index], useAbsoluteSeek);
            cursor = cursor.ReplaceNext(Replacement(index));
            if (ShouldSnapshot(index, snapshotCadence))
                snapshot = cursor.Snapshot();
        }

        return snapshot;
    }

    /// <summary>Measures run mutable cursor.</summary>
    internal static Rope<char> RunMutableCursor(
        Rope<char> source,
        int[] positions,
        int snapshotCadence,
        int focusCapacity,
        int flushChunkSize,
        bool useAbsoluteSeek)
    {
        var cursor = source.GetMutableCursorPrototype(positions[0], focusCapacity, flushChunkSize);
        var snapshot = source;
        for (var index = 0; index < positions.Length; index++)
        {
            MoveTo(cursor, positions[index], useAbsoluteSeek);
            cursor.ReplaceNext(Replacement(index));
            if (ShouldSnapshot(index, snapshotCadence))
                snapshot = cursor.Snapshot();
        }

        return snapshot;
    }

    /// <summary>Measures run string builder.</summary>
    internal static string RunStringBuilder(
        string source,
        int[] positions,
        int snapshotCadence)
    {
        var builder = new StringBuilder(source);
        var snapshot = source;
        for (var index = 0; index < positions.Length; index++)
        {
            builder[positions[index]] = Replacement(index);
            if (ShouldSnapshot(index, snapshotCadence))
                snapshot = builder.ToString();
        }

        return snapshot;
    }

    /// <summary>Measures run struct cursor carry cycle.</summary>
    internal static Rope<char> RunStructCursorCarryCycle(
        Rope<char> source,
        int startPosition,
        int snapshotCadence,
        int focusCapacity,
        int flushChunkSize,
        int localityWindow,
        int carryRunLength,
        bool verifyStructuralTransitions)
    {
        if (localityWindow <= 0)
            throw new ArgumentOutOfRangeException(nameof(localityWindow));
        if (snapshotCadence <= 0)
            throw new ArgumentOutOfRangeException(nameof(snapshotCadence));
        if (carryRunLength <= 0 || carryRunLength % snapshotCadence != 0)
            throw new ArgumentOutOfRangeException(nameof(carryRunLength));

        var cursor = source.GetStructCursorPrototype(startPosition, focusCapacity, flushChunkSize);
        var snapshot = source;
        var completedEdits = 0;
        var initial = verifyStructuralTransitions ? cursor.GetDiagnostics() : default;

        // Type to the right in adjacent runs. The eight-element round trip after every run makes
        // the locked locality window executable without changing the gap at the next edit.
        for (var index = 0; index < carryRunLength; index++)
        {
            cursor = cursor.Insert(CarryCharacter(index));
            completedEdits++;
            if (ShouldSnapshot(completedEdits - 1, snapshotCadence))
                snapshot = cursor.Snapshot();

            if ((index + 1) % localityWindow == 0)
            {
                for (var step = 0; step < localityWindow; step++)
                    cursor = cursor.MovePrevious();
                for (var step = 0; step < localityWindow; step++)
                    cursor = cursor.MoveNext();
            }
        }

        var afterLeftTyping = verifyStructuralTransitions ? cursor.GetDiagnostics() : default;

        // Backspace the complete typed run, forcing refill from the flushed left ordinary spine.
        for (var index = 0; index < carryRunLength; index++)
        {
            cursor = cursor.DeletePrevious();
            completedEdits++;
            if (ShouldSnapshot(completedEdits - 1, snapshotCadence))
                snapshot = cursor.Snapshot();
        }

        var beforeRightTyping = verifyStructuralTransitions ? cursor.GetDiagnostics() : default;

        // Keep the gap fixed while inserting, so the second run accumulates on the right. The
        // resulting forward-delete phase must refill from the flushed right ordinary spine.
        for (var index = 0; index < carryRunLength; index++)
        {
            cursor = cursor.Insert(CarryCharacter(carryRunLength + index));
            cursor = cursor.MovePrevious();
            completedEdits++;
            if (ShouldSnapshot(completedEdits - 1, snapshotCadence))
                snapshot = cursor.Snapshot();
        }

        var afterRightTyping = verifyStructuralTransitions ? cursor.GetDiagnostics() : default;

        for (var index = 0; index < carryRunLength; index++)
        {
            cursor = cursor.DeleteNext();
            completedEdits++;
            if (ShouldSnapshot(completedEdits - 1, snapshotCadence))
                snapshot = cursor.Snapshot();
        }

        if (verifyStructuralTransitions)
        {
            if (afterLeftTyping.LeftOrdinaryChunkCount <= initial.LeftOrdinaryChunkCount)
            {
                throw new InvalidOperationException(
                    "The carry workload did not flush a new ordinary chunk to the left spine.");
            }

            if (afterRightTyping.RightOrdinaryChunkCount <= beforeRightTyping.RightOrdinaryChunkCount)
            {
                throw new InvalidOperationException(
                    "The carry workload did not flush a new ordinary chunk to the right spine.");
            }

            if (cursor.Count != source.Count || cursor.Position != startPosition)
                throw new InvalidOperationException("The carry workload did not restore the source gap state.");
            if (!ReferenceEquals(snapshot, cursor.Snapshot()))
                throw new InvalidOperationException("The final cadence publication was not consumed.");
            if (!snapshot.SequenceEqual(source))
                throw new InvalidOperationException("The carry workload did not restore the source sequence.");
        }

        return snapshot;
    }

    private static RopeCursorPrototype<char> MoveTo(
        RopeCursorPrototype<char> cursor,
        int position,
        bool useAbsoluteSeek)
    {
        if (useAbsoluteSeek)
            return cursor.Seek(position);

        while (cursor.Position < position)
            cursor = cursor.MoveNext();
        while (cursor.Position > position)
            cursor = cursor.MovePrevious();
        return cursor;
    }

    private static RopeCursorStructPrototype<char> MoveTo(
        RopeCursorStructPrototype<char> cursor,
        int position,
        bool useAbsoluteSeek)
    {
        if (useAbsoluteSeek)
            return cursor.Seek(position);

        while (cursor.Position < position)
            cursor = cursor.MoveNext();
        while (cursor.Position > position)
            cursor = cursor.MovePrevious();
        return cursor;
    }

    private static void MoveTo(
        RopeMutableCursorPrototype<char> cursor,
        int position,
        bool useAbsoluteSeek)
    {
        if (useAbsoluteSeek)
        {
            cursor.Seek(position);
            return;
        }

        while (cursor.Position < position)
            cursor.MoveNext();
        while (cursor.Position > position)
            cursor.MovePrevious();
    }

    private static char Replacement(int index) => (char)('A' + index % 26);

    private static char CarryCharacter(int index) => (char)('!' + index % 90);

    private static bool ShouldSnapshot(int index, int cadence) =>
        (index + 1) % cadence == 0;
}
