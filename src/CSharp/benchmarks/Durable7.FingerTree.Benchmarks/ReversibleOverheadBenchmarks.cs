// Benchmarks for the reversible overhead.

using BenchmarkDotNet.Attributes;

namespace Durable7.FingerTree.Benchmarks;

// The price of O(1) reverse: every internal access in ReversibleDeque tests a reversal bit and reads through
// orientation-aware accessors. This measures that constant-factor overhead versus the plain FingerTreeDeque on
// normal operations, both in forward orientation and after a Reverse() (the reversed-path cost), so the
// trade-off behind choosing ReversibleDeque is quantified rather than asserted.
[MemoryDiagnoser]
public class ReversibleOverheadBenchmarks
{
    /// <summary>Gets the number of elements in the collection.</summary>
    [Params(100_000)]
    public int Size;

    private FingerTreeDeque<int> _plain = null!;
    private ReversibleDeque<int> _reversible = null!;
    private ReversibleDeque<int> _reversed = null!;

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var items = Enumerable.Range(0, Size).ToArray();
        _plain = FingerTreeDeque<int>.CreateRange(items);
        _reversible = ReversibleDeque<int>.CreateRange(items);
        _reversed = _reversible.Reverse();
    }

    /// <summary>Measures plain add first.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("AddFirst")]
    public FingerTreeDeque<int> Plain_AddFirst() => _plain.AddFirst(-1);

    /// <summary>Measures reversible add first.</summary>
    [Benchmark]
    [BenchmarkCategory("AddFirst")]
    public ReversibleDeque<int> Reversible_AddFirst() => _reversible.AddFirst(-1);

    /// <summary>Measures reversible add first reversed.</summary>
    [Benchmark]
    [BenchmarkCategory("AddFirst")]
    public ReversibleDeque<int> Reversible_AddFirst_Reversed() => _reversed.AddFirst(-1);

    /// <summary>Measures plain index mid.</summary>
    [Benchmark]
    [BenchmarkCategory("IndexMid")]
    public int Plain_IndexMid() => _plain[Size / 2];

    /// <summary>Measures reversible index mid.</summary>
    [Benchmark]
    [BenchmarkCategory("IndexMid")]
    public int Reversible_IndexMid() => _reversible[Size / 2];

    /// <summary>Measures reversible index mid reversed.</summary>
    [Benchmark]
    [BenchmarkCategory("IndexMid")]
    public int Reversible_IndexMid_Reversed() => _reversed[Size / 2];
}
