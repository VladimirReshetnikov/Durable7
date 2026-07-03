using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;

namespace Tools.DataStructures.FingerTree.Benchmarks;

// Endpoint operations: FingerTreeDeque is O(1) amortized at both ends and O(1) worst case to read them, while
// ImmutableList.Insert(0, x) is O(n) (prepend shifts the whole spine) and Add is O(log n). Each method performs
// exactly ONE persistent operation on a structure pre-built in [GlobalSetup]; the new instance is returned so
// the JIT cannot elide it.
[MemoryDiagnoser]
public class DequeEndpointBenchmarks
{
    [Params(1_000, 100_000)]
    public int Size;

    private FingerTreeDeque<int> _deque = null!;
    private ImmutableList<int> _immutable = null!;

    [GlobalSetup]
    public void Setup()
    {
        var items = Enumerable.Range(0, Size).ToArray();
        _deque = FingerTreeDeque<int>.CreateRange(items);
        _immutable = ImmutableList.CreateRange(items);
    }

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("AddFirst")]
    public FingerTreeDeque<int> Deque_AddFirst() => _deque.AddFirst(-1);

    [Benchmark]
    [BenchmarkCategory("AddFirst")]
    public ImmutableList<int> ImmutableList_Insert0() => _immutable.Insert(0, -1);

    [Benchmark]
    [BenchmarkCategory("AddLast")]
    public FingerTreeDeque<int> Deque_AddLast() => _deque.AddLast(-1);

    [Benchmark]
    [BenchmarkCategory("AddLast")]
    public ImmutableList<int> ImmutableList_Add() => _immutable.Add(-1);

    [Benchmark]
    [BenchmarkCategory("RemoveFirst")]
    public FingerTreeDeque<int> Deque_RemoveFirst() => _deque.RemoveFirst();

    [Benchmark]
    [BenchmarkCategory("First")]
    public int Deque_First() => _deque.First;
}

// Indexed access is O(log(distance from the nearer end)) for the finger tree: reads near either end are cheap,
// the middle is the worst case. ImmutableList indexing is O(log n) regardless of position. Varying Position
// makes the finger tree's distance-dependent, U-shaped cost visible against the flat ImmutableList line.
[MemoryDiagnoser]
public class DequeIndexingBenchmarks
{
    [Params(100_000)]
    public int Size;

    [Params("Start", "Quarter", "Mid", "End")]
    public string Position = "";

    private FingerTreeDeque<int> _deque = null!;
    private ImmutableList<int> _immutable = null!;
    private int _index;

    [GlobalSetup]
    public void Setup()
    {
        var items = Enumerable.Range(0, Size).ToArray();
        _deque = FingerTreeDeque<int>.CreateRange(items);
        _immutable = ImmutableList.CreateRange(items);
        _index = Position switch
        {
            "Start" => 0,
            "Quarter" => Size / 4,
            "Mid" => Size / 2,
            "End" => Size - 1,
            _ => 0,
        };
    }

    [Benchmark(Baseline = true)]
    public int Deque_Index() => _deque[_index];

    [Benchmark]
    public int ImmutableList_Index() => _immutable[_index];
}

// Concatenation is O(log(min(n, m))) for the finger tree, so with a fixed large left operand the cost grows
// only with log(right). The BCL has no immutable concat, so the fair idiomatic alternative is AddRange, which
// is O(m log n); SplitAt is O(log(min(k, n-k))) versus ImmutableList.GetRange which is O(k).
[MemoryDiagnoser]
public class DequeConcatBenchmarks
{
    [Params(100, 100_000)]
    public int RightSize;

    private const int LeftSize = 100_000;

    private FingerTreeDeque<int> _left = null!;
    private FingerTreeDeque<int> _right = null!;
    private ImmutableList<int> _leftImmutable = null!;
    private ImmutableList<int> _rightImmutable = null!;

    [GlobalSetup]
    public void Setup()
    {
        _left = FingerTreeDeque<int>.CreateRange(Enumerable.Range(0, LeftSize));
        _right = FingerTreeDeque<int>.CreateRange(Enumerable.Range(LeftSize, RightSize));
        _leftImmutable = ImmutableList.CreateRange(Enumerable.Range(0, LeftSize));
        _rightImmutable = ImmutableList.CreateRange(Enumerable.Range(LeftSize, RightSize));
    }

    [Benchmark(Baseline = true)]
    public FingerTreeDeque<int> Deque_Concat() => _left.Concat(_right);

    [Benchmark]
    public ImmutableList<int> ImmutableList_AddRange() => _leftImmutable.AddRange(_rightImmutable);
}
