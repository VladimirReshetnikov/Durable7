using BenchmarkDotNet.Attributes;
using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Benchmarks;

[MemoryDiagnoser]
public class DabaLiteBenchmarks
{
    private DabaLite<long, SumMonoid> _daba = null!;
    private Queue<long> _queue = null!;
    private long _nextDaba;
    private long _nextQueue;

    [Params(63, 64, 65, 1_000, 100_000)]
    public int Count { get; set; }

    [GlobalSetup]
    public void Setup()
    {
        _daba = new DabaLite<long, SumMonoid>();
        _queue = new Queue<long>(Count);
        for (var i = 0; i < Count; i++)
        {
            _daba.Insert(i);
            _queue.Enqueue(i);
        }
        _nextDaba = Count;
        _nextQueue = Count;
    }

    [Benchmark(Baseline = true)]
    public long DabaSlideAndQuery()
    {
        _daba.Evict();
        _daba.Insert(_nextDaba++);
        return _daba.Aggregate;
    }

    [Benchmark]
    public long QueueSlideAndReaggregate()
    {
        _queue.Dequeue();
        _queue.Enqueue(_nextQueue++);
        return _queue.Sum();
    }

    [Benchmark]
    public DabaLiteStatistics DabaValidateStructure() => _daba.ValidateStructure();

    private readonly struct SumMonoid : IMonoid<long>
    {
        public static long Empty => 0;

        public static long Combine(long left, long right) => left + right;
    }
}
