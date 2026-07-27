// Benchmarks for the canonical sorted set.

using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Durable7.FingerTree;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>Benchmarks for the canonical sorted set.</summary>
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

    /// <summary>Gets the number of elements in the set.</summary>
    [Params(1_000, 100_000)]
    public int Count { get; set; }

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
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

    /// <summary>Measures canonical contains.</summary>
    [Benchmark]
    public bool CanonicalContains() => _canonical.Contains(_probe);

    /// <summary>Measures immutable contains.</summary>
    [Benchmark]
    public bool ImmutableContains() => _immutable.Contains(_probe);

    /// <summary>Measures canonical independent history equality.</summary>
    [Benchmark(Baseline = true)]
    public bool CanonicalIndependentHistoryEquality() => _canonical.SetEquals(_independent);

    /// <summary>Measures immutable independent history equality.</summary>
    [Benchmark]
    public bool ImmutableIndependentHistoryEquality() => _immutable.SetEquals(_independentImmutable);

    /// <summary>Measures canonical memoized digest.</summary>
    [Benchmark]
    public ulong CanonicalMemoizedDigest() => _canonical.ContentHash;

    /// <summary>Measures canonical digest inequality.</summary>
    [Benchmark]
    public bool CanonicalDigestInequality() => _canonical.SetEquals(_changed);

    /// <summary>Measures canonical persistent insert.</summary>
    [Benchmark]
    public CanonicalSortedSet<int> CanonicalPersistentInsert() => _canonical.Add(Count);

    /// <summary>Measures canonical persistent remove.</summary>
    [Benchmark]
    public CanonicalSortedSet<int> CanonicalPersistentRemove() => _canonical.Remove(_probe);

    /// <summary>Measures immutable persistent insert.</summary>
    [Benchmark]
    public ImmutableSortedSet<int> ImmutablePersistentInsert() => _immutable.Add(Count);

    /// <summary>Measures immutable persistent remove.</summary>
    [Benchmark]
    public ImmutableSortedSet<int> ImmutablePersistentRemove() => _immutable.Remove(_probe);

    /// <summary>Measures canonical bulk build.</summary>
    [Benchmark]
    public CanonicalSortedSet<int> CanonicalBulkBuild() =>
        CanonicalSortedSet<int>.CreateRange(_items, _policy);

    /// <summary>Measures canonical fully colliding bulk build.</summary>
    [Benchmark]
    public CanonicalSortedSet<int> CanonicalFullyCollidingBulkBuild() =>
        CanonicalSortedSet<int>.CreateRange(_items, _collidingPolicy);

    /// <summary>Measures canonical validate structure.</summary>
    [Benchmark]
    public CanonicalSortedSetStatistics CanonicalValidateStructure() => _canonical.ValidateStructure();
}
