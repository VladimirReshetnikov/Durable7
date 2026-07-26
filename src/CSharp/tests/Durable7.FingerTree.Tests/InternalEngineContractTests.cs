// Tests for the internal engine contract.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Pins shared internal bounds and invariant-failure contracts used by the two finger-tree engines.</summary>
public sealed class InternalEngineContractTests
{
    /// <summary>Verifies digits reject every position outside their actual stored-child range.</summary>
    [Fact]
    public void DigitChildAt_RejectsOutOfRangePositionsInsteadOfClampingToLastSlot()
    {
        var digit = new Digit<int, Leaf<int>>(new Leaf<int>(42));

        Assert.Equal(42, digit.ChildAt(0).Value);
        Assert.Throws<ArgumentOutOfRangeException>(() => digit.ChildAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => digit.ChildAt(1));
        Assert.Throws<ArgumentOutOfRangeException>(() => Digit<int, Leaf<int>>.Empty.ChildAt(0));
    }

    /// <summary>Verifies impossible enumeration descent into either empty engine is reported as an invariant failure.</summary>
    [Fact]
    public void EmptyTreeTryGetChild_UsesConsistentInvariantViolationException()
    {
        IEnumerationBlock<int> dequeTree = EmptyTree<int, Leaf<int>>.Instance;
        IEnumerationBlock<int> measuredTree =
            EmptyMeasuredTree<int, MeasuredLeaf<int, int, SumMeasure<int>>, int, SumMeasure<int>>.Instance;

        Assert.Throws<InvalidOperationException>(() => dequeTree.TryGetChild(0, out _, out _));
        Assert.Throws<InvalidOperationException>(() => measuredTree.TryGetChild(0, out _, out _));
    }
}
