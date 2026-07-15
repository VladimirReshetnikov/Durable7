using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Direct AVL, count, logical-measure-cache, pending-tag, and height-ceiling invariant checks.</summary>
public sealed class RangeUpdateInvariantTests
{
    /// <summary>Checks diagnostic count, node count, AVL balance, and logarithmic height for many factory sizes.</summary>
    [Fact]
    public void FactoryShapes_HaveExactCountsAndAvlHeightCeiling()
    {
        for (var size = 0; size <= 512; size++)
        {
            var sequence = RangeUpdateAssert.Create(Enumerable.Range(0, size).ToArray());
            var statistics = RangeUpdateAssert.Matches(Enumerable.Range(0, size).ToArray(), sequence);
            AssertDetailedStatistics(sequence, statistics);
        }
    }

    /// <summary>Checks adversarial endpoint and middle edits retain AVL and cache invariants after every command.</summary>
    [Fact]
    public void AdversarialEditHistory_PreservesAvlAndCacheInvariants()
    {
        var sequence = RangeUpdateAssert.Create();
        var model = new List<int>();

        for (var value = 0; value < 300; value++)
        {
            if ((value & 1) == 0)
            {
                sequence = sequence.Prepend(value);
                model.Insert(0, value);
            }
            else
            {
                sequence = sequence.Append(value);
                model.Add(value);
            }

            AssertDetailedStatistics(sequence, RangeUpdateAssert.Matches(model, sequence));
        }

        for (var step = 0; step < 220; step++)
        {
            var index = (step * 37) % model.Count;
            if (step % 3 == 0)
            {
                sequence = sequence.SetItem(index, -step);
                model[index] = -step;
            }
            else
            {
                sequence = sequence.RemoveAt(index);
                model.RemoveAt(index);
            }

            AssertDetailedStatistics(sequence, RangeUpdateAssert.Matches(model, sequence));
        }
    }

    /// <summary>Checks cached logical measures while pending tags occur at several structural depths.</summary>
    [Fact]
    public void NestedPendingTags_HaveValidLogicalCachesAndDiagnostics()
    {
        var model = Enumerable.Range(0, 257).ToList();
        var sequence = RangeUpdateAssert.Create(model.ToArray());

        for (var round = 0; round < 24; round++)
        {
            var index = round % 2 == 0 ? round : 257 - (round * 3);
            index = Math.Clamp(index, 0, model.Count);
            var count = model.Count - index;
            if (round % 3 == 0)
                count /= 2;

            var tag = round % 4 == 0
                ? RangeUpdateTag.Assign(round - 10)
                : RangeUpdateTag.Add((round % 7) - 3);
            sequence = sequence.ApplyRange(index, count, tag);
            RangeUpdateAssert.Apply(model, index, count, tag);

            if (round % 5 == 0)
            {
                var insertionIndex = (round * 11) % (model.Count + 1);
                sequence = sequence.Insert(insertionIndex, 1000 + round);
                model.Insert(insertionIndex, 1000 + round);
            }

            var statistics = RangeUpdateAssert.Matches(model, sequence);
            AssertDetailedStatistics(sequence, statistics);
            Assert.Equal(RangeUpdateAssert.Fold(model), sequence.Measure);
        }

        var finalStatistics = RangeUpdateDiagnosticsAdapter.Validate(
            sequence,
            static (left, right) => left == right);
        Assert.True(finalStatistics.PendingTagNodeCount > 0);
        Assert.True(finalStatistics.MaximumPendingTagDepth >= 1);
        RangeUpdateAssert.AllRangesMatch(model, sequence);
    }

    /// <summary>Checks split and join products independently before and after additional lazy updates.</summary>
    [Fact]
    public void EverySplitProductAndRejoin_HasIndependentValidDiagnostics()
    {
        var model = Enumerable.Range(-64, 129).ToList();
        var source = RangeUpdateAssert.Create(model.ToArray())
            .ApplyRange(0, model.Count, RangeUpdateTag.Add(7))
            .ApplyRange(17, 91, RangeUpdateTag.Assign(4))
            .ApplyRange(33, 37, RangeUpdateTag.Add(-9));
        RangeUpdateAssert.Apply(model, 0, model.Count, RangeUpdateTag.Add(7));
        RangeUpdateAssert.Apply(model, 17, 91, RangeUpdateTag.Assign(4));
        RangeUpdateAssert.Apply(model, 33, 37, RangeUpdateTag.Add(-9));

        for (var index = 0; index <= source.Count; index++)
        {
            var split = source.SplitAt(index);
            var leftStatistics = RangeUpdateAssert.Matches(model.Take(index).ToArray(), split.Left);
            var rightStatistics = RangeUpdateAssert.Matches(model.Skip(index).ToArray(), split.Right);
            AssertDetailedStatistics(split.Left, leftStatistics);
            AssertDetailedStatistics(split.Right, rightStatistics);

            var rejoined = split.Left.Concat(split.Right);
            var joinedStatistics = RangeUpdateAssert.Matches(model, rejoined);
            AssertDetailedStatistics(rejoined, joinedStatistics);
        }

        RangeUpdateAssert.Matches(model, source);
    }

    private static void AssertDetailedStatistics(
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra> sequence,
        RangeUpdateStructureSnapshot statistics)
    {
        Assert.True(statistics.HasDetailedStatistics);
        Assert.Equal(sequence.Count, statistics.Count);
        Assert.Equal(sequence.Count, statistics.NodeCount);
        Assert.InRange(statistics.MaximumAbsoluteBalanceFactor, 0, 1);
        Assert.True(statistics.PendingTagNodeCount >= 0);
        Assert.True(statistics.MaximumPendingTagDepth >= 0);

        if (sequence.Count == 0)
        {
            Assert.Equal(0, statistics.Height);
            return;
        }

        Assert.InRange(statistics.Height, 1, HeightCeiling(sequence.Count));
    }

    private static int HeightCeiling(int count) =>
        (2 * (int)Math.Ceiling(Math.Log2(count + 1))) + 1;
}
