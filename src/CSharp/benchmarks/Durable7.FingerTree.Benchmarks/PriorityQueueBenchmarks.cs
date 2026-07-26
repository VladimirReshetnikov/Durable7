// Benchmarks for the priority queue.

using BenchmarkDotNet.Attributes;
using FtPriorityQueue = Durable7.FingerTree.PriorityQueue<int, int>;
using BclPriorityQueue = System.Collections.Generic.PriorityQueue<int, int>;

namespace Durable7.FingerTree.Benchmarks;

// The meldable priority queue's headline is Meld in O(log(min(n, m))): two persistent queues join by tree
// concatenation, sharing structure. A binary-heap PriorityQueue (the BCL) cannot merge two heaps cheaply — the
// only option is to rebuild, which is O(n + m). The benchmark melds a fixed large queue with a varying one;
// our cost grows with log(right) while the BCL rebuild grows linearly. Peek/Enqueue/Dequeue are included for
// the per-operation picture. Aliases keep our PriorityQueue distinct from the BCL generic of the same name.
[MemoryDiagnoser]
public class PriorityQueueBenchmarks
{
    private const int LeftSize = 100_000;

    [Params(100, 100_000)]
    public int RightSize;

    private FtPriorityQueue _left = null!;
    private FtPriorityQueue _right = null!;
    private int[] _leftElems = null!;
    private int[] _rightElems = null!;

    [GlobalSetup]
    public void Setup()
    {
        _left = FtPriorityQueue.Empty;
        for (var i = 0; i < LeftSize; i++)
            _left = _left.Enqueue(i, i);

        _right = FtPriorityQueue.Empty;
        for (var i = 0; i < RightSize; i++)
            _right = _right.Enqueue(LeftSize + i, LeftSize + i);

        _leftElems = Enumerable.Range(0, LeftSize).ToArray();
        _rightElems = Enumerable.Range(LeftSize, RightSize).ToArray();
    }

    // O(log(min(n, m))): persistent meld by concatenation.
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Meld")]
    public FtPriorityQueue Ours_Meld() => _left.Meld(_right);

    // O(n + m): a binary heap cannot merge, so rebuild a fresh queue holding both sides' entries.
    [Benchmark]
    [BenchmarkCategory("Meld")]
    public BclPriorityQueue Bcl_RebuildToMerge()
    {
        var merged = new BclPriorityQueue();
        foreach (var e in _leftElems)
            merged.Enqueue(e, e);
        foreach (var e in _rightElems)
            merged.Enqueue(e, e);
        return merged;
    }

    // O(1): peek the minimum priority through the cached measure.
    [Benchmark]
    [BenchmarkCategory("Peek")]
    public int Ours_PeekPriority() => _left.TryPeekPriority(out var p) ? p : -1;

    // O(log n): persistent dequeue of the minimum.
    [Benchmark]
    [BenchmarkCategory("Dequeue")]
    public FtPriorityQueue Ours_Dequeue() => _left.TryDequeue(out _, out _, out var rest) ? rest : _left;
}
