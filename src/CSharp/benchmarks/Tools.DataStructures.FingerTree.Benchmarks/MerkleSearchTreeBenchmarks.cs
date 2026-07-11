using BenchmarkDotNet.Attributes;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class MerkleSearchTreeBenchmarks
{
    private MerkleSearchTree<int, long> _tree = null!;
    private MerkleSearchTree<int, long> _independent = null!;
    private MerkleSearchTree<int, long> _changed = null!;
    private System.Collections.Generic.SortedDictionary<int, long> _dictionary = null!;
    private int _probe;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        var policy = MerkleSearchTreePolicy<int, long>.Create(
            "benchmark-int-long-v1",
            Comparer<int>.Default,
            MerkleCodecs.Int32,
            MerkleCodecs.Int64);
        var entries = Enumerable.Range(0, Count).Select(key => KeyValuePair.Create(key, (long)key)).ToArray();
        _tree = MerkleSearchTree<int, long>.CreateRange(entries, policy);
        _independent = MerkleSearchTree<int, long>.CreateRange(entries.Reverse(), policy);
        _changed = _tree.SetItem(Count / 2, -1);
        _dictionary = new System.Collections.Generic.SortedDictionary<int, long>(entries.ToDictionary());
        _probe = Count / 2;
    }

    [Benchmark(Baseline = true)]
    public long MerkleLookup() => _tree[_probe];

    [Benchmark]
    public long SortedDictionaryLookup() => _dictionary[_probe];

    [Benchmark]
    public bool MerkleContentEquality() => _tree.ContentEquals(_independent);

    [Benchmark]
    public IReadOnlyList<MerkleMapDifference<int, long>> MerkleSingleChangeDiff() => _tree.Diff(_changed);

    [Benchmark]
    public MerkleSearchTree<int, long> MerklePersistentUpdate() => _tree.SetItem(_probe, -1);
}
