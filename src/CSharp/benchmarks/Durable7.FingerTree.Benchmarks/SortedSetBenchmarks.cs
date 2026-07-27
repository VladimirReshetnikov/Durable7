// Benchmarks for the sorted set.

using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using FtSortedSet = Durable7.FingerTree.SortedSet<int>;
using BclImmutableSortedSet = System.Collections.Immutable.ImmutableSortedSet<int>;

namespace Durable7.FingerTree.Benchmarks;

// Sorted-set operations versus the persistent BCL ImmutableSortedSet. Contains, Add, and order-statistic
// rank-select are all O(log n) on both: ImmutableSortedSet implements IReadOnlyList<T>, so its indexer is an
// O(log n) rank-select too — this measures whether the finger-tree SortedSet is competitive with the mature
// BCL collection on the operations they share (it is, within a small constant factor). The aliases keep our
// SortedSet distinct from System.Collections.Generic.SortedSet.
[MemoryDiagnoser]
public class SortedSetBenchmarks
{
    /// <summary>Gets the number of elements in the set.</summary>
    [Params(1_000, 100_000)]
    public int Size;

    private FtSortedSet _ours = null!;
    private BclImmutableSortedSet _immutable = null!;
    private int _probe;
    private int _rank;

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
    [GlobalSetup]
    public void Setup()
    {
        // Shuffle so construction is not pre-sorted and lookups hit varied paths.
        var items = Enumerable.Range(0, Size).OrderBy(x => unchecked(x * 2654435761u)).ToArray();
        _ours = FtSortedSet.CreateRange(items);
        _immutable = ImmutableSortedSet.CreateRange(items);
        _probe = Size / 2;
        _rank = Size / 2;
    }

    /// <summary>Measures ours contains.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Contains")]
    public bool Ours_Contains() => _ours.Contains(_probe);

    /// <summary>Measures immutable contains.</summary>
    [Benchmark]
    [BenchmarkCategory("Contains")]
    public bool Immutable_Contains() => _immutable.Contains(_probe);

    /// <summary>Measures ours add.</summary>
    [Benchmark]
    [BenchmarkCategory("Add")]
    public FtSortedSet Ours_Add() => _ours.Add(-1);

    /// <summary>Measures immutable add.</summary>
    [Benchmark]
    [BenchmarkCategory("Add")]
    public BclImmutableSortedSet Immutable_Add() => _immutable.Add(-1);

    // O(log n): order-statistic rank-select via the cached size measure (the k-th smallest element).
    [Benchmark]
    [BenchmarkCategory("RankSelect")]
    public int Ours_SelectByRank() => _ours[_rank];

    // O(log n): ImmutableSortedSet's IReadOnlyList indexer is also a rank-select — the fair comparison.
    [Benchmark]
    [BenchmarkCategory("RankSelect")]
    public int Immutable_SelectByRank() => _immutable[_rank];
}
