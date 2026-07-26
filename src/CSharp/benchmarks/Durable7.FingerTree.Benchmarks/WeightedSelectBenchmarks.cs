// Benchmarks for the weighted select.

using BenchmarkDotNet.Attributes;

namespace Durable7.FingerTree.Benchmarks;

// Cumulative-weight (Fenwick) selection: the sum-measured tree locates the element whose cumulative-weight
// interval contains a target in O(log n) via the cached running sum, where the obvious alternative is an O(n)
// prefix-sum scan. Holding the query at the midpoint of the total weight (a true worst case for the scan, which
// must walk to the middle), the tree split stays flat while the scan grows linearly with Size. Weights are
// `long` so the running total of the larger sizes cannot overflow.
[MemoryDiagnoser]
public class WeightedSelectBenchmarks
{
    [Params(1_000, 100_000)]
    public int Size;

    private FingerTree<long, long, SumMeasure<long>> _tree = null!;
    private long[] _weights = null!;
    private long _target;

    [GlobalSetup]
    public void Setup()
    {
        _weights = Enumerable.Range(1, Size).Select(i => (long)i).ToArray();   // strictly positive weights
        _tree = FingerTree<long, long, SumMeasure<long>>.CreateRange(_weights);
        _target = _weights.Sum() / 2;                                          // midpoint: worst case for a linear scan
    }

    // O(log n): descend on the cached running sum.
    [Benchmark(Baseline = true)]
    public long Tree_WeightedSelect() =>
        _tree.TrySelectByCumulativeWeight(_target, out var selected, out _) ? selected : -1;

    // O(n): accumulate a prefix sum until it exceeds the target.
    [Benchmark]
    public int Naive_PrefixScan()
    {
        var running = 0L;
        for (var i = 0; i < _weights.Length; i++)
        {
            running += _weights[i];
            if (running > _target)
                return i;
        }

        return -1;
    }
}
