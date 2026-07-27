// Benchmarks for the concurrent hash trie.

using System.Collections.Concurrent;
using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Durable7.Hamt;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>Benchmarks for the concurrent hash trie.</summary>
[MemoryDiagnoser]
public class CtrieBenchmarks
{
    private ConcurrentHashTrie<int, int> _trie = null!;
    private ConcurrentDictionary<int, int> _dictionary = null!;
    private int _probe;

    /// <summary>Gets the number of elements in the collection.</summary>
    [Params(1_000, 100_000)]
    public int Count { get; set; }

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var entries = Enumerable.Range(0, Count).Select(i => KeyValuePair.Create(i, i)).ToArray();
        _trie = ConcurrentHashTrie<int, int>.CreateRange(entries);
        _dictionary = new ConcurrentDictionary<int, int>(entries);
        _probe = Count / 2;
    }

    /// <summary>Measures ctrie lookup.</summary>
    [Benchmark]
    public int CtrieLookup() => _trie[_probe];

    /// <summary>Measures concurrent dictionary lookup.</summary>
    [Benchmark(Baseline = true)]
    public int ConcurrentDictionaryLookup() => _dictionary[_probe];

    /// <summary>Measures ctrie snapshot.</summary>
    [Benchmark]
    public ConcurrentHashTrie<int, int>.SnapshotView CtrieSnapshot() => _trie.Snapshot();

    /// <summary>Measures concurrent dictionary immutable snapshot.</summary>
    [Benchmark]
    public ImmutableDictionary<int, int> ConcurrentDictionaryImmutableSnapshot() =>
        _dictionary.ToImmutableDictionary();
}
