using System.Collections.Immutable;
using BenchmarkDotNet.Attributes;

namespace Durable7.FingerTree.Benchmarks;

// The rope's reason to exist: editing a large sequence in O(log n) instead of O(n). Mid-buffer insert and
// remove on a string (or StringBuilder) copy the whole tail -- O(n) -- while Rope<char> splices in O(log n) and
// returns a new persistent version sharing structure. ImmutableList<char> is the fair persistent baseline. The
// honest trade is random indexing (string is O(1); the rope is O(log n)), so this set focuses on the editing
// operations where the rope wins, across a large-text size sweep.
[MemoryDiagnoser]
public class RopeBenchmarks
{
    [Params(100_000, 1_000_000)]
    public int Size;

    private const string Insertion = "the quick brown fox jumps";   // 25 chars

    private char[] _source = null!;
    private Rope<char> _rope = null!;
    private string _string = null!;
    private ImmutableList<char> _immutable = null!;

    [GlobalSetup]
    public void Setup()
    {
        var random = new Random(42);
        _source = new char[Size];
        for (var i = 0; i < Size; i++)
            _source[i] = (char)('a' + random.Next(26));
        _rope = Rope<char>.Create(_source);
        _string = new string(_source);
        _immutable = ImmutableList.CreateRange(_source);
    }

    // ---- Build a buffer of Size chars from a char array ----

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Build")]
    public Rope<char> Rope_Build() => Rope<char>.Create(_source);

    [Benchmark]
    [BenchmarkCategory("Build")]
    public ImmutableList<char> Immutable_Build() => ImmutableList.CreateRange(_source);

    // ---- Insert a short run at the middle (the headline) ----

    [Benchmark]
    [BenchmarkCategory("InsertMid")]
    public Rope<char> Rope_InsertMid() => _rope.InsertRange(Size / 2, Insertion.AsSpan());

    [Benchmark]
    [BenchmarkCategory("InsertMid")]
    public ImmutableList<char> Immutable_InsertMid() => _immutable.InsertRange(Size / 2, Insertion);

    [Benchmark]
    [BenchmarkCategory("InsertMid")]
    public string String_InsertMid() => string.Concat(_string.AsSpan(0, Size / 2), Insertion, _string.AsSpan(Size / 2));

    // ---- Remove a 1000-element run from the middle ----

    [Benchmark]
    [BenchmarkCategory("RemoveMid")]
    public Rope<char> Rope_RemoveMid() => _rope.RemoveRange(Size / 2, 1000);

    [Benchmark]
    [BenchmarkCategory("RemoveMid")]
    public string String_RemoveMid() => _string.Remove(Size / 2, 1000);

    // ---- Split in half and re-concatenate (a persistent rearrange) ----

    [Benchmark]
    [BenchmarkCategory("SplitConcat")]
    public Rope<char> Rope_SplitConcat()
    {
        var (left, right) = _rope.Split(Size / 2);
        return right.Concat(left);
    }

    [Benchmark]
    [BenchmarkCategory("SplitConcat")]
    public string String_SplitConcat() => string.Concat(_string.AsSpan(Size / 2), _string.AsSpan(0, Size / 2));
}
