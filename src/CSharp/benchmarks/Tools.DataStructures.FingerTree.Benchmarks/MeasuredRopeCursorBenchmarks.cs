using BenchmarkDotNet.Attributes;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class MeasuredRopeCursorBenchmarks
{
    private const int EditBurst = 32;

    private MeasuredRope<char, int, NewlineMeasure> _rope = null!;
    private int[] _positions = null!;
    private int _line;

    [Params(1_024, 65_536, 1_048_576)]
    public int DocumentSize { get; set; }

    [Params(16, 256)]
    public int SnapshotCadence { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        var data = Axis2BenchmarkPolicy.CreateText(DocumentSize);
        _rope = MeasuredRope<char, int, NewlineMeasure>.Create(data);
        _positions = Enumerable.Range(0, EditBurst)
            .Select(index => Math.Min(DocumentSize - 1, (DocumentSize / 2) + index))
            .ToArray();
        _line = _rope.Measure / 2;
    }

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("LocalEdit")]
    public MeasuredRope<char, int, NewlineMeasure> IndexedMeasuredRopeEditBurst()
    {
        var rope = _rope;
        for (var index = 0; index < _positions.Length; index++)
            rope = rope.SetItem(_positions[index], (char)('A' + index % 26));
        return rope;
    }

    [Benchmark]
    [BenchmarkCategory("MeasureSeek")]
    public int ExistingMeasureSeek() => _rope.LineStartOffset(_line);

    [Benchmark]
    [BenchmarkCategory("LineColumn")]
    public int ExistingLineColumnQuery()
    {
        var position = DocumentSize / 2;
        var line = _rope.LineOfOffset(position);
        return position - _rope.LineStartOffset(line);
    }
}
