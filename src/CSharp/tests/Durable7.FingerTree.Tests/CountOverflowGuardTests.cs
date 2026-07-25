using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Guards against silent <see cref="int"/> count wraparound on the concatenation entry points. Structural
/// sharing makes overflow cheap to reach: self-concatenation doubles the count in O(log n), so 30 doublings
/// of a two-element rope pass 2^30 elements without meaningful memory cost. Before the guards, the count
/// wrapped to 0 while the structure stayed non-empty, contradicting <c>IsEmpty</c> and making every element
/// unreachable through the bounds-checked indexer.
/// </summary>
public sealed class CountOverflowGuardTests
{
    /// <summary>Verifies Rope.Concat throws instead of wrapping the count past int.MaxValue.</summary>
    [Fact]
    public void RopeConcat_ThrowsOnCountOverflow()
    {
        var rope = Rope<char>.Create('a', 'b');
        for (var i = 0; i < 29; i++)
            rope = rope.Concat(rope);

        Assert.Equal(1 << 30, rope.Count);
        Assert.Throws<OverflowException>(() => rope.Concat(rope));
        Assert.Throws<OverflowException>(() => rope.InsertRange(0, rope));
    }

    /// <summary>Verifies MeasuredRope.Concat throws instead of wrapping the count past int.MaxValue.</summary>
    [Fact]
    public void MeasuredRopeConcat_ThrowsOnCountOverflow()
    {
        var rope = "ab".ToTextRope();
        for (var i = 0; i < 29; i++)
            rope = rope.Concat(rope);

        Assert.Equal(1 << 30, rope.Count);
        Assert.Throws<OverflowException>(() => rope.Concat(rope));
        Assert.Throws<OverflowException>(() => rope.InsertRange(0, rope));
    }

    /// <summary>Verifies PriorityQueue.Meld throws instead of wrapping the count past int.MaxValue.</summary>
    [Fact]
    public void PriorityQueueMeld_ThrowsOnCountOverflow()
    {
        var queue = PriorityQueue<int, int>.Empty.Enqueue(1, 1).Enqueue(2, 2);
        for (var i = 0; i < 29; i++)
            queue = queue.Meld(queue);

        Assert.Equal(1 << 30, queue.Count);
        Assert.Throws<OverflowException>(() => queue.Meld(queue));
    }
}
