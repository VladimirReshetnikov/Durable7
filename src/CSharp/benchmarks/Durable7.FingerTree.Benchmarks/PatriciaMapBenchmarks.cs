// Benchmarks for the patricia map.

using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Durable7.Hamt;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>Benchmarks for the persistent Patricia maps and sets.</summary>
[MemoryDiagnoser]
public class PatriciaMapBenchmarks
{
    private PersistentIntMap<int> _patricia = null!;
    private PersistentIntMap<int> _patriciaDelta = null!;
    private PersistentHashMap<int, int> _champ = null!;
    private ImmutableDictionary<int, int> _immutable = null!;
    private int _probe;

    /// <summary>Gets the number of entries in the map.</summary>
    [Params(1_000, 100_000)]
    public int Count { get; set; }

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var entries = Enumerable.Range(0, Count).Select(i => KeyValuePair.Create(i * 2, i)).ToArray();
        var delta = Enumerable.Range(Count / 2, Count).Select(i => KeyValuePair.Create(i * 2, -i));
        _patricia = PersistentIntMap<int>.CreateRange(entries);
        _patriciaDelta = PersistentIntMap<int>.CreateRange(delta);
        _champ = PersistentHashMap<int, int>.CreateRange(entries);
        _immutable = entries.ToImmutableDictionary();
        _probe = (Count * 3 / 4) * 2;
    }

    /// <summary>Measures patricia lookup.</summary>
    [Benchmark(Baseline = true)]
    public int PatriciaLookup() => _patricia[_probe];

    /// <summary>Looks up in the CHAMP-backed map, for comparison against the other representations.</summary>
    [Benchmark]
    public int ChampLookup() => _champ[_probe];

    /// <summary>
    /// Looks up in the framework's immutable dictionary, as the baseline the persistent map is compared against.
    /// </summary>
    [Benchmark]
    public int ImmutableDictionaryLookup() => _immutable[_probe];

    /// <summary>Measures patricia structural union.</summary>
    [Benchmark]
    public PersistentIntMap<int> PatriciaStructuralUnion() => _patricia.Union(_patriciaDelta);
}
