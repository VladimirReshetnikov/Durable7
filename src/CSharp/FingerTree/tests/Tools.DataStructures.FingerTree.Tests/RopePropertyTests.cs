using CsCheck;
using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Property-based tests (CsCheck) for the ropes: generated operation histories are replayed against a
/// <see cref="List{T}"/> oracle with shrinking, and the algebraic laws (split/concat round-trip, slicing, and
/// the text rope's line navigation) are checked over thousands of generated inputs. CsCheck runs each property
/// across multiple threads with independent inputs, so these also incidentally exercise concurrent, independent
/// rope construction.
/// </summary>
public sealed class RopePropertyTests
{
    /// <summary>
    /// One generated edit: an opcode, a raw index source mapped into range at replay time, and a length used by
    /// the bulk operations. Keeping indices as raw ints (mapped with <c>% (count + 1)</c> during replay) lets the
    /// generator stay independent of the evolving size.
    /// </summary>
    private static readonly Gen<(int Op, int Index, int Length)> EditGen =
        Gen.Select(Gen.Int[0, 9], Gen.Int[0, 1_000_000], Gen.Int[0, 300], (op, index, length) => (op, index, length));

    private static readonly Gen<(int Op, int Index, int Length)[]> HistoryGen = EditGen.Array[0, 60];

    /// <summary>Generated text over a mixed alphabet with a moderate newline rate, so both multi-character lines
    /// and degenerate line structure (blank, leading, trailing, and consecutive newlines) all occur.</summary>
    private static readonly Gen<string> TextGen =
        Gen.OneOfConst('a', 'b', 'c', 'd', 'e', ' ', '\n').Array[0, 120].Select(chars => new string(chars));

    /// <summary>
    /// Replays a generated edit history against a <see cref="Rope{T}"/> and a <see cref="List{T}"/> oracle,
    /// validating chunk invariants and full content equality after every step. Shrinks to a minimal failing
    /// history on any divergence.
    /// </summary>
    [Fact]
    public void Rope_RandomHistory_MatchesListOracle()
    {
        HistoryGen.Sample(history =>
        {
            var rope = Rope<int>.Empty;
            var model = new List<int>();
            var next = 0;

            foreach (var (op, indexSource, length) in history)
            {
                var n = model.Count;
                switch (op)
                {
                    case 0:
                        rope = rope.AddFirst(next);
                        model.Insert(0, next++);
                        break;
                    case 1:
                        rope = rope.AddLast(next);
                        model.Add(next++);
                        break;
                    case 2 when n > 0:
                        rope = rope.RemoveFirst();
                        model.RemoveAt(0);
                        break;
                    case 3 when n > 0:
                        rope = rope.RemoveLast();
                        model.RemoveAt(n - 1);
                        break;
                    case 4:
                    {
                        var i = indexSource % (n + 1);
                        rope = rope.Insert(i, next);
                        model.Insert(i, next++);
                        break;
                    }
                    case 5 when n > 0:
                    {
                        var i = indexSource % n;
                        rope = rope.RemoveAt(i);
                        model.RemoveAt(i);
                        break;
                    }
                    case 6 when n > 0:
                    {
                        var i = indexSource % n;
                        rope = rope.SetItem(i, next);
                        model[i] = next++;
                        break;
                    }
                    case 7:
                    {
                        var i = indexSource % (n + 1);
                        var run = Enumerable.Range(next, length).ToArray();
                        next += length;
                        rope = rope.InsertRange(i, run.AsSpan());
                        model.InsertRange(i, run);
                        break;
                    }
                    case 8 when n > 0:
                    {
                        var i = indexSource % n;
                        // Clamp the generated length into the removable range. Using Math.Min (rather than a
                        // modulo) keeps a non-zero removal the overwhelmingly common case, so this branch actually
                        // exercises range removal — only length == 0 yields the empty no-op.
                        var count = Math.Min(length, n - i);
                        rope = rope.RemoveRange(i, count);
                        model.RemoveRange(i, count);
                        break;
                    }
                    default:
                    {
                        // Split then reconcatenate in order: an identity that exercises the split/concat machinery.
                        var i = indexSource % (n + 1);
                        var (left, right) = rope.Split(i);
                        left.ValidateInvariants();
                        right.ValidateInvariants();
                        rope = left.Concat(right);
                        break;
                    }
                }

                rope.ValidateInvariants();
                Assert.Equal(model.Count, rope.Count);
                Assert.Equal(model, rope.ToArray());
            }
        }, iter: 200);
    }

    /// <summary>
    /// Verifies that splitting at any index yields the two expected halves and that concatenating them restores
    /// the original, with both halves internally valid.
    /// </summary>
    [Fact]
    public void Rope_SplitConcat_RoundTrips()
    {
        Gen.Int.Array[0, 3000]
            .SelectMany(values => Gen.Int[0, values.Length].Select(index => (values, index)))
            .Sample(t =>
            {
                var (values, index) = t;
                var rope = Rope<int>.Create(values);
                var (left, right) = rope.Split(index);

                left.ValidateInvariants();
                right.ValidateInvariants();
                Assert.Equal(index, left.Count);
                Assert.Equal(values.Length - index, right.Count);
                Assert.Equal(values.Take(index), left.ToArray());
                Assert.Equal(values.Skip(index), right.ToArray());
                Assert.Equal(values, left.Concat(right).ToArray());
            }, iter: 500);
    }

    /// <summary>Verifies that a slice equals the corresponding sub-range of the source and stays internally valid.</summary>
    [Fact]
    public void Rope_Slice_MatchesSubRange()
    {
        Gen.Int.Array[0, 3000]
            .SelectMany(values => Gen.Int[0, values.Length]
                .SelectMany(index => Gen.Int[0, values.Length - index].Select(count => (values, index, count))))
            .Sample(t =>
            {
                var (values, index, count) = t;
                var slice = Rope<int>.Create(values).Slice(index, count);
                slice.ValidateInvariants();
                Assert.Equal(count, slice.Count);
                Assert.Equal(values.Skip(index).Take(count), slice.ToArray());
            }, iter: 500);
    }

    /// <summary>
    /// Verifies the text rope's line navigation against a <c>string.Split('\n')</c> model over generated text:
    /// line count, per-line content, line enumeration, and offset↔(line, column) round-trips at every offset.
    /// </summary>
    [Fact]
    public void TextRope_LineNavigation_MatchesStringModel()
    {
        TextGen.Sample(text =>
        {
            var rope = text.ToTextRope();
            var lines = text.Split('\n');

            Assert.Equal(text, rope.AsString());
            Assert.Equal(lines.Length, rope.LineCount());
            Assert.Equal(lines, rope.Lines().ToArray());

            for (var line = 0; line < lines.Length; line++)
                Assert.Equal(lines[line], rope.GetLine(line));

            for (var offset = 0; offset <= text.Length; offset++)
            {
                var (lineIndex, column) = rope.LineColumnOf(offset);
                Assert.Equal(text.Take(offset).Count(c => c == '\n'), lineIndex);
                Assert.Equal(offset, rope.OffsetOf(lineIndex, column));
            }
        }, iter: 500);
    }

    /// <summary>
    /// Verifies the closure-free value-type-predicate overloads of the measured rope's measure navigation
    /// (<c>SplitByMeasure&lt;TPredicate&gt;</c> and <c>TryLocateByMeasure&lt;TPredicate&gt;</c>) produce results
    /// identical to the delegate overloads across random sum-measured ropes and thresholds — so the struct path,
    /// including the now-generic within-chunk scan, is behaviorally equivalent to the known-correct delegate path.
    /// </summary>
    [Fact]
    public void MeasuredRope_StructPredicate_MatchesFuncPredicate()
    {
        Gen.Int[0, 40].Array[0, 500]
            .SelectMany(values => Gen.Int[-1, Math.Max(0, values.Sum()) + 1].Select(threshold => (values, threshold)))
            .Sample(t =>
            {
                var (values, threshold) = t;
                var rope = MeasuredRope<int, int, IntSumMeasure>.CreateRange(values);

                var (structLeft, structRight) = rope.SplitByMeasure(new SumAbove(threshold));
                var (funcLeft, funcRight) = rope.SplitByMeasure(measure => measure > threshold);
                Assert.Equal(funcLeft.ToArray(), structLeft.ToArray());
                Assert.Equal(funcRight.ToArray(), structRight.ToArray());

                var structFound = rope.TryLocateByMeasure(new SumAbove(threshold), out var si, out var sb, out var se);
                var funcFound = rope.TryLocateByMeasure(measure => measure > threshold, out var fi, out var fb, out var fe);
                Assert.Equal(funcFound, structFound);
                if (funcFound)
                {
                    Assert.Equal(fi, si);
                    Assert.Equal(fb, sb);
                    Assert.Equal(fe, se);
                }
            }, iter: 300);
    }

    /// <summary>A caller-supplied value-type predicate over an accumulated integer sum measure.</summary>
    private readonly struct SumAbove(int threshold) : IMeasurePredicate<int>
    {
        public bool Invoke(int measure) => measure > threshold;
    }
}
