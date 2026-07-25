using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies sorted-search helpers for lower bounds, upper bounds, equal ranges, insertion, and deletion.
/// </summary>
public sealed class FingerTreeDequeSortedSearchTests
{
    /// <summary>
    /// Verifies lower-bound and upper-bound results around duplicate elements and missing keys.
    /// </summary>
    [Fact]
    public void SortedLowerBoundAndUpperBound_HandleDuplicatesAndMissingKeys()
    {
        var deque = FingerTreeDeque<int>.Create(1, 3, 3, 3, 7, 9);

        Assert.Equal(1, deque.SortedLowerBound(3));
        Assert.Equal(4, deque.SortedUpperBound(3));
        Assert.Equal(4, deque.SortedLowerBound(4));
        Assert.Equal(4, deque.SortedUpperBound(4));
        Assert.Equal(0, deque.SortedLowerBound(0));
        Assert.Equal(6, deque.SortedLowerBound(10));
    }

    /// <summary>
    /// Verifies binary search returns the first equal item and complement insertion points.
    /// </summary>
    [Fact]
    public void SortedBinarySearch_ReturnsFirstEqualOrComplementInsertionIndex()
    {
        var deque = FingerTreeDeque<int>.Create(1, 3, 3, 7, 9);

        Assert.Equal(1, deque.SortedBinarySearch(3));
        Assert.Equal(~3, deque.SortedBinarySearch(4));
        Assert.Equal(~0, deque.SortedBinarySearch(0));
        Assert.Equal(~5, deque.SortedBinarySearch(10));
        Assert.True(deque.SortedContains(7));
        Assert.False(deque.SortedContains(8));
    }

    /// <summary>
    /// Verifies sorted lower-bound and upper-bound split helpers return correctly partitioned deques.
    /// </summary>
    [Fact]
    public void SortedBoundSplits_ReturnExpectedPartitions()
    {
        var deque = FingerTreeDeque<int>.Create(1, 3, 3, 7, 9);

        var lower = deque.SplitAtSortedLowerBound(3);
        var upper = deque.SplitAtSortedUpperBound(3);

        FingerTreeDequeAssert.SequenceEqual([1], lower.Left);
        FingerTreeDequeAssert.SequenceEqual([3, 3, 7, 9], lower.Right);
        FingerTreeDequeAssert.SequenceEqual([1, 3, 3], upper.Left);
        FingerTreeDequeAssert.SequenceEqual([7, 9], upper.Right);
    }

    /// <summary>
    /// Verifies equal-range splitting isolates exactly the elements equal to the searched item.
    /// </summary>
    [Fact]
    public void SplitAtSortedEqualRange_ReturnsLessEqualAndGreaterPartitions()
    {
        var deque = FingerTreeDeque<int>.Create(1, 3, 3, 3, 7, 9);

        var split = deque.SplitAtSortedEqualRange(3);

        FingerTreeDequeAssert.SequenceEqual([1], split.Before);
        FingerTreeDequeAssert.SequenceEqual([3, 3, 3], split.Range);
        FingerTreeDequeAssert.SequenceEqual([7, 9], split.After);
        FingerTreeDequeAssert.SequenceEqual([1, 3, 3, 3, 7, 9], split.Before.Concat(split.Range).Concat(split.After));
    }

    /// <summary>
    /// Verifies sorted insertion places a new equal item after existing equal items.
    /// </summary>
    [Fact]
    public void InsertSorted_InsertsAtUpperBound()
    {
        var deque = FingerTreeDeque<int>.Create(1, 3, 3, 7);

        FingerTreeDequeAssert.SequenceEqual([1, 3, 3, 3, 7], deque.InsertSorted(3));
        FingerTreeDequeAssert.SequenceEqual([1, 3, 3, 5, 7], deque.InsertSorted(5));
    }

    /// <summary>
    /// Verifies sorted deletion removes the complete equal range.
    /// </summary>
    [Fact]
    public void RemoveAllSorted_RemovesCompleteEqualRange()
    {
        var deque = FingerTreeDeque<int>.Create(1, 3, 3, 3, 7, 9);

        FingerTreeDequeAssert.SequenceEqual([1, 7, 9], deque.RemoveAllSorted(3));
        FingerTreeDequeAssert.SequenceEqual([1, 3, 3, 3, 7, 9], deque.RemoveAllSorted(4));
    }

    /// <summary>
    /// Verifies all sorted-search helpers honor a custom comparer.
    /// </summary>
    [Fact]
    public void SortedSearch_UsesCustomComparer()
    {
        var comparer = Comparer<int>.Create(static (left, right) => right.CompareTo(left));
        var deque = FingerTreeDeque<int>.Create(9, 7, 3, 3, 1);

        Assert.Equal(2, deque.SortedLowerBound(4, comparer));
        Assert.Equal(4, deque.SortedUpperBound(3, comparer));
        Assert.Equal(2, deque.SortedBinarySearch(3, comparer));
        FingerTreeDequeAssert.SequenceEqual([9, 7, 5, 3, 3, 1], deque.InsertSorted(5, comparer));
        FingerTreeDequeAssert.SequenceEqual([9, 7, 1], deque.RemoveAllSorted(3, comparer));
    }
}
