// Tests for the model based command.

using CsCheck;
using Xunit;
using Deque = Durable7.FingerTree.FingerTreeDeque<int>;
using IntRope = Durable7.FingerTree.Rope<int>;
using SizeTree = Durable7.FingerTree.FingerTree<int, int, Durable7.FingerTree.SizeMeasure<int>>;
using MRope = Durable7.FingerTree.MeasuredRope<int, int, Durable7.FingerTree.Tests.IntSumMeasure>;
using FtSortedSet = Durable7.FingerTree.SortedSet<int>;
using BclSortedSet = System.Collections.Generic.SortedSet<int>;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Model-based command-sequence tests (CsCheck <c>SampleModelBased</c>). Where the data-property tests generate
/// inputs, these generate <em>sequences of operations</em> and replay each against a reference model, checking
/// equivalence after every step. On failure CsCheck shrinks the operation list itself, reporting a minimal
/// failing program (e.g. "RemoveLast; AddFirst 50") rather than just a minimal input. Each immutable structure is
/// wrapped in a mutable <see cref="Box{T}"/> so an operation can rebind the current version, which is how CsCheck
/// models a stateful object. Index-bearing operations map a generated raw value into range from the live count, so
/// the actual and the model stay in lockstep (the equivalence check after every step keeps their counts equal).
/// </summary>
public sealed class ModelBasedCommandTests
{
    private static readonly Gen<int> Value = Gen.Int[0, 1000];
    private static readonly Gen<int> RawIndex = Gen.Int[0, 1_000_000];
    private static readonly Gen<(int Raw, int Val)> IndexedValue =
        Gen.Select(RawIndex, Value, (raw, val) => (raw, val));
    private static readonly Gen<int[]> Run = Value.Array[0, 6];
    private static readonly Gen<(int Raw, int[] Run)> IndexedRun =
        Gen.Select(RawIndex, Run, (raw, run) => (raw, run));
    private static readonly Gen<(int Start, int Len)> RangeSpec =
        Gen.Select(RawIndex, Gen.Int[0, 200], (start, len) => (start, len));

    /// <summary>Replays generated command sequences against the tuned deque, comparing to a <see cref="List{T}"/>.</summary>
    [Fact]
    public void Deque_CommandSequences_MatchListModel()
    {
        var initial = Gen.Int[0, 1000].Array[0, 40]
            .Select(seed => (new Box<Deque>(Deque.CreateRange(seed)), new List<int>(seed)));

        var operations = new[]
        {
            Value.Operation<Box<Deque>, List<int>>(
                i => $"AddFirst {i}", (b, i) => b.Value = b.Value.AddFirst(i), (m, i) => m.Insert(0, i)),
            Value.Operation<Box<Deque>, List<int>>(
                i => $"AddLast {i}", (b, i) => b.Value = b.Value.AddLast(i), (m, i) => m.Add(i)),
            Gen.Operation<Box<Deque>, List<int>>(
                "RemoveFirst",
                b => { if (!b.Value.IsEmpty) b.Value = b.Value.RemoveFirst(); },
                m => { if (m.Count > 0) m.RemoveAt(0); }),
            Gen.Operation<Box<Deque>, List<int>>(
                "RemoveLast",
                b => { if (!b.Value.IsEmpty) b.Value = b.Value.RemoveLast(); },
                m => { if (m.Count > 0) m.RemoveAt(m.Count - 1); }),
            IndexedValue.Operation<Box<Deque>, List<int>>(
                t => $"InsertAt raw={t.Raw} val={t.Val}",
                (b, t) => b.Value = b.Value.InsertAt(t.Raw % (b.Value.Count + 1), t.Val),
                (m, t) => m.Insert(t.Raw % (m.Count + 1), t.Val)),
            IndexedRun.Operation<Box<Deque>, List<int>>(
                t => $"InsertRange raw={t.Raw} len={t.Run.Length}",
                (b, t) => b.Value = b.Value.InsertRange(t.Raw % (b.Value.Count + 1), t.Run),
                (m, t) => m.InsertRange(t.Raw % (m.Count + 1), t.Run)),
            IndexedValue.Operation<Box<Deque>, List<int>>(
                t => $"SetItem raw={t.Raw} val={t.Val}",
                (b, t) => { if (b.Value.Count > 0) b.Value = b.Value.SetItem(t.Raw % b.Value.Count, t.Val); },
                (m, t) => { if (m.Count > 0) m[t.Raw % m.Count] = t.Val; }),
            RawIndex.Operation<Box<Deque>, List<int>>(
                raw => $"RemoveAt raw={raw}",
                (b, raw) => { if (b.Value.Count > 0) b.Value = b.Value.RemoveAt(raw % b.Value.Count); },
                (m, raw) => { if (m.Count > 0) m.RemoveAt(raw % m.Count); }),
            RangeSpec.Operation<Box<Deque>, List<int>>(
                t => $"RemoveRange start={t.Start} len={t.Len}",
                (b, t) => { var (s, c) = ClampRange(t, b.Value.Count); b.Value = b.Value.RemoveRange(s, c); },
                (m, t) => { var (s, c) = ClampRange(t, m.Count); m.RemoveRange(s, c); }),
            RangeSpec.Operation<Box<Deque>, List<int>>(
                t => $"GetRangeWindow start={t.Start} len={t.Len}",
                (b, t) => { var (s, c) = ClampRange(t, b.Value.Count); b.Value = b.Value.GetRange(s, c); },
                (m, t) => { var (s, c) = ClampRange(t, m.Count); ReplaceWith(m, m.GetRange(s, c)); }),
            Run.Operation<Box<Deque>, List<int>>(
                run => $"Concat [{run.Length}]",
                (b, run) => b.Value = b.Value.Concat(Deque.CreateRange(run)),
                (m, run) => m.AddRange(run)),
            RawIndex.Operation<Box<Deque>, List<int>>(
                raw => $"TruncateTo raw={raw}",
                (b, raw) => b.Value = b.Value.SplitAt(raw % (b.Value.Count + 1)).Left,
                (m, raw) => { var i = raw % (m.Count + 1); m.RemoveRange(i, m.Count - i); }),
        };

        initial.SampleModelBased(operations, (b, m) => b.Value.Count == m.Count && b.Value.SequenceEqual(m), iter: 200);
    }

    /// <summary>Replays generated command sequences against the rope, validating chunk invariants each step.</summary>
    [Fact]
    public void Rope_CommandSequences_MatchListModel()
    {
        var initial = Gen.Int[0, 1000].Array[0, 40]
            .Select(seed => (new Box<IntRope>(IntRope.Create(seed)), new List<int>(seed)));

        var operations = new[]
        {
            Value.Operation<Box<IntRope>, List<int>>(
                i => $"AddFirst {i}", (b, i) => b.Value = b.Value.AddFirst(i), (m, i) => m.Insert(0, i)),
            Value.Operation<Box<IntRope>, List<int>>(
                i => $"AddLast {i}", (b, i) => b.Value = b.Value.AddLast(i), (m, i) => m.Add(i)),
            Gen.Operation<Box<IntRope>, List<int>>(
                "RemoveFirst",
                b => { if (!b.Value.IsEmpty) b.Value = b.Value.RemoveFirst(); },
                m => { if (m.Count > 0) m.RemoveAt(0); }),
            Gen.Operation<Box<IntRope>, List<int>>(
                "RemoveLast",
                b => { if (!b.Value.IsEmpty) b.Value = b.Value.RemoveLast(); },
                m => { if (m.Count > 0) m.RemoveAt(m.Count - 1); }),
            IndexedValue.Operation<Box<IntRope>, List<int>>(
                t => $"Insert raw={t.Raw} val={t.Val}",
                (b, t) => b.Value = b.Value.Insert(t.Raw % (b.Value.Count + 1), t.Val),
                (m, t) => m.Insert(t.Raw % (m.Count + 1), t.Val)),
            IndexedValue.Operation<Box<IntRope>, List<int>>(
                t => $"SetItem raw={t.Raw} val={t.Val}",
                (b, t) => { if (b.Value.Count > 0) b.Value = b.Value.SetItem(t.Raw % b.Value.Count, t.Val); },
                (m, t) => { if (m.Count > 0) m[t.Raw % m.Count] = t.Val; }),
            RawIndex.Operation<Box<IntRope>, List<int>>(
                raw => $"RemoveAt raw={raw}",
                (b, raw) => { if (b.Value.Count > 0) b.Value = b.Value.RemoveAt(raw % b.Value.Count); },
                (m, raw) => { if (m.Count > 0) m.RemoveAt(raw % m.Count); }),
            IndexedRun.Operation<Box<IntRope>, List<int>>(
                t => $"InsertRange raw={t.Raw} len={t.Run.Length}",
                (b, t) => b.Value = b.Value.InsertRange(t.Raw % (b.Value.Count + 1), t.Run.AsSpan()),
                (m, t) => m.InsertRange(t.Raw % (m.Count + 1), t.Run)),
            RangeSpec.Operation<Box<IntRope>, List<int>>(
                t => $"RemoveRange start={t.Start} len={t.Len}",
                (b, t) => { var (s, c) = ClampRange(t, b.Value.Count); b.Value = b.Value.RemoveRange(s, c); },
                (m, t) => { var (s, c) = ClampRange(t, m.Count); m.RemoveRange(s, c); }),
            RangeSpec.Operation<Box<IntRope>, List<int>>(
                t => $"SliceWindow start={t.Start} len={t.Len}",
                (b, t) => { var (s, c) = ClampRange(t, b.Value.Count); b.Value = b.Value.Slice(s, c); },
                (m, t) => { var (s, c) = ClampRange(t, m.Count); ReplaceWith(m, m.GetRange(s, c)); }),
            Run.Operation<Box<IntRope>, List<int>>(
                run => $"Concat [{run.Length}]",
                (b, run) => b.Value = b.Value.Concat(IntRope.Create(run)),
                (m, run) => m.AddRange(run)),
            RawIndex.Operation<Box<IntRope>, List<int>>(
                raw => $"TruncateTo raw={raw}",
                (b, raw) => b.Value = b.Value.Split(raw % (b.Value.Count + 1)).Left,
                (m, raw) => { var i = raw % (m.Count + 1); m.RemoveRange(i, m.Count - i); }),
        };

        initial.SampleModelBased(
            operations,
            (b, m) =>
            {
                b.Value.ValidateInvariants();
                return b.Value.Count == m.Count && b.Value.ToArray().SequenceEqual(m);
            },
            iter: 200);
    }

    /// <summary>
    /// Replays generated command sequences against the general measured tree (size measure), driving the
    /// lazy-memoized spine through prepend/append/view/concat/split and comparing the flattened sequence to a
    /// list model — so a forcing or memoization bug surfaces as a minimal failing operation sequence.
    /// </summary>
    [Fact]
    public void MeasuredTree_CommandSequences_MatchListModel()
    {
        var initial = Gen.Int[0, 1000].Array[0, 40]
            .Select(seed => (new Box<SizeTree>(SizeTree.CreateRange(seed)), new List<int>(seed)));

        var operations = new[]
        {
            Value.Operation<Box<SizeTree>, List<int>>(
                i => $"Prepend {i}", (b, i) => b.Value = b.Value.Prepend(i), (m, i) => m.Insert(0, i)),
            Value.Operation<Box<SizeTree>, List<int>>(
                i => $"Append {i}", (b, i) => b.Value = b.Value.Append(i), (m, i) => m.Add(i)),
            Gen.Operation<Box<SizeTree>, List<int>>(
                "ViewLeft",
                b => { if (b.Value.TryViewLeft(out _, out var rest)) b.Value = rest; },
                m => { if (m.Count > 0) m.RemoveAt(0); }),
            Gen.Operation<Box<SizeTree>, List<int>>(
                "ViewRight",
                b => { if (b.Value.TryViewRight(out _, out var rest)) b.Value = rest; },
                m => { if (m.Count > 0) m.RemoveAt(m.Count - 1); }),
            Run.Operation<Box<SizeTree>, List<int>>(
                run => $"Concat [{run.Length}]",
                (b, run) => b.Value = b.Value.Concat(SizeTree.CreateRange(run)),
                (m, run) => m.AddRange(run)),
            RawIndex.Operation<Box<SizeTree>, List<int>>(
                raw => $"TruncateTo raw={raw}",
                (b, raw) =>
                {
                    var count = b.Value.ToArray().Length;
                    var index = raw % (count + 1);
                    b.Value = b.Value.Split(measured => measured > index).Left;
                },
                (m, raw) => { var i = raw % (m.Count + 1); m.RemoveRange(i, m.Count - i); }),
        };

        initial.SampleModelBased(
            operations,
            (b, m) =>
            {
                if (!b.Value.ToArray().SequenceEqual(m))
                    return false;

                // For the size measure, `measured > k` locates the element at index k, so TryLocate/TrySplitFind
                // should find m[k] with k elements before it. Sample a handful of positions plus the just-past-end
                // boundary (where both must report no hit) to keep the per-step cost linear.
                var count = m.Count;
                foreach (var k in new[] { 0, count / 4, count / 2, 3 * count / 4, count - 1, count }.Distinct())
                {
                    if (k < 0)
                        continue;

                    var located = b.Value.TryLocate(measured => measured > k, out var measureBefore, out var found);
                    var split = b.Value.TrySplitFind(measured => measured > k, out var left, out var hit, out var right);

                    if (k < count)
                    {
                        if (!located || measureBefore != k || found != m[k])
                            return false;
                        if (!split || hit != m[k]
                            || !left.ToArray().SequenceEqual(m.Take(k))
                            || !right.ToArray().SequenceEqual(m.Skip(k + 1)))
                            return false;
                    }
                    else if (located || split)
                    {
                        return false;
                    }
                }

                return true;
            },
            iter: 200);
    }

    /// <summary>
    /// Replays generated command sequences against the measured rope (integer sum measure), asserting after every
    /// step that both the flattened sequence AND the cached <c>Measure</c> match the model — so a measure
    /// memoization or recomputation bug surfaces directly, which the unmeasured-tree test cannot see.
    /// </summary>
    [Fact]
    public void MeasuredRope_CommandSequences_MatchListAndMeasure()
    {
        var element = Gen.Int[0, 100];   // bounded so the running sum stays within int across a sequence
        var initial = element.Array[0, 40]
            .Select(seed => (new Box<MRope>(MRope.Create(seed)), new List<int>(seed)));

        var operations = new[]
        {
            element.Operation<Box<MRope>, List<int>>(
                i => $"AddFirst {i}", (b, i) => b.Value = b.Value.AddFirst(i), (m, i) => m.Insert(0, i)),
            element.Operation<Box<MRope>, List<int>>(
                i => $"AddLast {i}", (b, i) => b.Value = b.Value.AddLast(i), (m, i) => m.Add(i)),
            Gen.Operation<Box<MRope>, List<int>>(
                "RemoveFirst",
                b => { if (!b.Value.IsEmpty) b.Value = b.Value.RemoveFirst(); },
                m => { if (m.Count > 0) m.RemoveAt(0); }),
            Gen.Operation<Box<MRope>, List<int>>(
                "RemoveLast",
                b => { if (!b.Value.IsEmpty) b.Value = b.Value.RemoveLast(); },
                m => { if (m.Count > 0) m.RemoveAt(m.Count - 1); }),
            Gen.Select(RawIndex, element, (raw, val) => (raw, val)).Operation<Box<MRope>, List<int>>(
                t => $"Insert raw={t.raw} val={t.val}",
                (b, t) => b.Value = b.Value.Insert(t.raw % (b.Value.Count + 1), t.val),
                (m, t) => m.Insert(t.raw % (m.Count + 1), t.val)),
            RawIndex.Operation<Box<MRope>, List<int>>(
                raw => $"RemoveAt raw={raw}",
                (b, raw) => { if (b.Value.Count > 0) b.Value = b.Value.RemoveAt(raw % b.Value.Count); },
                (m, raw) => { if (m.Count > 0) m.RemoveAt(raw % m.Count); }),
            Gen.Select(RawIndex, element, (raw, val) => (raw, val)).Operation<Box<MRope>, List<int>>(
                t => $"SetItem raw={t.raw} val={t.val}",
                (b, t) => { if (b.Value.Count > 0) b.Value = b.Value.SetItem(t.raw % b.Value.Count, t.val); },
                (m, t) => { if (m.Count > 0) m[t.raw % m.Count] = t.val; }),
            element.Array[0, 6].Operation<Box<MRope>, List<int>>(
                run => $"Concat [{run.Length}]",
                (b, run) => b.Value = b.Value.Concat(MRope.Create(run)),
                (m, run) => m.AddRange(run)),
            RawIndex.Operation<Box<MRope>, List<int>>(
                raw => $"TruncateTo raw={raw}",
                (b, raw) => b.Value = b.Value.Split(raw % (b.Value.Count + 1)).Left,
                (m, raw) => { var i = raw % (m.Count + 1); m.RemoveRange(i, m.Count - i); }),
        };

        initial.SampleModelBased(
            operations,
            (b, m) =>
            {
                b.Value.ValidateInvariants();
                if (b.Value.Count != m.Count || !b.Value.ToArray().SequenceEqual(m) || b.Value.Measure != m.Sum())
                    return false;

                // The closure-free value-type-predicate locate must agree with the delegate locate at every
                // reached state, including the now-generic within-chunk scan.
                var sum = m.Sum();
                foreach (var threshold in new[] { -1, sum / 2, sum })
                {
                    var structFound = b.Value.TryLocateByMeasure(new SumAboveInt(threshold), out var si, out var sb, out var se);
                    var funcFound = b.Value.TryLocateByMeasure(measure => measure > threshold, out var fi, out var fb, out var fe);
                    if (structFound != funcFound)
                        return false;
                    if (funcFound && (si != fi || sb != fb || se != fe))
                        return false;
                }

                return true;
            },
            iter: 200);
    }

    /// <summary>Replays generated command sequences against the sorted set, comparing to the framework set.</summary>
    [Fact]
    public void SortedSet_CommandSequences_MatchFrameworkSet()
    {
        var element = Gen.Int[-30, 30];
        var elementRun = element.Array[0, 6];
        var initial = element.Array[0, 40]
            .Select(seed => (new Box<FtSortedSet>(FtSortedSet.CreateRange(seed)), new BclSortedSet(seed)));

        var operations = new[]
        {
            element.Operation<Box<FtSortedSet>, BclSortedSet>(
                i => $"Add {i}", (b, i) => b.Value = b.Value.Add(i), (m, i) => m.Add(i)),
            element.Operation<Box<FtSortedSet>, BclSortedSet>(
                i => $"Remove {i}", (b, i) => b.Value = b.Value.Remove(i), (m, i) => m.Remove(i)),
            elementRun.Operation<Box<FtSortedSet>, BclSortedSet>(
                run => $"Union [{run.Length}]",
                (b, run) => b.Value = b.Value.Union(FtSortedSet.CreateRange(run)),
                (m, run) => m.UnionWith(run)),
            elementRun.Operation<Box<FtSortedSet>, BclSortedSet>(
                run => $"Intersect [{run.Length}]",
                (b, run) => b.Value = b.Value.Intersect(FtSortedSet.CreateRange(run)),
                (m, run) => m.IntersectWith(run)),
            elementRun.Operation<Box<FtSortedSet>, BclSortedSet>(
                run => $"Except [{run.Length}]",
                (b, run) => b.Value = b.Value.Except(FtSortedSet.CreateRange(run)),
                (m, run) => m.ExceptWith(run)),
            elementRun.Operation<Box<FtSortedSet>, BclSortedSet>(
                run => $"SymmetricExcept [{run.Length}]",
                (b, run) => b.Value = b.Value.SymmetricExcept(FtSortedSet.CreateRange(run)),
                (m, run) => m.SymmetricExceptWith(run)),
        };

        initial.SampleModelBased(
            operations,
            (b, m) =>
            {
                if (b.Value.Count != m.Count)
                    return false;
                var sorted = m.ToArray();   // BCL set enumerates in sorted order
                var actual = new int[b.Value.Count];
                for (var i = 0; i < actual.Length; i++)
                    actual[i] = b.Value[i];
                if (!actual.SequenceEqual(sorted))
                    return false;

                for (var v = -31; v <= 31; v++)
                {
                    var present = m.Contains(v);
                    if (b.Value.Contains(v) != present)
                        return false;
                    if (present && b.Value.IndexOf(v) != sorted.Count(x => x < v))
                        return false;
                    if (!NeighborMatches(sorted.Where(x => x <= v).LastOrNull(), b.Value.TryFloor(v, out var floor), floor)
                        || !NeighborMatches(sorted.Where(x => x >= v).FirstOrNull(), b.Value.TryCeiling(v, out var ceil), ceil)
                        || !NeighborMatches(sorted.Where(x => x < v).LastOrNull(), b.Value.TryLower(v, out var low), low)
                        || !NeighborMatches(sorted.Where(x => x > v).FirstOrNull(), b.Value.TryHigher(v, out var high), high))
                        return false;
                }

                return true;
            },
            iter: 200);
    }

    /// <summary>Returns whether a navigable-query result (presence flag and value) matches the model's expected
    /// neighbor (present iff <paramref name="expected"/> has a value, and equal when present).</summary>
    private static bool NeighborMatches(int? expected, bool found, int actual) =>
        expected.HasValue == found && (!expected.HasValue || expected.Value == actual);

    /// <summary>
    /// Replays generated command sequences against the deque's order-maintaining sorted surface
    /// (<c>InsertSorted</c>/<c>RemoveAllSorted</c>), checking after every step that the sequence stays sorted and
    /// that the signpost-cached search verbs (<c>SortedContains</c>/<c>SortedLowerBound</c>/<c>SortedUpperBound</c>)
    /// agree with a binary-search model at every probe — so a stale rightmost-element signpost is caught.
    /// </summary>
    [Fact]
    public void SortedDeque_CommandSequences_MatchSortedModel()
    {
        var element = Gen.Int[0, 20];
        var initial = element.Array[0, 30].Select(seed =>
        {
            var deque = Deque.Empty;
            foreach (var x in seed)
                deque = deque.InsertSorted(x);
            return (new Box<Deque>(deque), new List<int>(seed));
        });

        var operations = new[]
        {
            element.Operation<Box<Deque>, List<int>>(
                i => $"InsertSorted {i}", (b, i) => b.Value = b.Value.InsertSorted(i), (m, i) => m.Add(i)),
            element.Operation<Box<Deque>, List<int>>(
                i => $"RemoveAllSorted {i}",
                (b, i) => b.Value = b.Value.RemoveAllSorted(i),
                (m, i) => m.RemoveAll(x => x == i)),
        };

        initial.SampleModelBased(
            operations,
            (b, m) =>
            {
                var sorted = m.OrderBy(x => x).ToArray();
                if (!b.Value.SequenceEqual(sorted))
                    return false;
                for (var v = -1; v <= 21; v++)
                {
                    var lower = sorted.Count(x => x < v);    // SortedLowerBound: first index >= v
                    var upper = sorted.Count(x => x <= v);   // SortedUpperBound: first index > v
                    var present = upper > lower;

                    if (b.Value.SortedContains(v) != present
                        || b.Value.SortedLowerBound(v) != lower
                        || b.Value.SortedUpperBound(v) != upper
                        || b.Value.SortedBinarySearch(v) != (present ? lower : ~lower))
                        return false;

                    var atLower = b.Value.SplitAtSortedLowerBound(v);
                    if (!atLower.Left.SequenceEqual(sorted.Take(lower)) || !atLower.Right.SequenceEqual(sorted.Skip(lower)))
                        return false;

                    var atUpper = b.Value.SplitAtSortedUpperBound(v);
                    if (!atUpper.Left.SequenceEqual(sorted.Take(upper)) || !atUpper.Right.SequenceEqual(sorted.Skip(upper)))
                        return false;

                    var equalRange = b.Value.SplitAtSortedEqualRange(v);
                    if (!equalRange.Before.SequenceEqual(sorted.Take(lower))
                        || !equalRange.Range.SequenceEqual(sorted.Skip(lower).Take(upper - lower))
                        || !equalRange.After.SequenceEqual(sorted.Skip(upper)))
                        return false;
                }

                return true;
            },
            iter: 200);
    }

    /// <summary>Clamps a generated (start, length) into a valid sub-range of a sequence of the given count: a
    /// non-negative start in <c>0..count</c> and a length in <c>0..count-start</c> (so it rarely degenerates to a
    /// no-op).</summary>
    private static (int Start, int Count) ClampRange((int Start, int Len) spec, int count)
    {
        var start = spec.Start % (count + 1);
        return (start, Math.Min(spec.Len, count - start));
    }

    /// <summary>Mutates <paramref name="list"/> in place to hold exactly <paramref name="replacement"/>.</summary>
    private static void ReplaceWith(List<int> list, List<int> replacement)
    {
        list.Clear();
        list.AddRange(replacement);
    }

    /// <summary>A mutable cell holding the current immutable version, so an operation can rebind it.</summary>
    private sealed class Box<T>(T value)
    {
        /// <summary>Gets the stored value.</summary>
        public T Value = value;
    }

    /// <summary>A value-type predicate over an accumulated integer sum measure, used to check the measured rope's
    /// closure-free locate against the delegate locate.</summary>
    private readonly struct SumAboveInt(int threshold) : IMeasurePredicate<int>
    {
        /// <summary>Runs the operation.</summary>
        public bool Invoke(int measure) => measure > threshold;
    }
}

/// <summary>An integer monoidal sum measure (<c>Measure(x) = x</c>, combine adds), so a measured rope's
/// whole-sequence measure equals the sum of its elements — used to assert measure consistency in the model-based
/// command tests.</summary>
internal readonly struct IntSumMeasure : Durable7.FingerTree.IMeasure<int, int>
{
    /// <summary>Gets the identity: the measure of an empty tree.</summary>
    public static int Empty => 0;

    /// <summary>Returns the measure of one element.</summary>
    public static int Measure(int value) => value;

    /// <summary>Combines two measures in order. Must be associative; it need not be commutative.</summary>
    public static int Combine(int left, int right) => left + right;
}
