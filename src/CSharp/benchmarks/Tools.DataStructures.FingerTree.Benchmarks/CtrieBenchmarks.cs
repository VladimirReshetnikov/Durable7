using System.Collections.Concurrent;
using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class CtrieBenchmarks
{
    private ConcurrentHashTrie<int, int> _trie = null!;
    private ConcurrentDictionary<int, int> _dictionary = null!;
    private int _probe;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        var entries = Enumerable.Range(0, Count).Select(i => KeyValuePair.Create(i, i)).ToArray();
        _trie = ConcurrentHashTrie<int, int>.CreateRange(entries);
        _dictionary = new ConcurrentDictionary<int, int>(entries);
        _probe = Count / 2;
    }

    [Benchmark]
    public int CtrieLookup() => _trie[_probe];

    [Benchmark(Baseline = true)]
    public int ConcurrentDictionaryLookup() => _dictionary[_probe];

    [Benchmark]
    public ConcurrentHashTrie<int, int>.SnapshotView CtrieSnapshot() => _trie.Snapshot();

    [Benchmark]
    public ImmutableDictionary<int, int> ConcurrentDictionaryImmutableSnapshot() =>
        _dictionary.ToImmutableDictionary();
}
