// Tests for the finger tree deque invariant.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies internal structural invariants (digit lengths, cached counts, rightmost-leaf signposts) through
/// the <c>InternalsVisibleTo</c> seam after every kind of operation, as required by the API specification.
/// </summary>
public sealed class FingerTreeDequeInvariantTests
{
    /// <summary>
    /// Verifies every empty/single/deep digit transition for counts zero through twelve under left-end,
    /// right-end, and span-factory construction.
    /// </summary>
    [Fact]
    public void SmallCounts_AllConstructionPaths_PreserveInvariants()
    {
        for (var count = 0; count <= 12; count++)
        {
            var expected = Enumerable.Range(0, count).ToArray();

            var bySnoc = FingerTreeDeque<int>.Empty;
            foreach (var item in expected)
            {
                bySnoc = bySnoc.AddLast(item);
                bySnoc.ValidateInvariants();
            }

            var byCons = FingerTreeDeque<int>.Empty;
            for (var item = count - 1; item >= 0; item--)
            {
                byCons = byCons.AddFirst(item);
                byCons.ValidateInvariants();
            }

            var byCreate = FingerTreeDeque<int>.CreateRange(expected);
            byCreate.ValidateInvariants();

            FingerTreeDequeAssert.SequenceEqual(expected, bySnoc);
            FingerTreeDequeAssert.SequenceEqual(expected, byCons);
            FingerTreeDequeAssert.SequenceEqual(expected, byCreate);
        }
    }

    /// <summary>
    /// Verifies endpoint removal keeps the structure valid at every intermediate size, including the deep
    /// underflow paths that pull nodes from the middle tree.
    /// </summary>
    [Fact]
    public void EndpointRemoval_EveryIntermediateSize_PreservesInvariants()
    {
        const int size = 64;
        var full = FingerTreeDeque<int>.CreateRange(Enumerable.Range(0, size));

        var fromFront = full;
        for (var removed = 0; removed < size; removed++)
        {
            fromFront = fromFront.RemoveFirst();
            fromFront.ValidateInvariants();
            FingerTreeDequeAssert.SequenceEqual(Enumerable.Range(removed + 1, size - removed - 1).ToArray(), fromFront);
        }

        var fromBack = full;
        for (var removed = 0; removed < size; removed++)
        {
            fromBack = fromBack.RemoveLast();
            fromBack.ValidateInvariants();
        }

        var alternating = full;
        while (!alternating.IsEmpty)
        {
            alternating = alternating.Count % 2 == 0 ? alternating.RemoveFirst() : alternating.RemoveLast();
            alternating.ValidateInvariants();
        }

        Assert.Same(FingerTreeDeque<int>.Empty, alternating);
        full.ValidateInvariants();
    }

    /// <summary>
    /// Verifies the split reconstruction law and structural validity at every legal split index for all
    /// small sizes.
    /// </summary>
    [Fact]
    public void SplitAt_EveryIndexAndSmallSize_ReconstructsAndPreservesInvariants()
    {
        for (var size = 0; size <= 24; size++)
        {
            var expected = Enumerable.Range(0, size).ToArray();
            var deque = FingerTreeDeque<int>.CreateRange(expected);

            for (var index = 0; index <= size; index++)
            {
                var split = deque.SplitAt(index);
                split.Left.ValidateInvariants();
                split.Right.ValidateInvariants();
                Assert.Equal(index, split.Left.Count);
                Assert.Equal(size - index, split.Right.Count);

                var rejoined = split.Left.Concat(split.Right);
                rejoined.ValidateInvariants();
                FingerTreeDequeAssert.SequenceEqual(expected, rejoined);
            }
        }
    }

    /// <summary>
    /// Verifies indexed insertion and removal against a list model at every position for small sizes,
    /// validating the structure after each operation.
    /// </summary>
    [Fact]
    public void InsertAtAndRemoveAt_EveryIndexAndSmallSize_MatchListModel()
    {
        for (var size = 0; size <= 16; size++)
        {
            var expected = Enumerable.Range(0, size).ToList();
            var deque = FingerTreeDeque<int>.CreateRange(expected);

            for (var index = 0; index <= size; index++)
            {
                var inserted = deque.InsertAt(index, -1);
                inserted.ValidateInvariants();
                var model = new List<int>(expected);
                model.Insert(index, -1);
                FingerTreeDequeAssert.SequenceEqual(model, inserted);
            }

            for (var index = 0; index < size; index++)
            {
                var removed = deque.RemoveAt(index);
                removed.ValidateInvariants();
                var model = new List<int>(expected);
                model.RemoveAt(index);
                FingerTreeDequeAssert.SequenceEqual(model, removed);
            }
        }
    }

    /// <summary>
    /// Verifies a large generated sequence creates multiple nested middle-tree levels and stays valid under
    /// splitting and reconcatenation.
    /// </summary>
    [Fact]
    public void LargeSequence_ForcesNestedMiddleLevels_AndSurvivesSplitConcat()
    {
        const int size = 10_000;
        var expected = Enumerable.Range(0, size).ToArray();
        var deque = FingerTreeDeque<int>.CreateRange(expected);

        deque.ValidateInvariants();
        Assert.True(deque.TreeDepth >= 5, $"Expected at least five deep levels, found {deque.TreeDepth}.");

        for (var index = 0; index < size; index += 997)
            Assert.Equal(expected[index], deque[index]);

        foreach (var splitIndex in new[] { 1, 2, size / 3, size / 2, size - 2, size - 1 })
        {
            var split = deque.SplitAt(splitIndex);
            split.Left.ValidateInvariants();
            split.Right.ValidateInvariants();

            var rejoined = split.Left.Concat(split.Right);
            rejoined.ValidateInvariants();
            Assert.Equal(size, rejoined.Count);
            Assert.Equal(expected, rejoined.ToArray());
        }
    }

    /// <summary>
    /// Verifies concatenation is associative at the sequence level across a spread of operand sizes,
    /// including empty and single-element operands.
    /// </summary>
    [Fact]
    public void Concat_IsAssociativeAcrossSizes()
    {
        int[] sizes = [0, 1, 2, 3, 5, 8, 21, 64];
        foreach (var sizeA in sizes)
        {
            foreach (var sizeB in sizes)
            {
                foreach (var sizeC in sizes)
                {
                    var a = FingerTreeDeque<int>.CreateRange(Enumerable.Range(0, sizeA));
                    var b = FingerTreeDeque<int>.CreateRange(Enumerable.Range(100, sizeB));
                    var c = FingerTreeDeque<int>.CreateRange(Enumerable.Range(200, sizeC));

                    var leftFirst = a.Concat(b).Concat(c);
                    var rightFirst = a.Concat(b.Concat(c));

                    leftFirst.ValidateInvariants();
                    rightFirst.ValidateInvariants();

                    var expected = Enumerable.Range(0, sizeA)
                        .Concat(Enumerable.Range(100, sizeB))
                        .Concat(Enumerable.Range(200, sizeC))
                        .ToArray();
                    Assert.Equal(expected, leftFirst.ToArray());
                    Assert.Equal(expected, rightFirst.ToArray());
                }
            }
        }
    }

    /// <summary>
    /// Runs deterministic mixed operation histories covering the full indexed and range API surface,
    /// validating internal invariants after every step.
    /// </summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    public void MixedOperationHistories_PreserveInvariantsEveryStep(int seed)
    {
        var random = new Random(seed);
        var model = new List<int>();
        var deque = FingerTreeDeque<int>.Empty;

        for (var step = 0; step < 120; step++)
        {
            switch (random.Next(12))
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
                    deque = deque.UpdateAt(index, _ => step);
                    break;
                }
                case 7:
                {
                    var index = random.Next(model.Count + 1);
                    var length = random.Next(4);
                    var values = Enumerable.Range(1000 + step, length).ToArray();
                    model.InsertRange(index, values);
                    deque = deque.InsertRange(index, values);
                    break;
                }
                case 8 when model.Count > 0:
                {
                    var index = random.Next(model.Count);
                    var length = random.Next(model.Count - index + 1);
                    model.RemoveRange(index, length);
                    deque = deque.RemoveRange(index, length);
                    break;
                }
                case 9 when model.Count > 0:
                {
                    var index = random.Next(model.Count);
                    var length = random.Next(model.Count - index + 1);
                    var range = deque.GetRange(index, length);
                    range.ValidateInvariants();
                    Assert.Equal(model.GetRange(index, length), range.ToArray());
                    break;
                }
                case 10 when model.Count > 0:
                {
                    var index = random.Next(model.Count);
                    var length = random.Next(model.Count - index + 1);
                    var split = deque.SplitRange(index, length);
                    split.Before.ValidateInvariants();
                    split.Range.ValidateInvariants();
                    split.After.ValidateInvariants();
                    deque = split.Before.Concat(split.Range).Concat(split.After);
                    break;
                }
                default:
                {
                    var index = random.Next(model.Count + 1);
                    var split = deque.SplitAt(index);
                    deque = split.Left.Concat(split.Right);
                    break;
                }
            }

            deque.ValidateInvariants();
            FingerTreeDequeAssert.SequenceEqual(model, deque);
        }
    }
}
