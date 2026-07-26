// Tests for the sequence cursor.

using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Contract coverage for measured and positional persistent sequence cursors.</summary>
public sealed class SequenceCursorTests
{
    private readonly struct CountAbove(int boundary) : IMeasurePredicate<int>
    {
        public bool Invoke(int measure) => measure > boundary;
    }

    /// <summary>Verifies measured split navigation, edits, branching, and usable miss cursors.</summary>
    [Fact]
    public void GeneralMeasuredCursor_PreservesMeasuresNeighborsSnapshotsAndBranches()
    {
        var tree = FingerTree<int, int, SizeMeasure<int>>.Create(10, 20, 30, 40);
        var start = tree.GetCursorAtStart();

        Assert.True(start.IsAtStart);
        Assert.False(start.IsAtEnd);
        Assert.Equal(0, start.MeasureBefore);
        Assert.Equal(4, start.MeasureAfter);
        Assert.False(start.TryPeekPrevious(out _));
        Assert.True(start.TryPeekNext(out var first));
        Assert.Equal(10, first);
        Assert.Same(tree, start.Snapshot());
        Assert.Throws<InvalidOperationException>(() => start.MovePrevious());

        Assert.True(tree.TryGetCursor(new CountAbove(2), out var middle));
        Assert.Equal(2, middle.MeasureBefore);
        Assert.Equal(2, middle.MeasureAfter);
        Assert.True(middle.TryPeekPrevious(out var previous));
        Assert.True(middle.TryPeekNext(out var next));
        Assert.Equal(20, previous);
        Assert.Equal(30, next);
        Assert.Same(tree, middle.Snapshot());

        var moved = middle.MovePrevious().MoveNext();
        Assert.Same(tree, moved.Snapshot());
        Assert.Equal(2, moved.MeasureBefore);
        var inserted = middle.Insert(25);
        Assert.Equal([10, 20, 25, 30, 40], inserted.Snapshot().ToArray());
        Assert.Equal(3, inserted.MeasureBefore);
        Assert.Equal([10, 20, 30, 40], middle.Snapshot().ToArray());
        Assert.Equal([10, 20, 99, 40], middle.ReplaceNext(99).Snapshot().ToArray());
        Assert.Equal([10, 30, 40], middle.DeletePrevious().Snapshot().ToArray());
        Assert.Equal([10, 20, 40], middle.DeleteNext().Snapshot().ToArray());

        Assert.False(tree.TryGetCursor(new CountAbove(10), out var miss));
        Assert.True(miss.IsAtEnd);
        Assert.Same(tree, miss.Snapshot());
        Assert.Throws<InvalidOperationException>(() => default(FingerTreeCursor<int, int, SizeMeasure<int>>).Snapshot());
    }

    /// <summary>Verifies deque gap semantics, nullable peeks, range capture, and branching edits.</summary>
    [Fact]
    public void DequeCursor_UsesGapSemanticsAndCapturesRangesOnce()
    {
        var deque = FingerTreeDeque<string?>.Create("a", null, "c");
        var cursor = deque.GetCursor(1);
        Assert.Equal(1, cursor.Position);
        Assert.True(cursor.TryPeekPrevious(out var previous));
        Assert.Equal("a", previous);
        Assert.True(cursor.TryPeekNext(out var next));
        Assert.Null(next);
        Assert.Same(deque, cursor.Snapshot());

        var source = new SingleUseEnumerable<string?>(["x", "y"]);
        var inserted = cursor.InsertRange(source);
        Assert.Equal(1, source.EnumerationCount);
        Assert.Equal(3, inserted.Position);
        Assert.Equal(["a", "x", "y", null, "c"], inserted.Snapshot());
        Assert.Equal(["a", null, "c"], cursor.Snapshot());
        Assert.Equal(["a", "z", "c"], cursor.ReplaceNext("z").Snapshot());
        Assert.Equal([null, "c"], cursor.DeletePrevious().Snapshot());
        Assert.Equal(["a", "c"], cursor.DeleteNext().Snapshot());

        var end = deque.GetCursor(deque.Count);
        Assert.True(end.IsAtEnd);
        Assert.False(end.TryPeekNext(out _));
        Assert.Throws<InvalidOperationException>(() => end.DeleteNext());
        Assert.Throws<InvalidOperationException>(() => default(FingerTreeDequeCursor<int>).Snapshot());
    }

    /// <summary>Verifies logical gap reversal at every boundary and non-palindromic range insertion.</summary>
    [Fact]
    public void ReversibleCursor_MapsEveryGapAcrossReverseAndRangeInsertion()
    {
        var deque = ReversibleDeque<int>.Create(1, 2, 3, 4);
        for (var position = 0; position <= deque.Count; position++)
        {
            var cursor = deque.GetCursor(position);
            var reversed = cursor.Reverse();
            Assert.Equal(deque.Count - position, reversed.Position);
            Assert.Equal([4, 3, 2, 1], reversed.Snapshot());
            var restored = reversed.Reverse();
            Assert.Equal(position, restored.Position);
            Assert.Equal(deque.ToArray(), restored.Snapshot().ToArray());
        }

        var edited = deque.GetCursor(2).InsertRange(new SingleUseEnumerable<int>([8, 9]));
        Assert.Equal(4, edited.Position);
        Assert.Equal([1, 2, 8, 9, 3, 4], edited.Snapshot());
        Assert.Equal([4, 3, 9, 8, 2, 1], edited.Reverse().Snapshot());
        Assert.Equal([1, 2, 3, 4], deque);
    }

    /// <summary>Verifies vector cursors across radix leaves and structurally shared vector splices.</summary>
    [Fact]
    public void RrbCursor_BranchesAcrossLeafBoundariesAndRetainsVectorSplices()
    {
        var vector = RrbVector<int>.CreateRange(Enumerable.Range(0, 70));
        var cursor = vector.GetCursor(32);
        Assert.True(cursor.TryPeekPrevious(out var previous));
        Assert.True(cursor.TryPeekNext(out var next));
        Assert.Equal(31, previous);
        Assert.Equal(32, next);
        Assert.Same(vector, cursor.Snapshot());

        var inserted = cursor.InsertRange(RrbVector<int>.CreateRange([100, 101]));
        Assert.Equal(34, inserted.Position);
        Assert.Equal(100, inserted.Snapshot()[32]);
        Assert.Equal(101, inserted.Snapshot()[33]);
        Assert.Equal(32, inserted.Snapshot()[34]);
        Assert.Equal(32, cursor.Snapshot()[32]);
        Assert.Equal(999, cursor.ReplaceNext(999).Snapshot()[32]);
        Assert.Equal(32, cursor.DeletePrevious().Snapshot()[31]);
        Assert.Equal(33, cursor.DeleteNext().Snapshot()[32]);
        Assert.True(inserted.Snapshot().ValidateStructure(out _));
    }

    /// <summary>Verifies logical lazy-tag peeks, directional measures, and directional updates.</summary>
    [Fact]
    public void RangeCursor_ObservesLazyTagsAndAppliesDirectionalMeasuresAndUpdates()
    {
        var source = RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
            .Create([1, 2, 3, 4])
            .ApplyRange(0, 4, RangeUpdateTag.Add(10));
        var cursor = source.GetCursor(2);

        Assert.True(cursor.TryPeekPrevious(out var previous));
        Assert.True(cursor.TryPeekNext(out var next));
        Assert.Equal(12, previous);
        Assert.Equal(13, next);
        Assert.Equal(new RangeUpdateMeasure(2, 23, 12), cursor.MeasureBefore);
        Assert.Equal(new RangeUpdateMeasure(2, 27, 14), cursor.MeasureAfter);
        Assert.Equal(cursor.MeasureBefore, cursor.MeasurePrevious(2));
        Assert.Equal(cursor.MeasureAfter, cursor.MeasureNext(2));
        Assert.Equal(source.Measure,
            RangeUpdateAffineAlgebra.Combine(cursor.MeasureBefore, cursor.MeasureAfter));
        Assert.Same(source, cursor.Snapshot());

        var previousChanged = cursor.ApplyPrevious(1, RangeUpdateTag.Add(100));
        Assert.Equal([11, 112, 13, 14], previousChanged.Snapshot());
        Assert.Equal(2, previousChanged.Position);
        var nextChanged = cursor.ApplyNext(2, RangeUpdateTag.Assign(7));
        Assert.Equal([11, 12, 7, 7], nextChanged.Snapshot());
        Assert.Equal([11, 12, 13, 14], cursor.Snapshot());
        Assert.Same(source, cursor.ApplyNext(0, RangeUpdateTag.Add(1)).Snapshot());
        Assert.Throws<ArgumentOutOfRangeException>(() => cursor.ApplyPrevious(3, RangeUpdateTag.Add(1)));
        Assert.Throws<InvalidOperationException>(() => default(
            RangeUpdateSequenceCursor<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>).Snapshot());
    }
}
