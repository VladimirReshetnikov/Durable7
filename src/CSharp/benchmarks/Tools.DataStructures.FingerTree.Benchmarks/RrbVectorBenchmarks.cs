using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class RrbVectorBenchmarks
{
    private RrbVector<int> _vector = null!;
    private RrbVector<int> _suffix = null!;
    private Rope<int> _rope = null!;
    private Rope<int> _ropeSuffix = null!;
    private ImmutableList<int> _immutable = null!;
    private int _index;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        var items = Enumerable.Range(0, Count).ToArray();
        _vector = RrbVector<int>.CreateRange(items);
        _suffix = RrbVector<int>.CreateRange(items);
        _rope = Rope<int>.Create(items);
        _ropeSuffix = Rope<int>.Create(items);
        _immutable = items.ToImmutableList();
        _index = Count / 2;
    }

    [Benchmark(Baseline = true)]
    public int RrbMiddleIndex() => _vector[_index];

    [Benchmark]
    public int RopeMiddleIndex() => _rope[_index];

    [Benchmark]
    public int ImmutableListMiddleIndex() => _immutable[_index];

    [Benchmark]
    public RrbVector<int> RrbConcat() => _vector.Concat(_suffix);

    [Benchmark]
    public Rope<int> RopeConcat() => _rope.Concat(_ropeSuffix);

    [Benchmark]
    public ImmutableList<int> ImmutableListConcat() => _immutable.AddRange(_immutable);
}
