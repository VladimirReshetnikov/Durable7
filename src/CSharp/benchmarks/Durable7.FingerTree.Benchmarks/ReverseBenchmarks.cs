// Benchmarks for the reverse.

using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;

namespace Durable7.FingerTree.Benchmarks;

// The headline claim: ReversibleDeque.Reverse() is O(1) worst case (mirror the root, flip a bit), so its time
// is flat across sizes while every eager structure's reverse climbs linearly. The Ratio and Allocated columns
// against the ImmutableList baseline make the asymptotic gap legible at a glance.
[MemoryDiagnoser]
public class ReverseBenchmarks
{
    [Params(100, 10_000, 1_000_000)]
    public int Size;

    private ReversibleDeque<int> _reversible = null!;
    private ImmutableList<int> _immutable = null!;
    private int[] _array = null!;

    [GlobalSetup]
    public void Setup()
    {
        var items = Enumerable.Range(0, Size).ToArray();
        _reversible = ReversibleDeque<int>.CreateRange(items);
        _immutable = ImmutableList.CreateRange(items);
        _array = items;
    }

    // O(1): mirror the root. Should be a flat ~nanoseconds line regardless of Size, allocating O(1) bytes.
    [Benchmark(Baseline = true)]
    public ReversibleDeque<int> ReversibleDeque_Reverse() => _reversible.Reverse();

    // O(n): the BCL immutable reverse rebuilds the whole tree.
    [Benchmark]
    public ImmutableList<int> ImmutableList_Reverse() => _immutable.Reverse();

    // O(n) lower bound: an in-place array reverse, the fastest possible eager reverse, still linear.
    [Benchmark]
    public int[] Array_Reverse()
    {
        var copy = (int[])_array.Clone();
        Array.Reverse(copy);
        return copy;
    }
}
