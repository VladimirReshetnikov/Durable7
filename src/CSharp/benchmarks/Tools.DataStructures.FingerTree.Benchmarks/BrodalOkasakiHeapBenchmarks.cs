using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Configs;
using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class BrodalOkasakiHeapBenchmarks
{
    private BrodalOkasakiHeap<int> _heap = null!;
    private BrodalOkasakiHeap<int> _other = null!;
    private PriorityQueue<int, int> _fingerTree = null!;
    private PriorityQueue<int, int> _otherFingerTree = null!;
    private int[] _values = null!;
    private (int Element, int Priority)[] _priorityEntries = null!;

    [Params(1_000, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        // Ascending insertion keeps the minimum root in place, building the ranked forest that
        // delete-min must normalize. Descending insertion would mostly benchmark root replacement.
        _values = Enumerable.Range(0, Count).ToArray();
        _priorityEntries = _values.Select(value => (value, value)).ToArray();
        _heap = BrodalOkasakiHeap<int>.CreateRange(_values);
        _other = BrodalOkasakiHeap<int>.CreateRange(_values);
        _fingerTree = PriorityQueue<int, int>.CreateRange(_priorityEntries);
        _otherFingerTree = PriorityQueue<int, int>.CreateRange(_priorityEntries);
    }

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Insert")]
    public BrodalOkasakiHeap<int> BrodalInsert() => _heap.Insert(Count);

    [Benchmark]
    [BenchmarkCategory("Insert")]
    public PriorityQueue<int, int> FingerTreeEnqueue() => _fingerTree.Enqueue(Count, Count);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Meld")]
    public BrodalOkasakiHeap<int> BrodalMeld() => _heap.Meld(_other);

    [Benchmark]
    [BenchmarkCategory("Meld")]
    public PriorityQueue<int, int> FingerTreeMeld() => _fingerTree.Meld(_otherFingerTree);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("DeleteMinimum")]
    public BrodalOkasakiHeap<int> BrodalDeleteMinimum() => _heap.DeleteMinimum();

    [Benchmark]
    [BenchmarkCategory("DeleteMinimum")]
    public PriorityQueue<int, int> FingerTreeDequeue() =>
        _fingerTree.TryDequeue(out _, out _, out var remainder) ? remainder : _fingerTree;

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Build")]
    public BrodalOkasakiHeap<int> BrodalBuildFromRange() =>
        BrodalOkasakiHeap<int>.CreateRange(_values);

    [Benchmark]
    [BenchmarkCategory("Build")]
    public PriorityQueue<int, int> FingerTreeBuildFromRange() =>
        PriorityQueue<int, int>.CreateRange(_priorityEntries);

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Drain")]
    public long BrodalDrainAll()
    {
        var heap = _heap;
        long checksum = 0;
        while (heap.TryDeleteMinimum(out var minimum, out var remainder))
        {
            checksum += minimum;
            heap = remainder;
        }

        return checksum;
    }

    [Benchmark]
    [BenchmarkCategory("Drain")]
    public long FingerTreeDrainAll()
    {
        var queue = _fingerTree;
        long checksum = 0;
        while (queue.TryDequeue(out var element, out _, out var remainder))
        {
            checksum += element;
            queue = remainder;
        }

        return checksum;
    }

    [Benchmark]
    [BenchmarkCategory("Validate")]
    public BrodalOkasakiHeapStatistics BrodalValidateStructure() => _heap.ValidateStructure();
}
