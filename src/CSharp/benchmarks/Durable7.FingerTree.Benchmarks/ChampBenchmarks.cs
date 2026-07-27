// Benchmarks for the CHAMP trie.

using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Durable7.Hamt;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>Benchmarks for the CHAMP-backed persistent hash map and set.</summary>
[MemoryDiagnoser]
public class ChampBenchmarks
{
    private PersistentHashMap<int, int> _champ = null!;
    private PersistentHashMap<int, int> _independentChamp = null!;
    private PersistentHashMap<int, int> _changedChamp = null!;
    private ImmutableDictionary<int, int> _immutable = null!;
    private Dictionary<int, int> _dictionary = null!;
    private int _probe;

    /// <summary>Gets the number of elements in the collection.</summary>
    [Params(1_000, 100_000)]
    public int Count { get; set; }

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var entries = Enumerable.Range(0, Count).Select(i => KeyValuePair.Create(i, i * 17)).ToArray();
        _champ = PersistentHashMap<int, int>.CreateRange(entries);
        _independentChamp = PersistentHashMap<int, int>.CreateRange(entries.Reverse());
        _probe = Count * 3 / 4;
        _changedChamp = _champ.SetItem(_probe, -1);
        _immutable = entries.ToImmutableDictionary();
        _dictionary = entries.ToDictionary();
    }

    /// <summary>Looks up in the CHAMP-backed map, for comparison against the other representations.</summary>
    [Benchmark]
    public int ChampLookup() => _champ[_probe];

    /// <summary>Measures dictionary lookup.</summary>
    [Benchmark(Baseline = true)]
    public int DictionaryLookup() => _dictionary[_probe];

    /// <summary>
    /// Looks up in the framework's immutable dictionary, as the baseline the persistent map is compared against.
    /// </summary>
    [Benchmark]
    public int ImmutableDictionaryLookup() => _immutable[_probe];

    /// <summary>Measures champ iteration.</summary>
    [Benchmark]
    public long ChampIteration()
    {
        long sum = 0;
        foreach (var entry in _champ)
            sum += entry.Value;
        return sum;
    }

    /// <summary>Measures immutable dictionary iteration.</summary>
    [Benchmark]
    public long ImmutableDictionaryIteration()
    {
        long sum = 0;
        foreach (var entry in _immutable)
            sum += entry.Value;
        return sum;
    }

    /// <summary>Measures champ independent history equality.</summary>
    [Benchmark]
    public bool ChampIndependentHistoryEquality() => _champ.MapEquals(_independentChamp);

    /// <summary>Measures champ shared single change diff.</summary>
    [Benchmark]
    public int ChampSharedSingleChangeDiff() => _champ.Diff(_changedChamp).Count();

    /// <summary>Measures champ independent history diff.</summary>
    [Benchmark]
    public int ChampIndependentHistoryDiff() => _champ.Diff(_independentChamp).Count();
}
