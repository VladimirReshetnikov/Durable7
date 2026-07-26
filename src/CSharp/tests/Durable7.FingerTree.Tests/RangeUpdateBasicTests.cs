// Tests for the range update basic.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Factories, positional edits, boundaries, null support, validation, and public identity behavior.</summary>
public sealed class RangeUpdateBasicTests
{
    /// <summary>Verifies both factories, empty state, cached measure, indexed reads, and IReadOnlyList shape.</summary>
    [Fact]
    public void Factories_ProduceEquivalentMeasuredSequences()
    {
        var items = Enumerable.Range(-20, 41).ToArray();
        var fromSpan = RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
            .Create(items.AsSpan());
        var fromEnumerable = RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
            .CreateRange(items.Where(static _ => true));

        RangeUpdateAssert.Matches(items, fromSpan);
        RangeUpdateAssert.Matches(items, fromEnumerable);
        Assert.Equal(fromSpan.Measure, fromEnumerable.Measure);
        Assert.Same(fromSpan, RangeUpdateSequence<
            int,
            RangeUpdateMeasure,
            RangeUpdateTag,
            RangeUpdateAffineAlgebra>.CreateRange(fromSpan));

        IReadOnlyList<int> readOnly = fromSpan;
        Assert.Equal(items.Length, readOnly.Count);
        Assert.Equal(items, readOnly.ToArray());

        var empty = RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>.Empty;
        Assert.True(empty.IsEmpty);
        Assert.Empty(empty);
        Assert.Equal(RangeUpdateAffineAlgebra.Empty, empty.Measure);
        Assert.Same(empty, RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
            .Create(Array.Empty<int>().AsSpan()));
        Assert.Same(empty, RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
            .CreateRange(Array.Empty<int>()));
    }

    /// <summary>Verifies every positional edit returns the expected new version and retains its source.</summary>
    [Fact]
    public void PositionalEdits_ReturnExpectedPersistentVersions()
    {
        var source = RangeUpdateAssert.Create(10, 20, 30, 40);
        var prepended = source.Prepend(5);
        var appended = source.Append(50);
        var inserted = source.Insert(2, 25);
        var replaced = source.SetItem(1, 200);
        var removed = source.RemoveAt(2);

        RangeUpdateAssert.Matches([10, 20, 30, 40], source);
        RangeUpdateAssert.Matches([5, 10, 20, 30, 40], prepended);
        RangeUpdateAssert.Matches([10, 20, 30, 40, 50], appended);
        RangeUpdateAssert.Matches([10, 20, 25, 30, 40], inserted);
        RangeUpdateAssert.Matches([10, 200, 30, 40], replaced);
        RangeUpdateAssert.Matches([10, 20, 40], removed);

        var chained = source.Prepend(0).Append(60).Insert(3, 25).SetItem(1, 10).RemoveAt(4);
        RangeUpdateAssert.Matches([0, 10, 20, 25, 40, 60], chained);
        RangeUpdateAssert.Matches([10, 20, 30, 40], source);
    }

    /// <summary>Exhaustively checks split, concatenation, and slicing at all boundaries of small sequences.</summary>
    [Fact]
    public void SplitConcatAndRange_AllSmallBoundariesMatchArrayModel()
    {
        for (var size = 0; size <= 24; size++)
        {
            var expected = Enumerable.Range(0, size).Select(value => value - 12).ToArray();
            var sequence = RangeUpdateAssert.Create(expected);

            for (var splitIndex = 0; splitIndex <= size; splitIndex++)
            {
                var split = sequence.SplitAt(splitIndex);
                RangeUpdateAssert.Matches(expected.Take(splitIndex).ToArray(), split.Left);
                RangeUpdateAssert.Matches(expected.Skip(splitIndex).ToArray(), split.Right);
                RangeUpdateAssert.Matches(expected, split.Left.Concat(split.Right));
            }

            for (var index = 0; index <= size; index++)
            {
                for (var count = 0; count <= size - index; count++)
                {
                    var range = sequence.GetRange(index, count);
                    RangeUpdateAssert.Matches(expected.Skip(index).Take(count).ToArray(), range);
                    Assert.Equal(
                        RangeUpdateAssert.Fold(expected.Skip(index).Take(count)),
                        sequence.MeasureRange(index, count));
                }
            }
        }
    }

    /// <summary>Checks insertion and removal at every legal position for every small size.</summary>
    [Fact]
    public void InsertSetAndRemove_AllSmallPositionsMatchListModel()
    {
        for (var size = 0; size <= 20; size++)
        {
            var expected = Enumerable.Range(0, size).ToArray();
            var sequence = RangeUpdateAssert.Create(expected);

            for (var index = 0; index <= size; index++)
            {
                var insertedModel = expected.ToList();
                insertedModel.Insert(index, -100);
                RangeUpdateAssert.Matches(insertedModel, sequence.Insert(index, -100));
            }

            for (var index = 0; index < size; index++)
            {
                var setModel = expected.ToArray();
                setModel[index] = 1000 + index;
                RangeUpdateAssert.Matches(setModel, sequence.SetItem(index, 1000 + index));

                var removedModel = expected.ToList();
                removedModel.RemoveAt(index);
                RangeUpdateAssert.Matches(removedModel, sequence.RemoveAt(index));
            }
        }
    }

    /// <summary>Verifies empty and identity range updates preserve exact facade identity.</summary>
    [Fact]
    public void ApplyRange_NoOpsReturnSourceInstance()
    {
        var sequence = RangeUpdateAssert.Create(1, 2, 3, 4);
        var root = RangeUpdateDiagnosticsAdapter.RootIdentity(sequence);

        Assert.Same(sequence, sequence.ApplyRange(2, 0, RangeUpdateTag.Assign(99)));
        Assert.Same(sequence, sequence.ApplyRange(0, sequence.Count, RangeUpdateTag.Identity));
        Assert.Same(sequence, sequence.ApplyRange(1, 2, RangeUpdateTag.DistinctIdentity(47)));
        Assert.Same(sequence, sequence.ApplyRange(sequence.Count, 0, RangeUpdateTag.DistinctIdentity(99)));
        Assert.Same(sequence, sequence.Concat(
            RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>.Empty));
        Assert.Same(sequence, RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
            .Empty.Concat(sequence));
        Assert.Same(sequence, sequence.GetRange(0, sequence.Count));
        Assert.Same(
            RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>.Empty,
            sequence.GetRange(sequence.Count, 0));
        var splitAtStart = sequence.SplitAt(0);
        Assert.Same(sequence, splitAtStart.Right);
        var splitAtEnd = sequence.SplitAt(sequence.Count);
        Assert.Same(sequence, splitAtEnd.Left);
        Assert.Same(
            RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>.Empty,
            RangeUpdateAssert.Create(42).RemoveAt(0));

        if (root is not null)
            Assert.Same(root, RangeUpdateDiagnosticsAdapter.RootIdentity(sequence));
        RangeUpdateAssert.Matches([1, 2, 3, 4], sequence);
    }

    /// <summary>Verifies null values survive factories, positional edits, replacement tags, and measures.</summary>
    [Fact]
    public void NullableElements_AreOrdinaryValuesAndTagResults()
    {
        string?[] expected = [null, "a", null, "bc"];
        var source = RangeUpdateSequence<
            string?,
            RangeUpdateNullableMeasure,
            RangeUpdateNullableTag,
            RangeUpdateNullableAlgebra>.Create(expected.AsSpan());

        Assert.Equal(expected, source.ToArray());
        Assert.Equal(
            expected.Select(RangeUpdateNullableAlgebra.Measure)
                .Aggregate(RangeUpdateNullableAlgebra.Empty, RangeUpdateNullableAlgebra.Combine),
            source.Measure);

        var assigned = source.ApplyRange(1, 2, RangeUpdateNullableTag.Assign("x"));
        Assert.Equal(new string?[] { null, "x", "x", "bc" }, assigned.ToArray());
        var nulled = assigned.ApplyRange(0, assigned.Count, RangeUpdateNullableTag.Assign(null));
        Assert.Equal(new string?[] { null, null, null, null }, nulled.ToArray());
        Assert.Equal(new RangeUpdateNullableMeasure(4, 0, "<null>;<null>;<null>;<null>;"), nulled.Measure);

        RangeUpdateDiagnosticsAdapter.Validate(source, static (left, right) => left == right);
        RangeUpdateDiagnosticsAdapter.Validate(assigned, static (left, right) => left == right);
        RangeUpdateDiagnosticsAdapter.Validate(nulled, static (left, right) => left == right);
    }

    /// <summary>Verifies every invalid index or range is rejected, including overflowing index-plus-count.</summary>
    [Fact]
    public void InvalidIndicesAndRanges_AreRejectedEagerly()
    {
        var sequence = RangeUpdateAssert.Create(1, 2, 3);

        Assert.Throws<ArgumentOutOfRangeException>(() => _ = sequence[-1]);
        Assert.Throws<ArgumentOutOfRangeException>(() => _ = sequence[sequence.Count]);
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.Insert(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.Insert(sequence.Count + 1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.SetItem(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.SetItem(sequence.Count, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.RemoveAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.RemoveAt(sequence.Count));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.SplitAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.SplitAt(sequence.Count + 1));

        AssertInvalidRange(sequence, -1, 0);
        AssertInvalidRange(sequence, 0, -1);
        AssertInvalidRange(sequence, sequence.Count + 1, 0);
        AssertInvalidRange(sequence, 2, 2);
        AssertInvalidRange(sequence, 2, int.MaxValue);
        AssertInvalidRange(sequence, int.MaxValue, int.MaxValue);

        AssertInvalidRangeParameter(sequence, -1, -1, "index");
        AssertInvalidRangeParameter(sequence, sequence.Count + 1, -1, "count");
        AssertInvalidRangeParameter(sequence, sequence.Count + 1, 0, "index");
        AssertInvalidRangeParameter(sequence, 1, int.MaxValue, "count");

        Assert.Throws<ArgumentNullException>(() => sequence.Concat(null!));
        Assert.Throws<ArgumentNullException>(() =>
            RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
                .CreateRange(null!));
        RangeUpdateAssert.Matches([1, 2, 3], sequence);
    }

    private static void AssertInvalidRange(
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra> sequence,
        int index,
        int count)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.GetRange(index, count));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            sequence.ApplyRange(index, count, RangeUpdateTag.Add(1)));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.MeasureRange(index, count));
    }

    private static void AssertInvalidRangeParameter(
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra> sequence,
        int index,
        int count,
        string expectedParameterName)
    {
        Assert.Equal(
            expectedParameterName,
            Assert.Throws<ArgumentOutOfRangeException>(() => sequence.GetRange(index, count)).ParamName);
        Assert.Equal(
            expectedParameterName,
            Assert.Throws<ArgumentOutOfRangeException>(() =>
                sequence.ApplyRange(index, count, RangeUpdateTag.Add(1))).ParamName);
        Assert.Equal(
            expectedParameterName,
            Assert.Throws<ArgumentOutOfRangeException>(() => sequence.MeasureRange(index, count)).ParamName);
    }
}
