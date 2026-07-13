using BenchmarkDotNet.Attributes;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class TransientLifecycleBenchmarks
{
    private PersistentHashMap<Axis2HashKey, int> _base = null!;
    private KeyValuePair<Axis2HashKey, int>[] _constructionEntries = null!;
    private Axis2HashKey[] _editKeys = null!;

    [Params(0, 1, 8, 32, 1_024, 100_000)]
    public int Count { get; set; }

    [Params(1, 8, 64)]
    public int EditsPerPublication { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        _constructionEntries = Axis2BenchmarkPolicy.CreateHashEntries(Count, Axis2HashShape.ClusteredPrefix);
        _base = PersistentHashMap<Axis2HashKey, int>.CreateRange(
            _constructionEntries,
            Axis2HashKeyComparer.Instance);
        _editKeys = new Axis2HashKey[EditsPerPublication];
        for (var index = 0; index < _editKeys.Length; index++)
        {
            _editKeys[index] = Count == 0
                ? new Axis2HashKey(index, index << 5)
                : _constructionEntries[index % Count].Key;
        }
    }

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("EditPublish")]
    public object PersistentSetItemBatch()
    {
        var map = _base;
        for (var index = 0; index < _editKeys.Length; index++)
            map = map.SetItem(_editKeys[index], -index - 1);
        return map;
    }

    [Benchmark]
    [BenchmarkCategory("Construction")]
    public object CanonicalBulkBuilder()
    {
        var builder = PersistentHashMap<Axis2HashKey, int>.CreateBulkBuilder(Axis2HashKeyComparer.Instance);
        foreach (var entry in _constructionEntries)
            builder.SetItem(entry.Key, entry.Value);
        return builder.ToImmutable();
    }

    [Benchmark]
    [BenchmarkCategory("Construction")]
    public object PersistentConstructionLoop()
    {
        var map = PersistentHashMap<Axis2HashKey, int>.Create(Axis2HashKeyComparer.Instance);
        foreach (var entry in _constructionEntries)
            map = map.SetItem(entry.Key, entry.Value);
        return map;
    }
}
