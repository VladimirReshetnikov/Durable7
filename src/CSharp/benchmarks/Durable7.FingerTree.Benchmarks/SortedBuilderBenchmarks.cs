// Benchmarks for the sorted builder.

using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Configs;
using FtSortedDictionary = Durable7.FingerTree.SortedDictionary<int, int>;
using FtSortedSet = Durable7.FingerTree.SortedSet<int>;
using BclImmutableSortedDictionary = System.Collections.Immutable.ImmutableSortedDictionary<int, int>;
using BclImmutableSortedSet = System.Collections.Immutable.ImmutableSortedSet<int>;

namespace Durable7.FingerTree.Benchmarks;

// Batch-edit construction paths for the mutable sorted builders. These benchmarks compare the new staging
// builders against repeated immutable edits, caller-side BCL staging followed by CreateRange, and the BCL
// immutable builders as an external reference.
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class SortedBuilderBenchmarks
{
    /// <summary>Gets the number of elements in the collection.</summary>
    [Params(1_000, 100_000)]
    public int Size;

    private int[] _baseItems = null!;
    private int[] _batchItems = null!;
    private KeyValuePair<int, int>[] _batchEntries = null!;
    private FtSortedSet _oursSet = null!;
    private BclImmutableSortedSet _immutableSet = null!;
    private FtSortedDictionary _oursDictionary = null!;
    private BclImmutableSortedDictionary _immutableDictionary = null!;

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
    [GlobalSetup]
    public void Setup()
    {
        _baseItems = Enumerable.Range(0, Size).Select(i => i * 2).ToArray();
        _batchItems = Enumerable.Range(0, Math.Max(1, Size / 10)).Select(i => (i * 2) + 1).ToArray();
        _batchEntries = _batchItems.Select(i => new KeyValuePair<int, int>(i, -i)).ToArray();
        _oursSet = FtSortedSet.CreateRange(_baseItems);
        _immutableSet = ImmutableSortedSet.CreateRange(_baseItems);
        _oursDictionary = FtSortedDictionary.CreateRange(_baseItems.Select(i => new KeyValuePair<int, int>(i, i)));
        _immutableDictionary = ImmutableSortedDictionary.CreateRange(_baseItems.Select(i => new KeyValuePair<int, int>(i, i)));
    }

    /// <summary>Measures ours set immutable add range.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("SortedSetBatch")]
    public FtSortedSet Ours_Set_ImmutableAddRange() => _oursSet.AddRange(_batchItems);

    /// <summary>Measures ours set builder union freeze.</summary>
    [Benchmark]
    [BenchmarkCategory("SortedSetBatch")]
    public FtSortedSet Ours_Set_BuilderUnionFreeze()
    {
        var builder = _oursSet.ToBuilder();
        builder.UnionWith(_batchItems);
        return builder.ToImmutable();
    }

    /// <summary>Measures ours set bcl stage then create range.</summary>
    [Benchmark]
    [BenchmarkCategory("SortedSetBatch")]
    public FtSortedSet Ours_Set_BclStageThenCreateRange()
    {
        var staged = new System.Collections.Generic.SortedSet<int>(_oursSet);
        staged.UnionWith(_batchItems);
        return FtSortedSet.CreateRange(staged);
    }

    /// <summary>Measures bcl set builder union freeze.</summary>
    [Benchmark]
    [BenchmarkCategory("SortedSetBatch")]
    public BclImmutableSortedSet Bcl_Set_BuilderUnionFreeze()
    {
        var builder = _immutableSet.ToBuilder();
        builder.UnionWith(_batchItems);
        return builder.ToImmutable();
    }

    /// <summary>Measures ours dictionary immutable set item loop.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("SortedDictionaryBatch")]
    public FtSortedDictionary Ours_Dictionary_ImmutableSetItemLoop()
    {
        var result = _oursDictionary;
        foreach (var entry in _batchEntries)
            result = result.SetItem(entry.Key, entry.Value);
        return result;
    }

    /// <summary>Measures ours dictionary builder set item freeze.</summary>
    [Benchmark]
    [BenchmarkCategory("SortedDictionaryBatch")]
    public FtSortedDictionary Ours_Dictionary_BuilderSetItemFreeze()
    {
        var builder = _oursDictionary.ToBuilder();
        foreach (var entry in _batchEntries)
            builder.SetItem(entry.Key, entry.Value);
        return builder.ToImmutable();
    }

    /// <summary>Measures ours dictionary bcl stage then create range.</summary>
    [Benchmark]
    [BenchmarkCategory("SortedDictionaryBatch")]
    public FtSortedDictionary Ours_Dictionary_BclStageThenCreateRange()
    {
        var staged = new System.Collections.Generic.SortedDictionary<int, int>();
        foreach (var entry in _oursDictionary)
            staged.Add(entry.Key, entry.Value);
        foreach (var entry in _batchEntries)
            staged[entry.Key] = entry.Value;
        return FtSortedDictionary.CreateRange(staged);
    }

    /// <summary>Measures bcl dictionary builder set item freeze.</summary>
    [Benchmark]
    [BenchmarkCategory("SortedDictionaryBatch")]
    public BclImmutableSortedDictionary Bcl_Dictionary_BuilderSetItemFreeze()
    {
        var builder = _immutableDictionary.ToBuilder();
        foreach (var entry in _batchEntries)
            builder[entry.Key] = entry.Value;
        return builder.ToImmutable();
    }
}
