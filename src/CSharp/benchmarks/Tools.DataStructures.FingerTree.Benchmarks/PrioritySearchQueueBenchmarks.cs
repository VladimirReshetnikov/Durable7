using BenchmarkDotNet.Attributes;
using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class PrioritySearchQueueBenchmarks
{
    private PrioritySearchQueue<int, int, int> _queue = null!;
    private System.Collections.Generic.SortedDictionary<int, (int Priority, int Value)> _dictionary = null!;
    private int _middle;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

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
    }

    [Benchmark(Baseline = true)]
    public bool PrioritySearchLookup() => _queue.TryGetEntry(_middle, out _);

    [Benchmark]
    public bool SortedDictionaryLookup() => _dictionary.ContainsKey(_middle);

    [Benchmark]
    public PrioritySearchQueue<int, int, int> PrioritySearchDeleteMinimum() =>
        _queue.DeleteMinimum(out _);

    [Benchmark]
    public int PrioritySearchRangeThreshold() =>
        _queue.EnumerateAtMost(Count / 4, Count * 3 / 4, Count / 100).Count();

    [Benchmark]
    public int DictionaryRangeThresholdScan() =>
        _dictionary.Count(pair => pair.Key >= Count / 4 && pair.Key <= Count * 3 / 4 && pair.Value.Priority <= Count / 100);
}
