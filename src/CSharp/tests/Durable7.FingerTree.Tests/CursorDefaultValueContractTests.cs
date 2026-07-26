// Tests for the cursor default value contract.

using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Locks the repository-wide rule that every member of an invalid default cursor value throws the
/// same documented exception. <c>Position</c> and the members derived from it are covered explicitly
/// because an auto-property backing field would otherwise report gap zero for an uninitialized
/// struct while every other member throws.
/// </summary>
public sealed class CursorDefaultValueContractTests
{
    /// <summary>Verifies the positional sequence checkpoint cursors reject their default value.</summary>
    [Fact]
    public void DefaultSequenceCursors_RejectPositionAndDerivedMembers()
    {
        AssertPositionRejected(
            () => _ = default(FingerTreeDequeCursor<int>).Position,
            () => _ = default(FingerTreeDequeCursor<int>).IsAtStart,
            () => _ = default(FingerTreeDequeCursor<int>).Seek(0));

        AssertPositionRejected(
            () => _ = default(ReversibleDequeCursor<int>).Position,
            () => _ = default(ReversibleDequeCursor<int>).IsAtStart,
            () => _ = default(ReversibleDequeCursor<int>).Seek(0));

        AssertPositionRejected(
            () => _ = default(RrbVectorCursor<int>).Position,
            () => _ = default(RrbVectorCursor<int>).IsAtStart,
            () => _ = default(RrbVectorCursor<int>).Seek(0));

        AssertPositionRejected(
            () => _ = default(RangeUpdateSequenceCursor<
                int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>).Position,
            () => _ = default(RangeUpdateSequenceCursor<
                int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>).IsAtStart,
            () => _ = default(RangeUpdateSequenceCursor<
                int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>).Seek(0));
    }

    /// <summary>Verifies the ordered and augmented-search checkpoint cursors reject their default value.</summary>
    [Fact]
    public void DefaultOrderedSearchCursors_RejectPositionAndDerivedMembers()
    {
        AssertPositionRejected(
            () => _ = default(SortedBagCursor<int>).Position,
            () => _ = default(SortedBagCursor<int>).IsAtStart,
            () => _ = default(SortedBagCursor<int>).SeekRank(0));

        AssertPositionRejected(
            () => _ = default(SortedSetCursor<int>).Position,
            () => _ = default(SortedSetCursor<int>).IsAtStart,
            () => _ = default(SortedSetCursor<int>).SeekRank(0));

        AssertPositionRejected(
            () => _ = default(SortedDictionaryCursor<int, string>).Position,
            () => _ = default(SortedDictionaryCursor<int, string>).IsAtStart,
            () => _ = default(SortedDictionaryCursor<int, string>).SeekRank(0));

        AssertPositionRejected(
            () => _ = default(CanonicalSortedSetCursor<int>).Position,
            () => _ = default(CanonicalSortedSetCursor<int>).IsAtStart,
            () => _ = default(CanonicalSortedSetCursor<int>).SeekRank(0));

        AssertPositionRejected(
            () => _ = default(PrioritySearchQueueCursor<int, int, string>).Position,
            () => _ = default(PrioritySearchQueueCursor<int, int, string>).IsAtStart,
            () => _ = default(PrioritySearchQueueCursor<int, int, string>).SeekRank(0));

        AssertPositionRejected(
            () => _ = default(IntervalTreeCursor<int>).Position,
            () => _ = default(IntervalTreeCursor<int>).IsAtStart,
            () => _ = default(IntervalTreeCursor<int>).SeekRank(0));

        AssertPositionRejected(
            () => _ = default(PersistentIntervalMapCursor<int, string>).Position,
            () => _ = default(PersistentIntervalMapCursor<int, string>).IsAtStart,
            () => _ = default(PersistentIntervalMapCursor<int, string>).SeekRank(0));

        AssertPositionRejected(
            () => _ = default(PersistentChunkedBitSetCursor).Position,
            () => _ = default(PersistentChunkedBitSetCursor).IsAtStart,
            () => _ = default(PersistentChunkedBitSetCursor).SeekRank(0));
    }

    private static void AssertPositionRejected(params Action[] members)
    {
        foreach (var member in members)
            Assert.Throws<InvalidOperationException>(member);
    }
}
