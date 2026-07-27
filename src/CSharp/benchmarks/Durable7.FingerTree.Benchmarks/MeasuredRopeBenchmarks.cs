// Benchmarks for the measured rope.

using BenchmarkDotNet.Attributes;

namespace Durable7.FingerTree.Benchmarks;

// The measured rope's payoff: navigating a text buffer by line in O(log n) instead of the O(n) newline scan a
// plain string requires. A line-measured rope locates a line's start offset, or the line containing an offset,
// by descending the cached newline measure; the string baseline must scan. The gap grows with document size,
// so this sweeps the line count and the rope times stay flat while the scans climb.
[MemoryDiagnoser]
public class MeasuredRopeBenchmarks
{
    /// <summary>Gets the lines.</summary>
    [Params(10_000, 1_000_000)]
    public int Lines;

    private MeasuredRope<char, int, NewlineMeasure> _rope = null!;
    private string _text = null!;
    private int _midLine;
    private int _midOffset;

    /// <summary>Prepares the workload this benchmark measures. Runs outside the measured region.</summary>
    [GlobalSetup]
    public void Setup()
    {
        var builder = new System.Text.StringBuilder();
        for (var i = 0; i < Lines; i++)
            builder.Append("line ").Append(i).Append('\n');
        _text = builder.ToString();
        _rope = _text.ToTextRope();
        _midLine = Lines / 2;
        _midOffset = _text.Length / 2;
    }

    // line -> start offset

    [Benchmark(Baseline = true)]
    [BenchmarkCategory("LineStart")]
    public int Rope_LineStartOffset() => _rope.LineStartOffset(_midLine);

    /// <summary>Measures scan line start offset.</summary>
    [Benchmark]
    [BenchmarkCategory("LineStart")]
    public int Scan_LineStartOffset()
    {
        var line = 0;
        for (var i = 0; i < _text.Length; i++)
        {
            if (_text[i] != '\n')
                continue;
            if (++line == _midLine)
                return i + 1;
        }

        return _text.Length;
    }

    // offset -> line index

    [Benchmark]
    [BenchmarkCategory("LineOf")]
    public int Rope_LineOfOffset() => _rope.LineOfOffset(_midOffset);

    /// <summary>Measures scan line of offset.</summary>
    [Benchmark]
    [BenchmarkCategory("LineOf")]
    public int Scan_LineOfOffset()
    {
        var count = 0;
        for (var i = 0; i < _midOffset; i++)
            if (_text[i] == '\n')
                count++;
        return count;
    }
}
