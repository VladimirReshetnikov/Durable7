using System.Text;
using BenchmarkDotNet.Attributes;

namespace Tools.DataStructures.FingerTree.Benchmarks;

/// <summary>
/// Compares the C0 zipper representations with indexed immutable editing and a mutable text
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

    /// <summary>Gets or sets the active focus capacity of each C0 zipper prototype.</summary>
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

    /// <summary>Runs the replacement history through the immutable class zipper.</summary>
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

    /// <summary>Runs the replacement history through the readonly-struct zipper.</summary>
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

    /// <summary>Runs the replacement history through the mutable-session zipper control.</summary>
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

internal static class RopeCursorBenchmarkWorkload
{
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

    private static bool ShouldSnapshot(int index, int cadence) =>
        (index + 1) % cadence == 0;
}
