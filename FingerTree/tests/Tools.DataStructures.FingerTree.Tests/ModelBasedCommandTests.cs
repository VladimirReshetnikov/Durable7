using CsCheck;
using Xunit;
using Deque = Tools.DataStructures.FingerTree.FingerTreeDeque<int>;
using IntRope = Tools.DataStructures.FingerTree.Rope<int>;
using SizeTree = Tools.DataStructures.FingerTree.FingerTree<int, int, Tools.DataStructures.FingerTree.SizeMeasure<int>>;
using FtSortedSet = Tools.DataStructures.FingerTree.SortedSet<int>;
using BclSortedSet = System.Collections.Generic.SortedSet<int>;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Model-based command-sequence tests (CsCheck <c>SampleModelBased</c>). Where the data-property tests generate
/// inputs, these generate <em>sequences of operations</em> and replay each against a reference model, checking
/// equivalence after every step. On failure CsCheck shrinks the operation list itself, reporting a minimal
/// failing program (e.g. "AddFirst 0; Truncate 0; RemoveLast") rather than just a minimal input. Each immutable
/// structure is wrapped in a mutable <see cref="Box{T}"/> so an operation can rebind the current version, which is
/// how CsCheck models a stateful object.
/// </summary>
public sealed class ModelBasedCommandTests
{
    private static readonly Gen<int> Value = Gen.Int[0, 1000];
    private static readonly Gen<int> RawIndex = Gen.Int[0, 1_000_000];
    private static readonly Gen<(int Raw, int Val)> IndexedValue =
        Gen.Select(RawIndex, Value, (raw, val) => (raw, val));
    private static readonly Gen<int[]> Run = Value.Array[0, 6];

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
            IndexedValue.Operation<Box<Deque>, List<int>>(
                t => $"SetItem raw={t.Raw} val={t.Val}",
                (b, t) => { if (b.Value.Count > 0) b.Value = b.Value.SetItem(t.Raw % b.Value.Count, t.Val); },
                (m, t) => { if (m.Count > 0) m[t.Raw % m.Count] = t.Val; }),
            RawIndex.Operation<Box<Deque>, List<int>>(
                raw => $"RemoveAt raw={raw}",
                (b, raw) => { if (b.Value.Count > 0) b.Value = b.Value.RemoveAt(raw % b.Value.Count); },
                (m, raw) => { if (m.Count > 0) m.RemoveAt(raw % m.Count); }),
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
            Run.Operation<Box<IntRope>, List<int>>(
                run => $"InsertRange {run.Length}",
                (b, run) => b.Value = b.Value.InsertRange(b.Value.Count, run.AsSpan()),
                (m, run) => m.AddRange(run)),
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

        initial.SampleModelBased(operations, (b, m) => b.Value.ToArray().SequenceEqual(m), iter: 200);
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
        };

        initial.SampleModelBased(
            operations,
            (b, m) =>
            {
                if (b.Value.Count != m.Count)
                    return false;
                var actual = new int[b.Value.Count];
                for (var i = 0; i < actual.Length; i++)
                    actual[i] = b.Value[i];
                return actual.SequenceEqual(m);
            },
            iter: 200);
    }

    /// <summary>A mutable cell holding the current immutable version, so an operation can rebind it.</summary>
    private sealed class Box<T>(T value)
    {
        public T Value = value;
    }
}
