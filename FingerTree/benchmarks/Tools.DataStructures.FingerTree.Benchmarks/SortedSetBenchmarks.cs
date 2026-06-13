using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using FtSortedSet = Tools.DataStructures.FingerTree.SortedSet<int>;
using BclImmutableSortedSet = System.Collections.Immutable.ImmutableSortedSet<int>;

namespace Tools.DataStructures.FingerTree.Benchmarks;

// Sorted-set operations versus the persistent BCL ImmutableSortedSet. Contains and Add are O(log n) for both;
// the distinguishing capability is order-statistic indexing — the finger-tree SortedSet returns the k-th
// element in O(log n) via its cached size measure, whereas ImmutableSortedSet has no indexer and a rank query
// must enumerate (O(k)). The aliases keep our SortedSet distinct from System.Collections.Generic.SortedSet.
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

    // O(log n): the headline order-statistic capability — the k-th smallest element by rank.
    [Benchmark]
    [BenchmarkCategory("RankSelect")]
    public int Ours_SelectByRank() => _ours[_rank];

    // O(k): ImmutableSortedSet has no indexer, so a rank query must skip through the ordered enumeration.
    [Benchmark]
    [BenchmarkCategory("RankSelect")]
    public int Immutable_SelectByRank() => _immutable.Skip(_rank).First();
}
