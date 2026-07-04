using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies <see cref="ReversibleDeque{T}"/>: deque/index/split/concat semantics, O(1) reverse correctness
/// (including double-reverse, reverse-then-operate, and mixed-orientation concatenation), persistence, and
/// internal invariants — culminating in a randomized history that interleaves reverse with every operation.
/// </summary>
public sealed class ReversibleDequeTests
{
    private static void AssertSequence(IReadOnlyList<int> expected, ReversibleDeque<int> actual)
    {
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected.Count == 0, actual.IsEmpty);
        Assert.Equal(expected, actual.ToArray());
        Assert.Equal(expected, actual);
        actual.ValidateInvariants();
        for (var i = 0; i < expected.Count; i++)
            Assert.Equal(expected[i], actual[i]);
        if (expected.Count > 0)
        {
            Assert.Equal(expected[0], actual.First);
            Assert.Equal(expected[^1], actual.Last);
        }
    }

    /// <summary>Verifies construction, endpoint reads, and persistence of endpoint operations.</summary>
    [Fact]
    public void AddAndRemove_BuildExpectedOrderAndPreserveOriginal()
    {
        var original = ReversibleDeque<int>.Create(2, 3);
        var updated = original.AddFirst(1).AddLast(4);

        AssertSequence(new[] { 2, 3 }, original);
        AssertSequence(new[] { 1, 2, 3, 4 }, updated);
        AssertSequence(new[] { 2, 3, 4 }, updated.RemoveFirst());
        AssertSequence(new[] { 1, 2, 3 }, updated.RemoveLast());
    }

    /// <summary>Verifies single reverse, double-reverse identity, and that the original is preserved.</summary>
    [Fact]
    public void Reverse_IsCorrectAndInvolutive()
    {
        for (var size = 0; size <= 40; size++)
        {
            var values = Enumerable.Range(0, size).ToArray();
            var deque = ReversibleDeque<int>.Create(values);

            var reversed = deque.Reverse();
            AssertSequence(values.Reverse().ToArray(), reversed);
            AssertSequence(values, deque);                 // original unchanged
            AssertSequence(values, reversed.Reverse());    // double reverse == original
        }
    }

    /// <summary>Verifies operations on a reversed deque behave against the reversed sequence.</summary>
    [Fact]
    public void OperationsAfterReverse_MatchReversedSequence()
    {
        var deque = ReversibleDeque<int>.Create(1, 2, 3, 4, 5).Reverse(); // logical [5,4,3,2,1]

        Assert.Equal(5, deque.First);
        Assert.Equal(1, deque.Last);
        Assert.Equal(3, deque[2]);
        AssertSequence(new[] { 9, 5, 4, 3, 2, 1 }, deque.AddFirst(9));
        AssertSequence(new[] { 5, 4, 3, 2, 1, 9 }, deque.AddLast(9));
        AssertSequence(new[] { 4, 3, 2, 1 }, deque.RemoveFirst());
        AssertSequence(new[] { 5, 4, 3, 2 }, deque.RemoveLast());
        AssertSequence(new[] { 5, 4, 9, 2, 1 }, deque.SetItem(2, 9));
        AssertSequence(new[] { 5, 4, 9, 3, 2, 1 }, deque.InsertAt(2, 9));
        AssertSequence(new[] { 5, 4, 2, 1 }, deque.RemoveAt(2));

        var (left, right) = deque.SplitAt(2);
        AssertSequence(new[] { 5, 4 }, left);
        AssertSequence(new[] { 3, 2, 1 }, right);
    }

    /// <summary>
    /// Verifies concatenation in all four orientation combinations stays correct — the key test for the
    /// reversal-aware glue, since a reversed operand must concatenate in O(log min) without reification.
    /// </summary>
    [Fact]
    public void Concat_AllOrientationCombinations_AreCorrect()
    {
        int[] sizes = [0, 1, 2, 3, 8, 21, 50];
        foreach (var a in sizes)
        {
            foreach (var b in sizes)
            {
                var av = Enumerable.Range(0, a).ToArray();
                var bv = Enumerable.Range(100, b).ToArray();
                var da = ReversibleDeque<int>.Create(av);
                var db = ReversibleDeque<int>.Create(bv);

                AssertSequence([.. av, .. bv], da.Concat(db));
                AssertSequence([.. av.Reverse(), .. bv], da.Reverse().Concat(db));
                AssertSequence([.. av, .. bv.Reverse()], da.Concat(db.Reverse()));
                AssertSequence([.. av.Reverse(), .. bv.Reverse()], da.Reverse().Concat(db.Reverse()));
            }
        }
    }

    /// <summary>Verifies split reconstruction at every index, including on a reversed deque.</summary>
    [Fact]
    public void SplitAt_ReconstructsAtEveryIndex()
    {
        for (var size = 0; size <= 30; size++)
        {
            var values = Enumerable.Range(0, size).ToArray();
            foreach (var deque in new[] { ReversibleDeque<int>.Create(values), ReversibleDeque<int>.Create(values).Reverse() })
            {
                var sequence = deque.ToArray();
                for (var index = 0; index <= size; index++)
                {
                    var (left, right) = deque.SplitAt(index);
                    Assert.Equal(index, left.Count);
                    Assert.Equal(size - index, right.Count);
                    AssertSequence(sequence, left.Concat(right));
                }
            }
        }
    }

    /// <summary>Verifies a large reversed deque keeps multiple nested levels valid and indexable.</summary>
    [Fact]
    public void LargeReverse_StaysValidAndIndexable()
    {
        const int size = 5000;
        var expected = Enumerable.Range(0, size).Reverse().ToArray();
        var deque = ReversibleDeque<int>.CreateRange(Enumerable.Range(0, size)).Reverse();

        deque.ValidateInvariants();
        Assert.Equal(size, deque.Count);
        for (var i = 0; i < size; i += 137)
            Assert.Equal(expected[i], deque[i]);
        Assert.Equal(expected, deque.ToArray());
        Assert.Equal(expected, deque);
    }

    /// <summary>Verifies enumeration streams through reversed nodes without materializing the full sequence.</summary>
    [Fact]
    public void Enumeration_StreamsReversedDequeWithoutMaterializingArray()
    {
        const int size = 1 << 16;
        var deque = ReversibleDeque<int>.CreateRange(Enumerable.Range(0, size)).Reverse();
        var expectedSum = ((long)(size - 1) * size) / 2;

        var warmupSum = 0L;
        foreach (var item in deque)
            warmupSum += item;
        Assert.Equal(expectedSum, warmupSum);

        var before = GC.GetAllocatedBytesForCurrentThread();
        var sum = 0L;
        foreach (var item in deque)
            sum += item;
        var bytes = GC.GetAllocatedBytesForCurrentThread() - before;

        Assert.Equal(expectedSum, sum);
        Assert.InRange(bytes, 1, 16_384);
    }

    /// <summary>
    /// Runs a deterministic mixed history interleaving reverse with every operation, validating internal
    /// invariants after each step and comparing against a list model.
    /// </summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    [InlineData(4)]
    public void MixedHistoryWithReverse_MatchesListModel(int seed)
    {
        var random = new Random(seed);
        var model = new List<int>();
        var deque = ReversibleDeque<int>.Empty;

        for (var step = 0; step < 150; step++)
        {
            switch (random.Next(10))
            {
                case 0:
                    model.Insert(0, step);
                    deque = deque.AddFirst(step);
                    break;
                case 1:
                    model.Add(step);
                    deque = deque.AddLast(step);
                    break;
                case 2 when model.Count > 0:
                    model.RemoveAt(0);
                    deque = deque.RemoveFirst();
                    break;
                case 3 when model.Count > 0:
                    model.RemoveAt(model.Count - 1);
                    deque = deque.RemoveLast();
                    break;
                case 4:
                {
                    var index = random.Next(model.Count + 1);
                    model.Insert(index, step);
                    deque = deque.InsertAt(index, step);
                    break;
                }
                case 5 when model.Count > 0:
                {
                    var index = random.Next(model.Count);
                    model.RemoveAt(index);
                    deque = deque.RemoveAt(index);
                    break;
                }
                case 6 when model.Count > 0:
                {
                    var index = random.Next(model.Count);
                    model[index] = step;
                    deque = deque.SetItem(index, step);
                    break;
                }
                case 7:
                    model.Reverse();
                    deque = deque.Reverse();
                    break;
                case 8:
                {
                    var index = random.Next(model.Count + 1);
                    var (left, right) = deque.SplitAt(index);
                    deque = left.Concat(right);   // identity round-trip
                    break;
                }
                default:
                {
                    // Rotate via split then swapped concat, mirrored in the model.
                    var index = random.Next(model.Count + 1);
                    var (left, right) = deque.SplitAt(index);
                    deque = right.Concat(left);
                    var rotated = model.Skip(index).Concat(model.Take(index)).ToList();
                    model.Clear();
                    model.AddRange(rotated);
                    break;
                }
            }

            AssertSequence(model, deque);
        }
    }

    /// <summary>Verifies whole-deque reverse allocates only a constant amount regardless of size.</summary>
    [Fact]
    public void Reverse_AllocatesConstantBytes()
    {
        const int size = 1 << 16;
        var deque = ReversibleDeque<int>.CreateRange(Enumerable.Range(0, size));

        deque.Reverse();   // warm up JIT outside the measured window

        var before = GC.GetAllocatedBytesForCurrentThread();
        var reversed = deque.Reverse();
        var bytes = GC.GetAllocatedBytesForCurrentThread() - before;

        Assert.False(reversed.IsEmpty);
        Assert.InRange(bytes, 1, 512);   // O(1): mirror the root plus the wrapper, independent of size
    }

    /// <summary>
    /// Targets the subtle reversed-multi-level-middle path: a <see cref="ReversibleDeque{T}.SetItem"/> deep
    /// inside a large reversed deque descends through several reversed inner middles and must rebuild a
    /// logically correct (forward) tree, after which the result must keep operating correctly (re-reverse,
    /// split/rejoin, endpoint pushes). This pins the adjudication that operating on a reversed inner middle
    /// never desynchronizes orientation between a deep level and its middle subtree.
    /// </summary>
    [Fact]
    public void SetItem_DeepInReversedMiddle_StaysConsistent()
    {
        const int size = 300;   // large enough for a multi-level middle (a middle-of-a-middle), all reversed
        var model = Enumerable.Range(0, size).Reverse().ToList();
        var deque = ReversibleDeque<int>.CreateRange(Enumerable.Range(0, size)).Reverse();
        AssertSequence(model, deque);

        // Hit the prefix region, several deep-middle positions, and the suffix region.
        foreach (var index in new[] { 0, 1, 2, 7, size / 3, size / 2, 2 * size / 3, size - 3, size - 2, size - 1 })
        {
            var value = -index - 1;
            model[index] = value;
            deque = deque.SetItem(index, value);
            AssertSequence(model, deque);   // full sequence + internal invariants after each mutation
        }

        // The mutated, reversed-origin tree must still behave under every other operation.
        AssertSequence(((IEnumerable<int>)model).Reverse().ToArray(), deque.Reverse());
        var (left, right) = deque.SplitAt(size / 2);
        AssertSequence(model, left.Concat(right));
        AssertSequence([.. model, 777], deque.AddLast(777));
        AssertSequence([999, .. model], deque.AddFirst(999));
    }

    /// <summary>
    /// Verifies <see cref="ReversibleDeque{T}.SetItem"/> on a reversed deque is persistent: deriving a mutated
    /// version leaves the reversed source version untouched (the reversal bit is a non-mutating view).
    /// </summary>
    [Fact]
    public void Reverse_ThenSetItem_PreservesOriginalVersion()
    {
        const int size = 200;
        var reversedModel = Enumerable.Range(0, size).Reverse().ToArray();
        var reversed = ReversibleDeque<int>.CreateRange(Enumerable.Range(0, size)).Reverse();

        var mutated = reversed.SetItem(size / 2, -1);

        AssertSequence(reversedModel, reversed);   // source version intact after deriving a mutation
        var expectedMutated = reversedModel.ToArray();
        expectedMutated[size / 2] = -1;
        AssertSequence(expectedMutated, mutated);
    }

    /// <summary>Verifies empty-deque degenerate behavior.</summary>
    [Fact]
    public void Empty_IsDegenerate()
    {
        var empty = ReversibleDeque<int>.Empty;
        Assert.True(empty.IsEmpty);
        Assert.Empty(empty.ToArray());
        Assert.Same(empty, empty.Reverse());
        Assert.Throws<InvalidOperationException>(() => empty.First);
        Assert.Throws<InvalidOperationException>(() => empty.RemoveFirst());
        Assert.Throws<ArgumentOutOfRangeException>(() => empty[0]);
        Assert.False(empty.TryRemoveFirst(out _, out var rest));
        Assert.Same(empty, rest);
    }
}
