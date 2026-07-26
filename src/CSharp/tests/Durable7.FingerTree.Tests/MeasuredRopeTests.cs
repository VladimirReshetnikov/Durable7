// Tests for the measured rope.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies <see cref="MeasuredRope{T, TMeasure, TMeasureOps}"/>: its positional operations against a list
/// model, that it tracks the user measure (cumulative sum) correctly, that measure navigation
/// (<c>PrefixMeasure</c>, <c>SplitByMeasure</c>, <c>TryLocateByMeasure</c>) matches a brute-force model, and that
/// a line measure over text gives O(log n) offset↔line navigation — all with internal invariants checked.
/// </summary>
public sealed class MeasuredRopeTests
{
    private readonly struct LineMeasure : IMeasure<char, int>
    {
        public static int Empty => 0;
        public static int Measure(char value) => value == '\n' ? 1 : 0;
        public static int Combine(int left, int right) => left + right;
    }

    private static void AssertSequence(IReadOnlyList<int> expected, MeasuredRope<int, int, SumMeasure<int>> actual)
    {
        actual.ValidateInvariants();
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected, actual.ToArray());
        Assert.Equal(expected, actual.ToList());
        Assert.Equal(expected.Sum(), actual.Measure);   // SumMeasure: the measure is the element sum
        for (var i = 0; i < expected.Count; i++)
            Assert.Equal(expected[i], actual[i]);
    }

    /// <summary>Verifies positional operations match a list model and keep the measure consistent.</summary>
    [Fact]
    public void Positional_MatchesListModel()
    {
        var model = Enumerable.Range(1, 6000).ToList();
        var rope = MeasuredRope<int, int, SumMeasure<int>>.Create(model.ToArray());
        AssertSequence(model, rope);

        var insModel = new List<int>(model);
        insModel.Insert(3000, 99999);
        AssertSequence(insModel, rope.Insert(3000, 99999));

        var remModel = new List<int>(model);
        remModel.RemoveRange(1000, 2000);
        AssertSequence(remModel, rope.RemoveRange(1000, 2000));

        var (left, right) = rope.Split(2500);
        AssertSequence(model.Take(2500).ToList(), left);
        AssertSequence(model.Skip(2500).ToList(), right);
        AssertSequence(model, left.Concat(right));
    }

    /// <summary>Verifies range validation rejects overflowing lengths before allocating or slicing.</summary>
    [Fact]
    public void RangeValidation_RejectsOverflowingLengths()
    {
        var rope = MeasuredRope<int, int, SumMeasure<int>>.Create(1, 2, 3);

        Assert.Throws<ArgumentOutOfRangeException>(() => rope.RemoveRange(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.Slice(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.GetRange(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.CopyTo(2, new int[2]));
    }

    /// <summary>Verifies PrefixMeasure equals the running sum of the first k elements, at every k.</summary>
    [Fact]
    public void PrefixMeasure_MatchesRunningSum()
    {
        var values = Enumerable.Range(1, 500).ToArray();
        var rope = MeasuredRope<int, int, SumMeasure<int>>.Create(values);

        var running = 0;
        for (var k = 0; k <= values.Length; k++)
        {
            Assert.Equal(running, rope.PrefixMeasure(k));
            if (k < values.Length)
                running += values[k];
        }
    }

    /// <summary>Verifies SplitByMeasure and TryLocateByMeasure match a brute-force cumulative-sum model at every threshold.</summary>
    [Fact]
    public void MeasureNavigation_MatchesBruteForce()
    {
        var values = Enumerable.Range(1, 400).ToArray();   // strictly positive => cumulative sum is monotone
        var rope = MeasuredRope<int, int, SumMeasure<int>>.Create(values);
        var total = values.Sum();

        for (var threshold = -1; threshold <= total + 1; threshold++)
        {
            var t = threshold;

            // Brute force: longest prefix whose sum is <= threshold; the next element flips "sum > threshold".
            var running = 0;
            var boundary = 0;
            while (boundary < values.Length && running + values[boundary] <= t)
                running += values[boundary++];

            var (left, right) = rope.SplitByMeasure(sum => sum > t);
            Assert.Equal(values.Take(boundary), left.ToArray());
            Assert.Equal(values.Skip(boundary), right.ToArray());
            Assert.Equal(running, left.Measure);

            var located = rope.TryLocateByMeasure(sum => sum > t, out var index, out var before, out var element);
            if (boundary < values.Length)
            {
                Assert.True(located);
                Assert.Equal(boundary, index);
                Assert.Equal(running, before);
                Assert.Equal(values[boundary], element);
            }
            else
            {
                Assert.False(located);
                Assert.Equal(total, before);
            }
        }
    }

    /// <summary>Verifies a line measure over text gives correct O(log n) offset→line and line→offset navigation.</summary>
    [Fact]
    public void TextLineNavigation_Works()
    {
        const string text = "line zero\nline one\nline two\n\nline four";
        var rope = MeasuredRope<char, int, LineMeasure>.Create(text.AsSpan());

        Assert.Equal(text.Length, rope.Count);
        Assert.Equal(text.Count(c => c == '\n'), rope.Measure);

        // offset -> line index = number of newlines strictly before the offset.
        for (var offset = 0; offset <= text.Length; offset++)
            Assert.Equal(text[..offset].Count(c => c == '\n'), rope.PrefixMeasure(offset));

        // line L (1-based here) -> the index of the L-th newline; the line starts at index + 1.
        var newlineCount = rope.Measure;
        for (var line = 1; line <= newlineCount; line++)
        {
            Assert.True(rope.TryLocateByMeasure(n => n >= line, out var index, out var before, out var ch));
            Assert.Equal('\n', ch);
            Assert.Equal(line - 1, before);                 // newlines before the L-th newline
            Assert.Equal(line, text.Take(index + 1).Count(c => c == '\n'));   // it is the L-th newline
        }
    }

    /// <summary>Runs a randomized history validating elements, the measure, and invariants after each step.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    public void RandomizedHistory_MatchesListAndMeasure(int seed)
    {
        var random = new Random(seed);
        var model = new List<int>();
        var rope = MeasuredRope<int, int, SumMeasure<int>>.Empty;
        var next = 1;   // positive values keep the cumulative sum monotone for SplitByMeasure checks

        for (var step = 0; step < 300; step++)
        {
            switch (random.Next(8))
            {
                case 0:
                    rope = rope.AddLast(next);
                    model.Add(next++);
                    break;
                case 1:
                    rope = rope.AddFirst(next);
                    model.Insert(0, next++);
                    break;
                case 2 when model.Count > 0:
                    rope = rope.RemoveFirst();
                    model.RemoveAt(0);
                    break;
                case 3:
                {
                    var index = random.Next(model.Count + 1);
                    rope = rope.Insert(index, next);
                    model.Insert(index, next++);
                    break;
                }
                case 4 when model.Count > 0:
                {
                    var index = random.Next(model.Count);
                    rope = rope.RemoveAt(index);
                    model.RemoveAt(index);
                    break;
                }
                case 5:
                {
                    var index = random.Next(model.Count + 1);
                    var run = Enumerable.Range(next, random.Next(0, 400)).ToArray();
                    next += run.Length;
                    rope = rope.InsertRange(index, run.AsSpan());
                    model.InsertRange(index, run);
                    break;
                }
                case 6 when model.Count > 0:
                {
                    var index = random.Next(model.Count);
                    var count = random.Next(0, model.Count - index + 1);
                    rope = rope.RemoveRange(index, count);
                    model.RemoveRange(index, count);
                    break;
                }
                default:
                {
                    var index = random.Next(model.Count + 1);
                    var (left, right) = rope.Split(index);
                    rope = left.Concat(right);
                    break;
                }
            }

            rope.ValidateInvariants();
            Assert.Equal(model, rope.ToArray());
            Assert.Equal(model.Sum(), rope.Measure);

            // Spot-check measure navigation against the model at a random threshold.
            if (model.Count > 0)
            {
                var threshold = random.Next(0, model.Sum() + 1);
                var running = 0;
                var boundary = 0;
                while (boundary < model.Count && running + model[boundary] <= threshold)
                    running += model[boundary++];
                var located = rope.TryLocateByMeasure(sum => sum > threshold, out var index, out _, out _);
                Assert.Equal(boundary < model.Count, located);
                if (located)
                    Assert.Equal(boundary, index);
            }
        }
    }
}
