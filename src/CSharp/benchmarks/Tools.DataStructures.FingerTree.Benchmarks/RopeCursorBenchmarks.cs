using System.Text;
using BenchmarkDotNet.Attributes;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class RopeCursorBenchmarks
{
    private const int EditBurst = 32;

    private Rope<char> _rope = null!;
    private string _text = null!;
    private int[] _positions = null!;

    [Params(1_024, 65_536, 1_048_576)]
    public int DocumentSize { get; set; }

    [Params(1, 8, 256, int.MaxValue)]
    public int LocalityWindow { get; set; }

    [Params(1, 16, 256)]
    public int SnapshotCadence { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        var data = Axis2BenchmarkPolicy.CreateText(DocumentSize);
        _rope = Rope<char>.Create(data);
        _text = new string(data);
        _positions = CreatePositions(DocumentSize, LocalityWindow, EditBurst);
    }

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("LocalEdit")]
    public Rope<char> IndexedRopeEditBurst()
    {
        var rope = _rope;
        for (var index = 0; index < _positions.Length; index++)
            rope = rope.SetItem(_positions[index], (char)('A' + index % 26));
        return rope;
    }

    [Benchmark]
    [BenchmarkCategory("MutableControl")]
    public string StringBuilderEditBurst()
    {
        var builder = new StringBuilder(_text);
        for (var index = 0; index < _positions.Length; index++)
            builder[_positions[index]] = (char)('A' + index % 26);
        return builder.ToString();
    }

    private static int[] CreatePositions(int size, int localityWindow, int count)
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
}
