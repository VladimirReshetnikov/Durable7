// Benchmarks for the RRB vector.

using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Durable7.FingerTree;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>Benchmarks for the relaxed radix-balanced vector.</summary>
[MemoryDiagnoser]
public class RrbVectorBenchmarks
{
    private RrbVector<int> _vector = null!;
    private RrbVector<int> _suffix = null!;
    private Rope<int> _rope = null!;
    private Rope<int> _ropeSuffix = null!;
    private ImmutableList<int> _immutable = null!;
    private int _index;

    /// <summary>Gets the number of elements in the vector.</summary>
    [Params(1_000, 100_000)]
    public int Count { get; set; }

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
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

    /// <summary>Measures rrb middle index.</summary>
    [Benchmark(Baseline = true)]
    public int RrbMiddleIndex() => _vector[_index];

    /// <summary>Measures rope middle index.</summary>
    [Benchmark]
    public int RopeMiddleIndex() => _rope[_index];

    /// <summary>Measures immutable list middle index.</summary>
    [Benchmark]
    public int ImmutableListMiddleIndex() => _immutable[_index];

    /// <summary>Measures rrb concat.</summary>
    [Benchmark]
    public RrbVector<int> RrbConcat() => _vector.Concat(_suffix);

    /// <summary>Measures rope concat.</summary>
    [Benchmark]
    public Rope<int> RopeConcat() => _rope.Concat(_ropeSuffix);

    /// <summary>Measures immutable list concat.</summary>
    [Benchmark]
    public ImmutableList<int> ImmutableListConcat() => _immutable.AddRange(_immutable);
}
