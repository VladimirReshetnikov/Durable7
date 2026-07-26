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

    [Params(1_000, 100_000)]
    public int Count { get; set; }

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

    [Benchmark]
    public int ChampLookup() => _champ[_probe];

    [Benchmark(Baseline = true)]
    public int DictionaryLookup() => _dictionary[_probe];

    [Benchmark]
    public int ImmutableDictionaryLookup() => _immutable[_probe];

    [Benchmark]
    public long ChampIteration()
    {
        long sum = 0;
        foreach (var entry in _champ)
            sum += entry.Value;
        return sum;
    }

    [Benchmark]
    public long ImmutableDictionaryIteration()
    {
        long sum = 0;
        foreach (var entry in _immutable)
            sum += entry.Value;
        return sum;
    }

    [Benchmark]
    public bool ChampIndependentHistoryEquality() => _champ.MapEquals(_independentChamp);

    [Benchmark]
    public int ChampSharedSingleChangeDiff() => _champ.Diff(_changedChamp).Count();

    [Benchmark]
    public int ChampIndependentHistoryDiff() => _champ.Diff(_independentChamp).Count();
}
