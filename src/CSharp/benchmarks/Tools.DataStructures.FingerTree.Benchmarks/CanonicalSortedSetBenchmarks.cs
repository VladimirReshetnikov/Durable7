using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class CanonicalSortedSetBenchmarks
{
    private CanonicalSortedSet<int> _canonical = null!;
    private CanonicalSortedSet<int> _independent = null!;
    private ImmutableSortedSet<int> _immutable = null!;
    private ImmutableSortedSet<int> _independentImmutable = null!;
    private int _probe;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        var policy = ZipTreeRankPolicy<int>.Create(seed: 0xfeedfacecafebeefUL);
        var items = Enumerable.Range(0, Count).ToArray();
        _canonical = CanonicalSortedSet<int>.CreateRange(items, policy);
        _independent = CanonicalSortedSet<int>.CreateRange(items.Reverse(), policy);
        _immutable = items.ToImmutableSortedSet();
        _independentImmutable = items.Reverse().ToImmutableSortedSet();
        _probe = Count / 2;
    }

    [Benchmark]
    public bool CanonicalContains() => _canonical.Contains(_probe);

    [Benchmark]
    public bool ImmutableContains() => _immutable.Contains(_probe);

    [Benchmark(Baseline = true)]
    public bool CanonicalIndependentHistoryEquality() => _canonical.SetEquals(_independent);

    [Benchmark]
    public bool ImmutableIndependentHistoryEquality() => _immutable.SetEquals(_independentImmutable);

    [Benchmark]
    public ulong CanonicalMemoizedDigest() => _canonical.ContentHash;
}
