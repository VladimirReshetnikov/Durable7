// Benchmarks for the merkle search tree.

using BenchmarkDotNet.Attributes;
using Durable7.Hamt;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>
/// Benchmarks for the Merkle search tree, including synchronization and proofs.
/// </summary>
[MemoryDiagnoser]
public class MerkleSearchTreeBenchmarks
{
    private MerkleSearchTree<int, long> _tree = null!;
    private MerkleSearchTree<int, long> _independent = null!;
    private MerkleSearchTree<int, long> _changed = null!;
    private MerkleSearchTree<int, long> _left = null!;
    private MerkleSearchTree<int, long> _right = null!;
    private MerkleSearchTreePolicy<int, long> _policy = null!;
    private InMemoryMerkleBlockStore _store = null!;
    private InMemoryMerkleBlockStore _emptyStore = null!;
    private System.Collections.Generic.SortedDictionary<int, long> _dictionary = null!;
    private int _probe;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        _policy = MerkleSearchTreePolicy<int, long>.Create(
            "benchmark-int-long-v1",
            Comparer<int>.Default,
            MerkleCodecs.Int32,
            MerkleCodecs.Int64);
        var entries = Enumerable.Range(0, Count).Select(key => KeyValuePair.Create(key, (long)key)).ToArray();
        _tree = MerkleSearchTree<int, long>.CreateRange(entries, _policy);
        _independent = MerkleSearchTree<int, long>.CreateRange(entries.Reverse(), _policy);
        _changed = _tree.SetItem(Count / 2, -1);
        _left = _tree.SetItem(0, -1);
        _right = _tree.SetItem(Count - 1, -2);
        _store = new InMemoryMerkleBlockStore();
        _ = _tree.Save(_store);
        _emptyStore = new InMemoryMerkleBlockStore();
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

    [Benchmark]
    public MerkleSearchTree<int, long> MerklePersistentInsert() => _tree.SetItem(Count, Count);

    [Benchmark]
    public MerkleSearchTree<int, long> MerklePersistentRemove() => _tree.Remove(_probe);

    [Benchmark]
    public MerkleSearchTree<int, long> MerkleVerifiedLoad() =>
        MerkleSearchTree<int, long>.Load(_tree.RootHash, _policy, _store);

    [Benchmark]
    public MerkleBlockPack MerkleColdSyncPack() => _tree.CreateSyncPack(_emptyStore);

    [Benchmark]
    public MerkleProof MerkleMembershipProof() => _tree.CreateProof(_probe);

    [Benchmark]
    public MerkleProof MerkleRangeProof() => _tree.CreateRangeProof(_probe - 32, _probe + 32);

    [Benchmark]
    public MerkleThreeWayMergeResult<int, long> MerkleDisjointThreeWayMerge() =>
        MerkleSearchTree<int, long>.Merge(_tree, _left, _right);
}
