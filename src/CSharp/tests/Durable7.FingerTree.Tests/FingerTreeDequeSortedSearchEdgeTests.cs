// Tests for the finger tree deque sorted search edge.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies sorted-search helpers on boundary shapes: empty and single-element deques, all-duplicate
/// contents, null elements with a null-accepting comparer, projection comparers, and deep sorted sequences
/// that exercise the rightmost-element signposts across multiple tree levels.
/// </summary>
public sealed class FingerTreeDequeSortedSearchEdgeTests
{
    /// <summary>
    /// Verifies every sorted helper degenerates correctly on the empty deque.
    /// </summary>
    [Fact]
    public void SortedHelpers_OnEmptyDeque_ReturnDegenerateResults()
    {
        var empty = FingerTreeDeque<int>.Empty;

        Assert.Equal(0, empty.SortedLowerBound(5));
        Assert.Equal(0, empty.SortedUpperBound(5));
        Assert.Equal(~0, empty.SortedBinarySearch(5));
        Assert.False(empty.SortedContains(5));

        var lowerSplit = empty.SplitAtSortedLowerBound(5);
        Assert.Same(empty, lowerSplit.Left);
        Assert.Same(empty, lowerSplit.Right);

        var equalRange = empty.SplitAtSortedEqualRange(5);
        Assert.Same(empty, equalRange.Range);

        FingerTreeDequeAssert.SequenceEqual([5], empty.InsertSorted(5));
        Assert.Same(empty, empty.RemoveAllSorted(5));
    }

    /// <summary>
    /// Verifies sorted bounds on a single-element deque for smaller, equal, and greater search items.
    /// </summary>
    [Fact]
    public void SortedBounds_OnSingleElement_HandleAllRelations()
    {
        var single = FingerTreeDeque<int>.Create(5);

        Assert.Equal(0, single.SortedLowerBound(4));
        Assert.Equal(0, single.SortedLowerBound(5));
        Assert.Equal(1, single.SortedLowerBound(6));
        Assert.Equal(0, single.SortedUpperBound(4));
        Assert.Equal(1, single.SortedUpperBound(5));
        Assert.Equal(0, single.SortedBinarySearch(5));
        Assert.Equal(~0, single.SortedBinarySearch(4));
        Assert.Equal(~1, single.SortedBinarySearch(6));
    }

    /// <summary>
    /// Verifies an all-duplicate deque produces full-width equal ranges and removes down to the canonical
    /// empty instance.
    /// </summary>
    [Fact]
    public void SortedHelpers_OnAllDuplicates_CoverWholeDeque()
    {
        var duplicates = FingerTreeDeque<int>.Create(5, 5, 5, 5, 5);

        Assert.Equal(0, duplicates.SortedLowerBound(5));
        Assert.Equal(5, duplicates.SortedUpperBound(5));
        Assert.Equal(0, duplicates.SortedBinarySearch(5));

        var split = duplicates.SplitAtSortedEqualRange(5);
        Assert.True(split.Before.IsEmpty);
        Assert.Equal(5, split.Range.Count);
        Assert.True(split.After.IsEmpty);

        Assert.Same(FingerTreeDeque<int>.Empty, duplicates.RemoveAllSorted(5));
    }

    /// <summary>
    /// Verifies null elements participate in sorted search under the default null-accepting string comparer.
    /// </summary>
    [Fact]
    public void SortedHelpers_WithNullElements_UseComparerNullOrdering()
    {
        var deque = FingerTreeDeque<string?>.Create(null, null, "alpha", "omega");

        Assert.Equal(0, deque.SortedLowerBound(null));
        Assert.Equal(2, deque.SortedUpperBound(null));
        Assert.Equal(0, deque.SortedBinarySearch(null));
        Assert.True(deque.SortedContains(null));
        Assert.Equal(2, deque.SortedLowerBound("alpha"));
        Assert.Equal(~3, deque.SortedBinarySearch("beta"));

        FingerTreeDequeAssert.SequenceEqual(
            new string?[] { null, null, null, "alpha", "omega" },
            deque.InsertSorted(null));
        FingerTreeDequeAssert.SequenceEqual(
            new string?[] { "alpha", "omega" },
            deque.RemoveAllSorted(null));

        var equalRange = deque.SplitAtSortedEqualRange(null);
        Assert.Equal(2, equalRange.Range.Count);
        Assert.True(equalRange.Before.IsEmpty);
        Assert.Equal(2, equalRange.After.Count);
    }

    /// <summary>
    /// Verifies sorted helpers honor a projection comparer whose equality classes differ from element
    /// equality.
    /// </summary>
    [Fact]
    public void SortedHelpers_WithProjectionComparer_GroupByProjection()
    {
        var byLength = Comparer<string>.Create(static (left, right) => left.Length.CompareTo(right.Length));
        var deque = FingerTreeDeque<string>.Create("a", "bb", "cc", "ddd");

        Assert.Equal(1, deque.SortedLowerBound("xx", byLength));
        Assert.Equal(3, deque.SortedUpperBound("xx", byLength));
        Assert.Equal(1, deque.SortedBinarySearch("xx", byLength));
        Assert.True(deque.SortedContains("zz", byLength));

        var equalRange = deque.SplitAtSortedEqualRange("xx", byLength);
        FingerTreeDequeAssert.SequenceEqual(["bb", "cc"], equalRange.Range);

        FingerTreeDequeAssert.SequenceEqual(
            ["a", "bb", "cc", "yy", "ddd"],
            deque.InsertSorted("yy", byLength));
        FingerTreeDequeAssert.SequenceEqual(
            ["a", "ddd"],
            deque.RemoveAllSorted("xx", byLength));
    }

    /// <summary>
    /// Verifies bounds, equal ranges, sorted insertion, and sorted removal against a binary-search model on
    /// a deep sorted deque with duplicate blocks, exercising signposts across several tree levels.
    /// </summary>
    [Fact]
    public void SortedHelpers_OnDeepDuplicateBlocks_MatchReferenceModel()
    {
        const int distinctValues = 600;
        const int blockSize = 3;
        var values = Enumerable.Range(0, distinctValues).SelectMany(value => Enumerable.Repeat(value, blockSize)).ToList();
        var deque = FingerTreeDeque<int>.CreateRange(values);
        deque.ValidateInvariants();

        foreach (var item in new[] { -1, 0, 1, 7, distinctValues / 2, distinctValues - 1, distinctValues })
        {
            var expectedLower = values.FindIndex(value => value >= item);
            if (expectedLower < 0)
                expectedLower = values.Count;
            var expectedUpper = values.FindIndex(value => value > item);
            if (expectedUpper < 0)
                expectedUpper = values.Count;

            Assert.Equal(expectedLower, deque.SortedLowerBound(item));
            Assert.Equal(expectedUpper, deque.SortedUpperBound(item));

            var split = deque.SplitAtSortedEqualRange(item);
            Assert.Equal(expectedLower, split.Before.Count);
            Assert.Equal(expectedUpper - expectedLower, split.Range.Count);
            Assert.All(split.Range.ToArray(), value => Assert.Equal(item, value));

            var inserted = deque.InsertSorted(item);
            inserted.ValidateInvariants();
            Assert.Equal(values.Count + 1, inserted.Count);
            Assert.Equal(item, inserted[expectedUpper]);

            var removed = deque.RemoveAllSorted(item);
            removed.ValidateInvariants();
            Assert.Equal(values.Count - (expectedUpper - expectedLower), removed.Count);
        }
    }

    /// <summary>
    /// Verifies search items beyond both ends resolve with end-local cost paths and exact boundary indexes.
    /// </summary>
    [Fact]
    public void SortedBounds_BeyondBothEnds_ReturnBoundaryIndexes()
    {
        var deque = FingerTreeDeque<int>.CreateRange(Enumerable.Range(0, 1024).Select(value => value * 2));

        Assert.Equal(0, deque.SortedLowerBound(int.MinValue));
        Assert.Equal(0, deque.SortedUpperBound(int.MinValue));
        Assert.Equal(1024, deque.SortedLowerBound(int.MaxValue));
        Assert.Equal(1024, deque.SortedUpperBound(int.MaxValue));

        var below = deque.SplitAtSortedLowerBound(int.MinValue);
        Assert.Same(FingerTreeDeque<int>.Empty, below.Left);
        Assert.Same(deque, below.Right);

        var above = deque.SplitAtSortedUpperBound(int.MaxValue);
        Assert.Same(deque, above.Left);
        Assert.Same(FingerTreeDeque<int>.Empty, above.Right);
    }

    /// <summary>Verifies fused sorted update/removal across a deep tree and duplicate run.</summary>
    [Fact]
    public void FusedSortedEdits_LocateAndRebuildOneBoundaryPath()
    {
        var values = Enumerable.Range(0, 2_048).SelectMany(value => Enumerable.Repeat(value, 2)).ToArray();
        var deque = FingerTreeDeque<int>.CreateRange(values);

        Assert.True(deque.TryRemoveSortedItem(1_337, Comparer<int>.Default,
            out var removedIndex, out var removedValue, out var removed));
        Assert.Equal(2_674, removedIndex);
        Assert.Equal(1_337, removedValue);
        Assert.Equal(values.Length - 1, removed.Count);
        Assert.Equal(1_337, removed[removedIndex]);

        Assert.True(deque.TryUpdateSortedItem(1_337, Comparer<int>.Default,
            static value => value, out var stored, out var updated));
        Assert.Equal(1_337, stored);
        Assert.Equal(values, updated.ToArray());

        Assert.False(deque.TryRemoveSortedItem(3_000, Comparer<int>.Default,
            out var missingIndex, out _, out var unchanged));
        Assert.Equal(-1, missingIndex);
        Assert.Same(deque, unchanged);
        Assert.Equal(values, deque.ToArray());
        removed.ValidateInvariants();
        updated.ValidateInvariants();
    }
}
