using System.Collections.Frozen;
using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Configs;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

/// <summary>Shared benchmark surface for fixed-semantics Axis 2 frozen controls.</summary>
public abstract class FrozenF0AxisBenchmarksBase
{
    private FrozenF0AxisFixture _fixture = null!;

    [Params(0, 50, 100)]
    public int HitPercentage { get; set; }

    private protected void Initialize(int count, Axis2HashShape shape, string lane) =>
        _fixture = new FrozenF0AxisFixture(count, HitPercentage, shape, lane);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("LookupMix")]
    public long PersistentLookupMix() =>
        FrozenF0Operations.SumLookups(_fixture.Persistent, _fixture.Probes);

    [Benchmark]
    [BenchmarkCategory("LookupMix")]
    public long PackedPrototypeLookupMix() =>
        FrozenF0Operations.SumLookups(_fixture.Packed, _fixture.Probes);

    [Benchmark]
    [BenchmarkCategory("LookupMix")]
    public long DictionaryLookupMix() =>
        FrozenF0Operations.SumLookups(_fixture.Dictionary, _fixture.Probes);

    [Benchmark]
    [BenchmarkCategory("LookupMix")]
    public long ImmutableLookupMix() =>
        FrozenF0Operations.SumLookups(_fixture.Immutable, _fixture.Probes);

    [Benchmark]
    [BenchmarkCategory("LookupMix")]
    public long BclFrozenLookupMix() =>
        FrozenF0Operations.SumLookups(_fixture.BclFrozen, _fixture.Probes);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Enumeration")]
    public long PersistentEnumeration() => FrozenF0Operations.SumEntries(_fixture.Persistent);

    [Benchmark]
    [BenchmarkCategory("Enumeration")]
    public long PackedPrototypeEnumeration() => FrozenF0Operations.SumEntries(_fixture.Packed);

    [Benchmark]
    [BenchmarkCategory("Enumeration")]
    public long DictionaryEnumeration() => FrozenF0Operations.SumEntries(_fixture.Dictionary);

    [Benchmark]
    [BenchmarkCategory("Enumeration")]
    public long ImmutableEnumeration() => FrozenF0Operations.SumEntries(_fixture.Immutable);

    [Benchmark]
    [BenchmarkCategory("Enumeration")]
    public long BclFrozenEnumeration() => FrozenF0Operations.SumEntries(_fixture.BclFrozen);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Construction")]
    public PersistentHashMap<Axis2HashKey, int> PersistentRebuildFromSourceMap() =>
        _fixture.ConstructPersistent();

    [Benchmark]
    [BenchmarkCategory("Construction")]
    public object PackedPrototypeConstructionFromSourceMap() => _fixture.ConstructPacked();

    [Benchmark]
    [BenchmarkCategory("Construction")]
    public Dictionary<Axis2HashKey, int> DictionaryConstructionFromSourceMap() =>
        _fixture.ConstructDictionary();

    [Benchmark]
    [BenchmarkCategory("Construction")]
    public ImmutableDictionary<Axis2HashKey, int> ImmutableConstructionFromSourceMap() =>
        _fixture.ConstructImmutable();

    [Benchmark]
    [BenchmarkCategory("Construction")]
    public FrozenDictionary<Axis2HashKey, int> BclFrozenConstructionFromSourceMap() =>
        _fixture.ConstructBclFrozen();
}

/// <summary>
/// The general F0 lane: uniform real hashes, exact comparer sharing, and exact hit/miss mixes over
/// counts through 100,000. Construction for every control enumerates the same captured CHAMP map.
/// </summary>
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class FrozenLookupBenchmarks : FrozenF0AxisBenchmarksBase
{
    [Params(1, 8, 32, 1_024, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup() => Initialize(Count, Axis2HashShape.Uniform, "uniform");
}

/// <summary>
/// An explicit equal-full-hash lane. It is capped at 1,024 entries because every semantically
/// faithful implementation must perform an equality scan within the collision run.
/// </summary>
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class FrozenCollisionLookupBenchmarks : FrozenF0AxisBenchmarksBase
{
    [Params(8, 32, 1_024)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup() => Initialize(Count, Axis2HashShape.FullCollision, "full-collision");
}

/// <summary>
/// Null-key, first-representative, and equal-full-hash lane. BCL mutable, immutable, and frozen
/// controls are explicitly omitted: all three reject null keys, and replacing null with a sentinel
/// would make the workload semantically different.
/// </summary>
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class FrozenNullLookupBenchmarks
{
    private FrozenF0NullCollisionFixture _fixture = null!;

    [Params(8, 32)]
    public int Count { get; set; }

    [Params(0, 50, 100)]
    public int HitPercentage { get; set; }

    [GlobalSetup]
    public void Setup() => _fixture = new FrozenF0NullCollisionFixture(Count, HitPercentage);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("NullLookupMix")]
    public long PersistentNullLookupMix() =>
        FrozenF0Operations.SumLookups(_fixture.Persistent, _fixture.Probes);

    [Benchmark]
    [BenchmarkCategory("NullLookupMix")]
    public long PackedPrototypeNullLookupMix() =>
        FrozenF0Operations.SumLookups(_fixture.Packed, _fixture.Probes);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("NullEnumeration")]
    public long PersistentNullEnumeration() => FrozenF0Operations.SumEntries(_fixture.Persistent);

    [Benchmark]
    [BenchmarkCategory("NullEnumeration")]
    public long PackedPrototypeNullEnumeration() => FrozenF0Operations.SumEntries(_fixture.Packed);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("NullConstruction")]
    public PersistentHashMap<string?, int> PersistentNullRebuildFromSourceMap() =>
        _fixture.ConstructPersistent();

    [Benchmark]
    [BenchmarkCategory("NullConstruction")]
    public object PackedPrototypeNullConstructionFromSourceMap() => _fixture.ConstructPacked();
}

/// <summary>
/// Concrete overloads keep every timed lookup/enumeration on its real collection type. This avoids
/// adding interface enumeration or delegate allocation to one control but not another.
/// </summary>
internal static class FrozenF0Operations
{
    internal static long SumLookups(
        PersistentHashMap<Axis2HashKey, int> map,
        Axis2HashKey[] probes)
    {
        long sum = 0;
        foreach (var probe in probes)
        {
            if (map.TryGetValue(probe, out var value))
                sum += value;
        }

        return sum;
    }

    internal static long SumLookups(
        PackedFrozenMapPrototype<Axis2HashKey, int> map,
        Axis2HashKey[] probes)
    {
        long sum = 0;
        foreach (var probe in probes)
        {
            if (map.TryGetValue(probe, out var value))
                sum += value;
        }

        return sum;
    }

    internal static long SumLookups(
        Dictionary<Axis2HashKey, int> map,
        Axis2HashKey[] probes)
    {
        long sum = 0;
        foreach (var probe in probes)
        {
            if (map.TryGetValue(probe, out var value))
                sum += value;
        }

        return sum;
    }

    internal static long SumLookups(
        ImmutableDictionary<Axis2HashKey, int> map,
        Axis2HashKey[] probes)
    {
        long sum = 0;
        foreach (var probe in probes)
        {
            if (map.TryGetValue(probe, out var value))
                sum += value;
        }

        return sum;
    }

    internal static long SumLookups(
        FrozenDictionary<Axis2HashKey, int> map,
        Axis2HashKey[] probes)
    {
        long sum = 0;
        foreach (var probe in probes)
        {
            if (map.TryGetValue(probe, out var value))
                sum += value;
        }

        return sum;
    }

    internal static long SumLookups(PersistentHashMap<string?, int> map, string?[] probes)
    {
        long sum = 0;
        foreach (var probe in probes)
        {
            if (map.TryGetValue(probe, out var value))
                sum += value;
        }

        return sum;
    }

    internal static long SumLookups(PackedFrozenMapPrototype<string?, int> map, string?[] probes)
    {
        long sum = 0;
        foreach (var probe in probes)
        {
            if (map.TryGetValue(probe, out var value))
                sum += value;
        }

        return sum;
    }

    internal static long SumEntries(PersistentHashMap<Axis2HashKey, int> map)
    {
        long sum = 0;
        foreach (var entry in map)
            sum += entry.Value;
        return sum;
    }

    internal static long SumEntries(PackedFrozenMapPrototype<Axis2HashKey, int> map)
    {
        long sum = 0;
        foreach (var entry in map)
            sum += entry.Value;
        return sum;
    }

    internal static long SumEntries(Dictionary<Axis2HashKey, int> map)
    {
        long sum = 0;
        foreach (var entry in map)
            sum += entry.Value;
        return sum;
    }

    internal static long SumEntries(ImmutableDictionary<Axis2HashKey, int> map)
    {
        long sum = 0;
        foreach (var entry in map)
            sum += entry.Value;
        return sum;
    }

    internal static long SumEntries(FrozenDictionary<Axis2HashKey, int> map)
    {
        long sum = 0;
        foreach (var entry in map)
            sum += entry.Value;
        return sum;
    }

    internal static long SumEntries(PersistentHashMap<string?, int> map)
    {
        long sum = 0;
        foreach (var entry in map)
            sum += entry.Value;
        return sum;
    }

    internal static long SumEntries(PackedFrozenMapPrototype<string?, int> map)
    {
        long sum = 0;
        foreach (var entry in map)
            sum += entry.Value;
        return sum;
    }
}
