// Benchmarks for the DABA Lite lite.

using BenchmarkDotNet.Attributes;
using Durable7.FingerTree;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>Benchmarks for the DABA Lite sliding-window aggregate.</summary>
[MemoryDiagnoser]
public class DabaLiteBenchmarks
{
    private DabaLite<long, SumMonoid> _daba = null!;
    private Queue<long> _queue = null!;
    private long _nextDaba;
    private long _nextQueue;

    /// <summary>Gets the number of elements in the window.</summary>
    [Params(63, 64, 65, 1_000, 100_000)]
    public int Count { get; set; }

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
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

    /// <summary>Measures daba slide and query.</summary>
    [Benchmark(Baseline = true)]
    public long DabaSlideAndQuery()
    {
        _daba.Evict();
        _daba.Insert(_nextDaba++);
        return _daba.Aggregate;
    }

    /// <summary>Measures queue slide and reaggregate.</summary>
    [Benchmark]
    public long QueueSlideAndReaggregate()
    {
        _queue.Dequeue();
        _queue.Enqueue(_nextQueue++);
        return _queue.Sum();
    }

    /// <summary>Measures daba validate structure.</summary>
    [Benchmark]
    public DabaLiteStatistics DabaValidateStructure() => _daba.ValidateStructure();

    private readonly struct SumMonoid : IMonoid<long>
    {
        /// <summary>Gets the identity: the measure of an empty tree.</summary>
        public static long Empty => 0;

        /// <summary>Combines two measures in order. Must be associative; it need not be commutative.</summary>
        public static long Combine(long left, long right) => left + right;
    }
}
