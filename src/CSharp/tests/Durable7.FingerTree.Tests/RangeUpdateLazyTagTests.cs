// Tests for the range update lazy tag.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Nested lazy tags, structural pushes, ordered range queries, and persistent branching behavior.</summary>
public sealed class RangeUpdateLazyTagTests
{
    /// <summary>Exercises assign-after-add and add-after-assign at overlapping whole and nested ranges.</summary>
    [Fact]
    public void NestedAssignAndAddTags_RespectNewestAfterOlderComposition()
    {
        var model = Enumerable.Range(0, 48).ToList();
        var source = RangeUpdateAssert.Create(model.ToArray());

        var wholeAdd = source.ApplyRange(0, source.Count, RangeUpdateTag.Add(10));
        RangeUpdateAssert.Apply(model, 0, model.Count, RangeUpdateTag.Add(10));
        RangeUpdateAssert.Matches(model, wholeAdd);

        var middleAssign = wholeAdd.ApplyRange(5, 36, RangeUpdateTag.Assign(7));
        RangeUpdateAssert.Apply(model, 5, 36, RangeUpdateTag.Assign(7));
        RangeUpdateAssert.Matches(model, middleAssign);

        var innerAdd = middleAssign.ApplyRange(11, 19, RangeUpdateTag.Add(-3));
        RangeUpdateAssert.Apply(model, 11, 19, RangeUpdateTag.Add(-3));
        RangeUpdateAssert.Matches(model, innerAdd);

        var newestWholeAdd = innerAdd.ApplyRange(0, innerAdd.Count, RangeUpdateTag.Add(2));
        RangeUpdateAssert.Apply(model, 0, model.Count, RangeUpdateTag.Add(2));
        var statistics = RangeUpdateAssert.Matches(model, newestWholeAdd);
        RangeUpdateAssert.AllRangesMatch(model, newestWholeAdd);

        if (statistics.PendingTagNodeCount >= 0)
            Assert.True(statistics.PendingTagNodeCount > 0);

        RangeUpdateAssert.Matches(Enumerable.Range(0, 48).ToArray(), source);
    }

    /// <summary>Forces pending tags through insert, replacement, deletion, split, and concatenation paths.</summary>
    [Fact]
    public void StructuralEdits_ThroughLazilyTaggedTreesMatchListModel()
    {
        var model = Enumerable.Range(0, 65).ToList();
        var sequence = RangeUpdateAssert.Create(model.ToArray());

        sequence = sequence.ApplyRange(0, sequence.Count, RangeUpdateTag.Assign(100));
        RangeUpdateAssert.Apply(model, 0, model.Count, RangeUpdateTag.Assign(100));
        sequence = sequence.ApplyRange(9, 47, RangeUpdateTag.Add(5));
        RangeUpdateAssert.Apply(model, 9, 47, RangeUpdateTag.Add(5));
        sequence = sequence.ApplyRange(20, 21, RangeUpdateTag.Assign(-8));
        RangeUpdateAssert.Apply(model, 20, 21, RangeUpdateTag.Assign(-8));

        sequence = sequence.Insert(0, 700);
        model.Insert(0, 700);
        sequence = sequence.Insert(33, 701);
        model.Insert(33, 701);
        sequence = sequence.Insert(sequence.Count, 702);
        model.Add(702);
        sequence = sequence.SetItem(21, 900);
        model[21] = 900;
        sequence = sequence.RemoveAt(10);
        model.RemoveAt(10);
        sequence = sequence.RemoveAt(sequence.Count - 2);
        model.RemoveAt(model.Count - 2);
        RangeUpdateAssert.Matches(model, sequence);

        var splitIndex = 37;
        var split = sequence.SplitAt(splitIndex);
        RangeUpdateAssert.Matches(model.Take(splitIndex).ToArray(), split.Left);
        RangeUpdateAssert.Matches(model.Skip(splitIndex).ToArray(), split.Right);

        var rightModel = model.Skip(splitIndex).ToList();
        var changedRight = split.Right.ApplyRange(0, split.Right.Count, RangeUpdateTag.Add(11));
        RangeUpdateAssert.Apply(rightModel, 0, rightModel.Count, RangeUpdateTag.Add(11));
        var rejoined = split.Left.Concat(changedRight);
        var rejoinedModel = model.Take(splitIndex).Concat(rightModel).ToList();
        RangeUpdateAssert.Matches(rejoinedModel, rejoined);
        RangeUpdateAssert.AllRangesMatch(rejoinedModel, rejoined);

        RangeUpdateAssert.Matches(model, sequence);
        RangeUpdateAssert.Matches(model.Skip(splitIndex).ToArray(), split.Right);
    }

    /// <summary>Checks whole, empty, prefix, suffix, singleton, and arbitrary measures without structural edits.</summary>
    [Fact]
    public void MeasureRange_SeesLogicalValuesUnderPendingTags()
    {
        var model = Enumerable.Range(-12, 32).ToList();
        var sequence = RangeUpdateAssert.Create(model.ToArray());
        sequence = sequence.ApplyRange(0, model.Count, RangeUpdateTag.Add(4));
        RangeUpdateAssert.Apply(model, 0, model.Count, RangeUpdateTag.Add(4));
        sequence = sequence.ApplyRange(4, 24, RangeUpdateTag.Assign(3));
        RangeUpdateAssert.Apply(model, 4, 24, RangeUpdateTag.Assign(3));
        sequence = sequence.ApplyRange(8, 12, RangeUpdateTag.Add(9));
        RangeUpdateAssert.Apply(model, 8, 12, RangeUpdateTag.Add(9));

        Assert.Equal(RangeUpdateAffineAlgebra.Empty, sequence.MeasureRange(0, 0));
        Assert.Equal(RangeUpdateAffineAlgebra.Empty, sequence.MeasureRange(sequence.Count, 0));
        Assert.Equal(RangeUpdateAssert.Fold(model), sequence.MeasureRange(0, sequence.Count));
        Assert.Equal(sequence.Measure, sequence.MeasureRange(0, sequence.Count));

        for (var count = 0; count <= sequence.Count; count++)
        {
            Assert.Equal(RangeUpdateAssert.Fold(model.Take(count)), sequence.MeasureRange(0, count));
            Assert.Equal(
                RangeUpdateAssert.Fold(model.Skip(model.Count - count)),
                sequence.MeasureRange(model.Count - count, count));
        }

        for (var index = 0; index < sequence.Count; index++)
            Assert.Equal(RangeUpdateAffineAlgebra.Measure(model[index]), sequence.MeasureRange(index, 1));

        RangeUpdateAssert.AllRangesMatch(model, sequence);
        RangeUpdateAssert.Matches(model, sequence);
    }

    /// <summary>Retains and branches from tagged versions, proving later pushes never mutate older roots.</summary>
    [Fact]
    public void TaggedVersions_RemainStableAcrossIndependentBranches()
    {
        var originalModel = Enumerable.Range(1, 40).ToArray();
        var original = RangeUpdateAssert.Create(originalModel);
        var taggedModel = originalModel.ToList();
        var tagged = original.ApplyRange(0, original.Count, RangeUpdateTag.Add(5));
        RangeUpdateAssert.Apply(taggedModel, 0, taggedModel.Count, RangeUpdateTag.Add(5));

        var assignModel = taggedModel.ToList();
        var assignBranch = tagged.ApplyRange(7, 20, RangeUpdateTag.Assign(-1));
        RangeUpdateAssert.Apply(assignModel, 7, 20, RangeUpdateTag.Assign(-1));
        assignBranch = assignBranch.Insert(12, 500).RemoveAt(3);
        assignModel.Insert(12, 500);
        assignModel.RemoveAt(3);

        var addModel = taggedModel.ToList();
        var addBranch = tagged.ApplyRange(3, 30, RangeUpdateTag.Add(100));
        RangeUpdateAssert.Apply(addModel, 3, 30, RangeUpdateTag.Add(100));
        var split = addBranch.SplitAt(18);
        addBranch = split.Left.Concat(split.Right.SetItem(0, 999));
        addModel[18] = 999;

        RangeUpdateAssert.Matches(originalModel, original);
        RangeUpdateAssert.Matches(taggedModel, tagged);
        RangeUpdateAssert.Matches(assignModel, assignBranch);
        RangeUpdateAssert.Matches(addModel, addBranch);

        Assert.Equal(originalModel, original.ToArray());
        Assert.Equal(taggedModel, tagged.ToArray());
    }
}
