using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;

namespace Durable7.FingerTree.Benchmarks;

// Persistence robustness, made measurable. BenchmarkDotNet invokes each method many times against the SAME
// instance built once in [GlobalSetup] -- it operates on a RETAINED version over and over, the exact branching
// (fully persistent) pattern the lazy-memoized spine exists for. Because the general measured tree memoizes its
// deferred spine repairs, an endpoint operation on a retained version is O(1) amortized, so its time is flat
// across sizes from 100 to 1,000,000; a strict representation without a memoized lazy spine would re-pay an
// O(log n) cascade on every branch off the retained version. The sorted collections are built on the same core
// and inherit the behavior (their persistent Add stays O(log n) on a retained version, matching the BCL).
[MemoryDiagnoser]
public class PersistenceBenchmarks
{
    [Params(100, 10_000, 1_000_000)]
    public int Size;

    private FingerTree<int, int, SizeMeasure<int>> _tree = null!;
    private SortedSet<int> _ours = null!;
    private ImmutableSortedSet<int> _immutable = null!;

    [GlobalSetup]
    public void Setup()
    {
        var items = Enumerable.Range(0, Size).ToArray();
        _tree = FingerTree<int, int, SizeMeasure<int>>.CreateRange(items);
        _ = _tree.Measure;   // force and memoize the spine once, as a long-lived retained version would be
        _ours = SortedSet<int>.CreateRange(items);
        _immutable = ImmutableSortedSet.CreateRange(items);
    }

    // O(1) amortized on a retained version: each call branches off the same retained _tree, so its time should
    // be flat across all sizes -- the persistence-robustness signal.
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Prepend")]
    public FingerTree<int, int, SizeMeasure<int>> MeasuredTree_PrependRetained() => _tree.Prepend(-1);

    [Benchmark]
    [BenchmarkCategory("Append")]
    public FingerTree<int, int, SizeMeasure<int>> MeasuredTree_AppendRetained() => _tree.Append(Size);

    // Derived-collection inheritance: SortedSet (built on the measured core) keeps an O(log n) persistent Add on
    // a retained version, on par with the BCL's persistent sorted set.
    [Benchmark]
    [BenchmarkCategory("SetAdd")]
    public SortedSet<int> Ours_SetAddRetained() => _ours.Add(-1);

    [Benchmark]
    [BenchmarkCategory("SetAdd")]
    public ImmutableSortedSet<int> Immutable_SetAddRetained() => _immutable.Add(-1);
}
