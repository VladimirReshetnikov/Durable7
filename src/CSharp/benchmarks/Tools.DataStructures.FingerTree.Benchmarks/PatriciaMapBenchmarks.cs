using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class PatriciaMapBenchmarks
{
    private PersistentIntMap<int> _patricia = null!;
    private PersistentIntMap<int> _patriciaDelta = null!;
    private PersistentHashMap<int, int> _champ = null!;
    private ImmutableDictionary<int, int> _immutable = null!;
    private int _probe;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

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

    [Benchmark(Baseline = true)]
    public int PatriciaLookup() => _patricia[_probe];

    [Benchmark]
    public int ChampLookup() => _champ[_probe];

    [Benchmark]
    public int ImmutableDictionaryLookup() => _immutable[_probe];

    [Benchmark]
    public PersistentIntMap<int> PatriciaStructuralUnion() => _patricia.Union(_patriciaDelta);
}
