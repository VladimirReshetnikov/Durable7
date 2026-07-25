using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies deque endpoint insertion, removal, concatenation, and persistence contracts.
/// </summary>
public sealed class FingerTreeDequeEndpointOperationTests
{
    /// <summary>
    /// Verifies left and right insertion build the expected order without mutating the source.
    /// </summary>
    [Fact]
    public void AddFirstAndAddLast_BuildExpectedOrderAndPreserveOriginal()
    {
        var original = FingerTreeDeque<int>.Create(2, 3);

        var updated = original.AddFirst(1).AddLast(4);

        FingerTreeDequeAssert.SequenceEqual([2, 3], original);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4], updated);
    }

    /// <summary>
    /// Verifies endpoint removal returns the expected suffix and prefix while preserving the source.
    /// </summary>
    [Fact]
    public void RemoveFirstAndRemoveLast_ReturnExpectedRestAndPreserveOriginal()
    {
        var original = FingerTreeDeque<int>.Create(1, 2, 3, 4);

        var withoutFirst = original.RemoveFirst();
        var withoutLast = original.RemoveLast();

        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4], original);
        FingerTreeDequeAssert.SequenceEqual([2, 3, 4], withoutFirst);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3], withoutLast);
    }

    /// <summary>
    /// Verifies endpoint pop APIs return both the removed value and remaining deque.
    /// </summary>
    [Fact]
    public void PopFirstAndPopLast_ReturnEndpointValueAndRest()
    {
        var deque = FingerTreeDeque<int>.Create(1, 2, 3);

        var first = deque.PopFirst();
        var last = deque.PopLast();

        Assert.Equal(1, first.Value);
        FingerTreeDequeAssert.SequenceEqual([2, 3], first.Rest);
        Assert.Equal(3, last.Value);
        FingerTreeDequeAssert.SequenceEqual([1, 2], last.Rest);

        Assert.True(deque.TryPopFirst(out var firstValue, out var firstRest));
        Assert.Equal(1, firstValue);
        FingerTreeDequeAssert.SequenceEqual([2, 3], firstRest);

        Assert.True(deque.TryPopLast(out var lastValue, out var lastRest));
        Assert.Equal(3, lastValue);
        FingerTreeDequeAssert.SequenceEqual([1, 2], lastRest);
    }

    /// <summary>
    /// Verifies concatenation preserves left-to-right order and leaves both inputs unchanged.
    /// </summary>
    [Fact]
    public void Concat_PreservesOrderAndInputs()
    {
        var left = FingerTreeDeque<int>.Create(1, 2, 3);
        var right = FingerTreeDeque<int>.Create(4, 5);

        var combined = left.Concat(right);

        FingerTreeDequeAssert.SequenceEqual([1, 2, 3], left);
        FingerTreeDequeAssert.SequenceEqual([4, 5], right);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4, 5], combined);
    }

    /// <summary>
    /// Verifies adding another deque as a range is equivalent to concatenation.
    /// </summary>
    [Fact]
    public void AddRange_WithDeque_IsEquivalentToConcat()
    {
        var left = FingerTreeDeque<int>.Create(1, 2);
        var right = FingerTreeDeque<int>.Create(3, 4);

        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4], left.AddRange(right));
    }

    /// <summary>
    /// Verifies adding an enumerable range appends its values once in enumeration order.
    /// </summary>
    [Fact]
    public void AddRange_WithEnumerable_EnumeratesOnceAndAppends()
    {
        var source = new SingleUseEnumerable<int>([3, 4, 5]);

        var updated = FingerTreeDeque<int>.Create(1, 2).AddRange(source);

        Assert.Equal(1, source.EnumerationCount);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4, 5], updated);
    }
}
