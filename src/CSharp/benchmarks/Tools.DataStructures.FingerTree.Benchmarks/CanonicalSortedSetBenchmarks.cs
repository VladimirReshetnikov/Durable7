using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class CanonicalSortedSetBenchmarks
{
    private CanonicalSortedSet<int> _canonical = null!;
    private CanonicalSortedSet<int> _independent = null!;
    private CanonicalSortedSet<int> _changed = null!;
    private ZipTreeRankPolicy<int> _policy = null!;
    private ZipTreeRankPolicy<int> _collidingPolicy = null!;
    private ImmutableSortedSet<int> _immutable = null!;
    private ImmutableSortedSet<int> _independentImmutable = null!;
    private int[] _items = null!;
    private int _probe;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        _policy = ZipTreeRankPolicy<int>.Create(seed: 0xfeedfacecafebeefUL);
        _collidingPolicy = ZipTreeRankPolicy<int>.Create(rankHash: _ => 0, seed: 1);
        _items = Enumerable.Range(0, Count).ToArray();
        _canonical = CanonicalSortedSet<int>.CreateRange(_items, _policy);
        _independent = CanonicalSortedSet<int>.CreateRange(_items.Reverse(), _policy);
        _changed = _canonical.Remove(Count / 2).Add(-1);
        _immutable = _items.ToImmutableSortedSet();
        _independentImmutable = _items.Reverse().ToImmutableSortedSet();
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

    [Benchmark]
    public bool CanonicalDigestInequality() => _canonical.SetEquals(_changed);

    [Benchmark]
    public CanonicalSortedSet<int> CanonicalPersistentInsert() => _canonical.Add(Count);

    [Benchmark]
    public CanonicalSortedSet<int> CanonicalPersistentRemove() => _canonical.Remove(_probe);

    [Benchmark]
    public ImmutableSortedSet<int> ImmutablePersistentInsert() => _immutable.Add(Count);

    [Benchmark]
    public ImmutableSortedSet<int> ImmutablePersistentRemove() => _immutable.Remove(_probe);

    [Benchmark]
    public CanonicalSortedSet<int> CanonicalBulkBuild() =>
        CanonicalSortedSet<int>.CreateRange(_items, _policy);

    [Benchmark]
    public CanonicalSortedSet<int> CanonicalFullyCollidingBulkBuild() =>
        CanonicalSortedSet<int>.CreateRange(_items, _collidingPolicy);

    [Benchmark]
    public CanonicalSortedSetStatistics CanonicalValidateStructure() => _canonical.ValidateStructure();
}
