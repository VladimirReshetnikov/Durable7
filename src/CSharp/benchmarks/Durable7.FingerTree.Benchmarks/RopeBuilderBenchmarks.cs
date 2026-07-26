// Benchmarks for the rope builder.

using System.Collections.Immutable;
using System.Text;
using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Configs;

namespace Durable7.FingerTree.Benchmarks;

// Append-heavy construction paths for the rope builders. The add-last loop is intentionally included as the
// boundary-copy baseline that the append-only builders avoid; Create remains the up-front-input baseline.
[MemoryDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByCategory)]
public class RopeBuilderBenchmarks
{
    [Params(10_000, 100_000)]
    public int Size;

    private char[] _chars = null!;
    private int[] _ints = null!;

    [GlobalSetup]
    public void Setup()
    {
        var random = new Random(42);
        _chars = new char[Size];
        _ints = new int[Size];
        for (var i = 0; i < Size; i++)
        {
            _chars[i] = (char)('a' + random.Next(26));
            _ints[i] = i % 17;
        }
    }

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("RopeBuild")]
    public Rope<char> Rope_Create() => Rope<char>.Create(_chars);

    [Benchmark]
    [BenchmarkCategory("RopeBuild")]
    public Rope<char> Rope_BuilderAddRangeSpan()
    {
        var builder = Rope<char>.CreateBuilder();
        builder.AddRange(_chars.AsSpan());
        return builder.ToImmutable();
    }

    [Benchmark]
    [BenchmarkCategory("RopeBuild")]
    public Rope<char> Rope_BuilderAddOneByOne()
    {
        var builder = Rope<char>.CreateBuilder();
        foreach (var c in _chars)
            builder.Add(c);
        return builder.ToImmutable();
    }

    [Benchmark]
    [BenchmarkCategory("RopeBuild")]
    public Rope<char> Rope_AddLastLoop()
    {
        var rope = Rope<char>.Empty;
        foreach (var c in _chars)
            rope = rope.AddLast(c);
        return rope;
    }

    [Benchmark]
    [BenchmarkCategory("RopeBuild")]
    public ImmutableList<char> ImmutableListBuilder_AddOneByOne()
    {
        var builder = ImmutableList.CreateBuilder<char>();
        foreach (var c in _chars)
            builder.Add(c);
        return builder.ToImmutable();
    }

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("MeasuredRopeBuild")]
    public MeasuredRope<int, int, SumMeasure<int>> MeasuredRope_Create() =>
        MeasuredRope<int, int, SumMeasure<int>>.Create(_ints);

    [Benchmark]
    [BenchmarkCategory("MeasuredRopeBuild")]
    public MeasuredRope<int, int, SumMeasure<int>> MeasuredRope_BuilderAddRangeSpan()
    {
        var builder = MeasuredRope<int, int, SumMeasure<int>>.CreateBuilder();
        builder.AddRange(_ints.AsSpan());
        return builder.ToImmutable();
    }

    [Benchmark]
    [BenchmarkCategory("TextBuilder")]
    public Rope<char> Text_RopeBuilderToRope()
    {
        var builder = new RopeBuilder();
        builder.Append(_chars);
        return builder.ToRope();
    }

    [Benchmark]
    [BenchmarkCategory("TextBuilder")]
    public Rope<char> Text_StringBuilderThenCreate()
    {
        var builder = new StringBuilder();
        builder.Append(_chars);
        return Rope<char>.Create(builder.ToString().AsSpan());
    }
}
