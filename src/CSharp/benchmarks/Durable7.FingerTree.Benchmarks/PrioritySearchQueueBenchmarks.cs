// Benchmarks for the priority search queue.

using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Configs;
using Durable7.FingerTree;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>Benchmarks for the persistent priority search queue.</summary>
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class PrioritySearchQueueBenchmarks
{
    private PrioritySearchQueue<int, int, int> _queue = null!;
    private System.Collections.Generic.SortedDictionary<int, (int Priority, int Value)> _dictionary = null!;
    private int _middle;
    private int _minimumKey;
    private int _maximumKey;
    private int _sparsePriorityThreshold;
    private int _densePriorityThreshold;

    /// <summary>Gets the number of entries in the queue.</summary>
    [Params(1_000, 100_000)]
    public int Count { get; set; }

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var random = new Random(42);
        var entries = Enumerable.Range(0, Count)
            .Select(key => new PrioritySearchEntry<int, int, int>(key, random.Next(Count), key))
            .ToArray();
        _queue = PrioritySearchQueue<int, int, int>.CreateRange(entries);
        _dictionary = new System.Collections.Generic.SortedDictionary<int, (int, int)>(entries.ToDictionary(
            entry => entry.Key,
            entry => (entry.Priority, entry.Value)));
        _middle = Count / 2;
        _minimumKey = Count / 4;
        _maximumKey = Count * 3 / 4;
        _sparsePriorityThreshold = Count / 100;
        _densePriorityThreshold = Count - 1;
    }

    /// <summary>Measures priority search lookup.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Lookup")]
    public bool PrioritySearchLookup() => _queue.TryGetEntry(_middle, out _);

    /// <summary>Looks up in the framework's sorted dictionary, as an ordered baseline.</summary>
    [Benchmark]
    [BenchmarkCategory("Lookup")]
    public bool SortedDictionaryLookup() => _dictionary.ContainsKey(_middle);

    /// <summary>Measures priority search set existing item.</summary>
    [Benchmark]
    [BenchmarkCategory("SetItem")]
    public PrioritySearchQueue<int, int, int> PrioritySearchSetExistingItem() =>
        _queue.SetItem(_middle, priority: -1, value: -_middle);

    /// <summary>Measures priority search delete minimum.</summary>
    [Benchmark]
    [BenchmarkCategory("DeleteMinimum")]
    public PrioritySearchQueue<int, int, int> PrioritySearchDeleteMinimum() =>
        _queue.DeleteMinimum(out _);

    /// <summary>Measures priority search fully pruned threshold query.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("ThresholdEmpty")]
    public int PrioritySearchFullyPrunedThresholdQuery() =>
        _queue.EnumerateAtMost(_minimumKey, _maximumKey, maximumPriority: -1).Count();

    /// <summary>Measures sorted dictionary empty threshold scan.</summary>
    [Benchmark]
    [BenchmarkCategory("ThresholdEmpty")]
    public int SortedDictionaryEmptyThresholdScan() => CountDictionaryEntriesAtMost(maximumPriority: -1);

    /// <summary>Measures priority search sparse threshold query.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("ThresholdSparse")]
    public int PrioritySearchSparseThresholdQuery() =>
        _queue.EnumerateAtMost(_minimumKey, _maximumKey, _sparsePriorityThreshold).Count();

    /// <summary>Measures sorted dictionary sparse threshold scan.</summary>
    [Benchmark]
    [BenchmarkCategory("ThresholdSparse")]
    public int SortedDictionarySparseThresholdScan() => CountDictionaryEntriesAtMost(_sparsePriorityThreshold);

    /// <summary>Measures priority search dense threshold query.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("ThresholdDense")]
    public int PrioritySearchDenseThresholdQuery() =>
        _queue.EnumerateAtMost(_minimumKey, _maximumKey, _densePriorityThreshold).Count();

    /// <summary>Measures sorted dictionary dense threshold scan.</summary>
    [Benchmark]
    [BenchmarkCategory("ThresholdDense")]
    public int SortedDictionaryDenseThresholdScan() => CountDictionaryEntriesAtMost(_densePriorityThreshold);

    /// <summary>Measures priority search validate structure.</summary>
    [Benchmark]
    [BenchmarkCategory("Validate")]
    public PrioritySearchQueueStatistics PrioritySearchValidateStructure() => _queue.ValidateStructure();

    private int CountDictionaryEntriesAtMost(int maximumPriority)
    {
        var matches = 0;
        foreach (var pair in _dictionary)
        {
            if (pair.Key < _minimumKey)
                continue;
            if (pair.Key > _maximumKey)
                break;
            if (pair.Value.Priority <= maximumPriority)
                matches++;
        }

        return matches;
    }
}
