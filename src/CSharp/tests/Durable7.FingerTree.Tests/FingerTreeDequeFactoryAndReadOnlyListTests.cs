using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies factory, empty-instance, and read-only-list contracts for <see cref="FingerTreeDeque{T}"/>.
/// </summary>
public sealed class FingerTreeDequeFactoryAndReadOnlyListTests
{
    /// <summary>
    /// Verifies the empty instance exposes the standard empty read-only-list shape.
    /// </summary>
    [Fact]
    public void Empty_ExposesEmptyReadOnlyListShape()
    {
        var deque = FingerTreeDeque<string>.Empty;

        FingerTreeDequeAssert.SequenceEqual(Array.Empty<string>(), deque);
        Assert.Throws<InvalidOperationException>(() => deque.First);
        Assert.Throws<InvalidOperationException>(() => deque.Last);
        Assert.Throws<ArgumentOutOfRangeException>(() => deque[0]);
        Assert.Throws<InvalidOperationException>(() => deque.RemoveFirst());
        Assert.Throws<InvalidOperationException>(() => deque.RemoveLast());
        Assert.Throws<InvalidOperationException>(() => deque.PopFirst());
        Assert.Throws<InvalidOperationException>(() => deque.PopLast());

        Assert.False(deque.TryPopFirst(out var first, out var firstRest));
        Assert.Null(first);
        Assert.Same(FingerTreeDeque<string>.Empty, firstRest);

        Assert.False(deque.TryPopLast(out var last, out var lastRest));
        Assert.Null(last);
        Assert.Same(FingerTreeDeque<string>.Empty, lastRest);
    }

    /// <summary>
    /// Verifies the span-backed params factory preserves order and nullable values.
    /// </summary>
    [Fact]
    public void Create_WithParams_PreservesOrderAndNullValues()
    {
        var deque = FingerTreeDeque<string?>.Create("alpha", null, "omega");

        FingerTreeDequeAssert.SequenceEqual(new string?[] { "alpha", null, "omega" }, deque);
    }

    /// <summary>
    /// Verifies enumerable construction preserves enumeration order and enumerates only once.
    /// </summary>
    [Fact]
    public void CreateRange_EnumeratesSourceOnceAndPreservesOrder()
    {
        var source = new SingleUseEnumerable<int>([1, 2, 3, 4]);

        var deque = FingerTreeDeque<int>.CreateRange(source);

        Assert.Equal(1, source.EnumerationCount);
        FingerTreeDequeAssert.SequenceEqual([1, 2, 3, 4], deque);
    }

    /// <summary>
    /// Verifies null enumerable sources are rejected by factory and range APIs.
    /// </summary>
    [Fact]
    public void NullEnumerableSources_ThrowArgumentNullException()
    {
        Assert.Throws<ArgumentNullException>("items", () => FingerTreeDeque<int>.CreateRange(null!));

        var deque = FingerTreeDeque<int>.Empty;
        Assert.Throws<ArgumentNullException>("items", () => deque.AddRange((IEnumerable<int>)null!));
        Assert.Throws<ArgumentNullException>("items", () => deque.InsertRange(0, null!));
    }

    /// <summary>
    /// Verifies copying validates destination array arguments like a .NET collection.
    /// </summary>
    [Fact]
    public void CopyTo_ValidatesDestinationArguments()
    {
        var deque = FingerTreeDeque<int>.Create(1, 2, 3);

        Assert.Throws<ArgumentNullException>("array", () => deque.CopyTo(null!, 0));
        Assert.Throws<ArgumentOutOfRangeException>("arrayIndex", () => deque.CopyTo(new int[3], -1));
        Assert.Throws<ArgumentException>(() => deque.CopyTo(new int[3], 1));
    }
}
