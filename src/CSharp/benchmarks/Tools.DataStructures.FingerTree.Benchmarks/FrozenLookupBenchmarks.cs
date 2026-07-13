using System.Collections.Frozen;
using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class FrozenLookupBenchmarks
{
    private KeyValuePair<Axis2HashKey, int>[] _entries = null!;
    private Axis2HashKey[] _probes = null!;
    private PersistentHashMap<Axis2HashKey, int> _persistent = null!;
    private Dictionary<Axis2HashKey, int> _dictionary = null!;
    private ImmutableDictionary<Axis2HashKey, int> _immutable = null!;
    private FrozenDictionary<Axis2HashKey, int> _bclFrozen = null!;

    [Params(1, 8, 32, 1_024, 100_000)]
    public int Count { get; set; }

    [Params(0, 50, 100)]
    public int HitPercentage { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        _entries = Axis2BenchmarkPolicy.CreateHashEntries(Count, Axis2HashShape.Uniform);
        _probes = Axis2BenchmarkPolicy.CreateLookupProbes(1_024, HitPercentage, Axis2HashShape.Uniform);
        _persistent = PersistentHashMap<Axis2HashKey, int>.CreateRange(_entries, Axis2HashKeyComparer.Instance);
        _dictionary = new Dictionary<Axis2HashKey, int>(Count, Axis2HashKeyComparer.Instance);
        var immutable = ImmutableDictionary.CreateBuilder<Axis2HashKey, int>(Axis2HashKeyComparer.Instance);
        foreach (var entry in _entries)
        {
            _dictionary.Add(entry.Key, entry.Value);
            immutable.Add(entry.Key, entry.Value);
        }

        _immutable = immutable.ToImmutable();
        _bclFrozen = _dictionary.ToFrozenDictionary(Axis2HashKeyComparer.Instance);
    }

    [Benchmark]
    [BenchmarkCategory("LookupMix")]
    public long PersistentLookupMix() => SumLookups(_persistent);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("LookupMix")]
    public long DictionaryLookupMix() => SumLookups(_dictionary);

    [Benchmark]
    [BenchmarkCategory("LookupMix")]
    public long ImmutableLookupMix() => SumLookups(_immutable);

    [Benchmark]
    [BenchmarkCategory("LookupMix")]
    public long BclFrozenLookupMix() => SumLookups(_bclFrozen);

    [Benchmark]
    [BenchmarkCategory("Enumeration")]
    public long PersistentEnumeration() => SumEntries(_persistent);

    [Benchmark]
    [BenchmarkCategory("Enumeration")]
    public long DictionaryEnumeration() => SumEntries(_dictionary);

    [Benchmark]
    [BenchmarkCategory("Enumeration")]
    public long ImmutableEnumeration() => SumEntries(_immutable);

    [Benchmark]
    [BenchmarkCategory("Enumeration")]
    public long BclFrozenEnumeration() => SumEntries(_bclFrozen);

    private long SumLookups(IReadOnlyDictionary<Axis2HashKey, int> dictionary)
    {
        long sum = 0;
        foreach (var probe in _probes)
        {
            if (dictionary.TryGetValue(probe, out var value))
                sum += value;
        }

        return sum;
    }

    private static long SumEntries(IEnumerable<KeyValuePair<Axis2HashKey, int>> entries)
    {
        long sum = 0;
        foreach (var entry in entries)
            sum += entry.Value;
        return sum;
    }
}
