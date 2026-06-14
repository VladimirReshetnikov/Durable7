using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using FtSortedSet = Tools.DataStructures.FingerTree.SortedSet<int>;
using BclImmutableSortedSet = System.Collections.Immutable.ImmutableSortedSet<int>;

namespace Tools.DataStructures.FingerTree.Benchmarks;

// Sorted-set operations versus the persistent BCL ImmutableSortedSet. Contains, Add, and order-statistic
// rank-select are all O(log n) on both: ImmutableSortedSet implements IReadOnlyList<T>, so its indexer is an
// O(log n) rank-select too — this measures whether the finger-tree SortedSet is competitive with the mature
// BCL collection on the operations they share (it is, within a small constant factor). The aliases keep our
// SortedSet distinct from System.Collections.Generic.SortedSet.
[MemoryDiagnoser]
public class SortedSetBenchmarks
{
    [Params(1_000, 100_000)]
    public int Size;

    private FtSortedSet _ours = null!;
    private BclImmutableSortedSet _immutable = null!;
    private int _probe;
    private int _rank;

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

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Contains")]
    public bool Ours_Contains() => _ours.Contains(_probe);

    [Benchmark]
    [BenchmarkCategory("Contains")]
    public bool Immutable_Contains() => _immutable.Contains(_probe);

    [Benchmark]
    [BenchmarkCategory("Add")]
    public FtSortedSet Ours_Add() => _ours.Add(-1);

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
