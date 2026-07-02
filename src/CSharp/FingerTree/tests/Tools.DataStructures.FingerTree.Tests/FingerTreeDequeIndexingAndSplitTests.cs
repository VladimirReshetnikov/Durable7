using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies indexed lookup, replacement, insertion, deletion, ranges, and split contracts.
/// </summary>
public sealed class FingerTreeDequeIndexingAndSplitTests
{
    /// <summary>
    /// Verifies indexed replacement handles both endpoints and a middle element without mutating the source.
    /// </summary>
    [Fact]
    public void SetItem_ReplacesEndpointsAndMiddleAndPreservesOriginal()
    {
        var original = FingerTreeDeque<int>.Create(1, 2, 3, 4);

        FingerTreeDequeAssert.SequenceEqual([9, 2, 3, 4], original.SetItem(0, 9));
        FingerTreeDequeAssert.SequenceEqual([1, 2, 9, 4], original.SetItem(2, 9));
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 9], original.SetItem(3, 9));
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4], original);
    }

    /// <summary>
    /// Verifies delegate-based indexed update calls the updater once with the old value.
    /// </summary>
    [Fact]
    public void UpdateAt_CallsUpdaterOnceWithOldValue()
    {
        var calls = 0;
        var seen = 0;
        var original = FingerTreeDeque<int>.Create(10, 20, 30);

        var updated = original.UpdateAt(1, value =>
        {
            calls++;
            seen = value;
            return value + 1;
        });

        Assert.Equal(1, calls);
        Assert.Equal(20, seen);
        FingerTreeDequeAssert.SequenceEqual([10, 21, 30], updated);
        FingerTreeDequeAssert.SequenceEqual([10, 20, 30], original);
    }

    /// <summary>
    /// Verifies indexed insertion handles front, middle, and end positions.
    /// </summary>
    [Fact]
    public void InsertAt_InsertsAtFrontMiddleAndEnd()
    {
        var original = FingerTreeDeque<int>.Create(1, 3, 5);

        FingerTreeDequeAssert.SequenceEqual([0, 1, 3, 5], original.InsertAt(0, 0));
        FingerTreeDequeAssert.SequenceEqual([1, 3, 4, 5], original.InsertAt(2, 4));
        FingerTreeDequeAssert.SequenceEqual([1, 3, 5, 7], original.InsertAt(3, 7));
    }

    /// <summary>
    /// Verifies range insertion preserves source and inserted range order.
    /// </summary>
    [Fact]
    public void InsertRange_InsertsEnumerableOnceAtIndex()
    {
        var source = new SingleUseEnumerable<int>([3, 4]);
        var original = FingerTreeDeque<int>.Create(1, 2, 5);

        var updated = original.InsertRange(2, source);

        Assert.Equal(1, source.EnumerationCount);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4, 5], updated);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 5], original);
    }

    /// <summary>
    /// Verifies indexed deletion handles front, middle, and end positions.
    /// </summary>
    [Fact]
    public void RemoveAt_RemovesFrontMiddleAndEnd()
    {
        var original = FingerTreeDeque<int>.Create(1, 2, 3, 4);

        FingerTreeDequeAssert.SequenceEqual([2, 3, 4], original.RemoveAt(0));
        FingerTreeDequeAssert.SequenceEqual([1, 2, 4], original.RemoveAt(2));
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3], original.RemoveAt(3));
    }

    /// <summary>
    /// Verifies contiguous range extraction and removal preserve ordering.
    /// </summary>
    [Fact]
    public void GetRangeAndRemoveRange_ReturnExpectedSegments()
    {
        var original = FingerTreeDeque<int>.Create(1, 2, 3, 4, 5, 6);

        FingerTreeDequeAssert.SequenceEqual([3, 4, 5], original.GetRange(2, 3));
        FingerTreeDequeAssert.SequenceEqual([1, 2, 6], original.RemoveRange(2, 3));
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4, 5, 6], original);
    }

    /// <summary>
    /// Verifies split reconstruction laws for every legal split index.
    /// </summary>
    /// <param name="index">Split index.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(3)]
    [InlineData(6)]
    public void SplitAt_ReconstructsOriginal(int index)
    {
        var original = FingerTreeDeque<int>.Create(1, 2, 3, 4, 5, 6);

        var split = original.SplitAt(index);

        Assert.Equal(index, split.Left.Count);
        Assert.Equal(original.Count - index, split.Right.Count);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4, 5, 6], split.Left.Concat(split.Right));
    }

    /// <summary>
    /// Verifies splitting around one indexed item returns the expected prefix, item, and suffix.
    /// </summary>
    [Fact]
    public void SplitItemAt_ReturnsLeftItemAndRight()
    {
        var original = FingerTreeDeque<int>.Create(1, 2, 3, 4, 5);

        var split = original.SplitItemAt(2);

        FingerTreeDequeAssert.SequenceEqual([1, 2], split.Left);
        Assert.Equal(3, split.Item);
        FingerTreeDequeAssert.SequenceEqual([4, 5], split.Right);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4, 5], split.Left.AddLast(split.Item).Concat(split.Right));
    }

    /// <summary>
    /// Verifies splitting around a range returns the expected three partitions.
    /// </summary>
    [Fact]
    public void SplitRange_ReturnsBeforeRangeAndAfter()
    {
        var original = FingerTreeDeque<int>.Create(1, 2, 3, 4, 5, 6);

        var split = original.SplitRange(2, 3);

        FingerTreeDequeAssert.SequenceEqual([1, 2], split.Before);
        FingerTreeDequeAssert.SequenceEqual([3, 4, 5], split.Range);
        FingerTreeDequeAssert.SequenceEqual([6], split.After);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4, 5, 6], split.Before.Concat(split.Range).Concat(split.After));
    }

    /// <summary>
    /// Verifies invalid index and range arguments throw the expected exception types.
    /// </summary>
    [Fact]
    public void InvalidIndexAndRangeArguments_ThrowArgumentOutOfRangeException()
    {
        var deque = FingerTreeDeque<int>.Create(1, 2, 3);

        Assert.Throws<ArgumentOutOfRangeException>(() => deque[-1]);
        Assert.Throws<ArgumentOutOfRangeException>(() => deque[3]);
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.SetItem(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.SetItem(3, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.UpdateAt(3, static value => value));
        Assert.Throws<ArgumentNullException>("updater", () => deque.UpdateAt(0, null!));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.InsertAt(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.InsertAt(4, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.RemoveAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.RemoveAt(3));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.GetRange(1, 3));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.GetRange(1, -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.RemoveRange(1, 3));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.SplitAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.SplitAt(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.SplitItemAt(3));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.SplitRange(1, 3));
    }
}
