using BenchmarkDotNet.Attributes;
using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class BrodalOkasakiHeapBenchmarks
{
    private BrodalOkasakiHeap<int> _heap = null!;
    private BrodalOkasakiHeap<int> _other = null!;
    private PriorityQueue<int, int> _fingerTree = null!;
    private PriorityQueue<int, int> _otherFingerTree = null!;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        var values = Enumerable.Range(0, Count).Reverse().ToArray();
        _heap = BrodalOkasakiHeap<int>.CreateRange(values);
        _other = BrodalOkasakiHeap<int>.CreateRange(values);
        _fingerTree = PriorityQueue<int, int>.CreateRange(values.Select(value => (value, value)));
        _otherFingerTree = PriorityQueue<int, int>.CreateRange(values.Select(value => (value, value)));
    }

    [Benchmark(Baseline = true)]
    public BrodalOkasakiHeap<int> BrodalInsert() => _heap.Insert(-1);

    [Benchmark]
    public PriorityQueue<int, int> FingerTreeEnqueue() => _fingerTree.Enqueue(-1, -1);

    [Benchmark]
    public BrodalOkasakiHeap<int> BrodalMeld() => _heap.Meld(_other);

    [Benchmark]
    public PriorityQueue<int, int> FingerTreeMeld() => _fingerTree.Meld(_otherFingerTree);

    [Benchmark]
    public BrodalOkasakiHeap<int> BrodalDeleteMinimum() => _heap.DeleteMinimum();
}
