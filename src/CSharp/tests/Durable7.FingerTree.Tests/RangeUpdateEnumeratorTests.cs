// Tests for the range update enumerator.

using System.Collections;
using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Concrete struct, copied-state, boxed-interface, reset, exhaustion, and version-bound enumeration.</summary>
public sealed class RangeUpdateEnumeratorTests
{
    /// <summary>Verifies the default enumerator is safely exhausted and exposes default current.</summary>
    [Fact]
    public void DefaultEnumerator_IsPermanentlyExhausted()
    {
        var enumerator = default(RangeUpdateSequence<
            int,
            RangeUpdateMeasure,
            RangeUpdateTag,
            RangeUpdateAffineAlgebra>.Enumerator);

        Assert.Equal(0, enumerator.Current);
        Assert.Equal(0, ((IEnumerator)enumerator).Current);
        Assert.False(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        enumerator.Dispose();
        Assert.Equal(0, enumerator.Current);
    }

    /// <summary>Verifies logical tagged values appear identically through pattern and both interface paths.</summary>
    [Fact]
    public void EveryEnumerationPath_SeesLogicalTaggedOrder()
    {
        var model = Enumerable.Range(0, 100).ToList();
        var sequence = RangeUpdateAssert.Create(model.ToArray());
        sequence = sequence.ApplyRange(0, sequence.Count, RangeUpdateTag.Add(10));
        RangeUpdateAssert.Apply(model, 0, model.Count, RangeUpdateTag.Add(10));
        sequence = sequence.ApplyRange(20, 60, RangeUpdateTag.Assign(-4));
        RangeUpdateAssert.Apply(model, 20, 60, RangeUpdateTag.Assign(-4));
        sequence = sequence.ApplyRange(35, 17, RangeUpdateTag.Add(9));
        RangeUpdateAssert.Apply(model, 35, 17, RangeUpdateTag.Add(9));

        Assert.Equal(model, sequence.ToArray());
        Assert.Equal(model, ((IEnumerable<int>)sequence).ToArray());
        Assert.Equal(model.Cast<object?>(), ((IEnumerable)sequence).Cast<object?>());
    }

    /// <summary>Verifies Current is default before the first element and after idempotent exhaustion.</summary>
    [Fact]
    public void Current_IsDefaultOutsideActiveTraversal()
    {
        var enumerator = RangeUpdateAssert.Create(3, 1, 4).GetEnumerator();
        Assert.Equal(0, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(3, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(1, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(4, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.Equal(0, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.Equal(0, enumerator.Current);
    }

    /// <summary>Verifies interface Reset is unsupported before, during, and after traversal.</summary>
    [Fact]
    public void InterfaceReset_IsAlwaysUnsupported()
    {
        using var enumerator = ((IEnumerable<int>)RangeUpdateAssert.Create(1, 2)).GetEnumerator();
        Assert.Throws<NotSupportedException>(enumerator.Reset);
        Assert.True(enumerator.MoveNext());
        Assert.Throws<NotSupportedException>(enumerator.Reset);
        Assert.True(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        Assert.Throws<NotSupportedException>(enumerator.Reset);
    }

    /// <summary>Locks fail-fast behavior when one active struct copy advances beyond another shared-stack copy.</summary>
    [Fact]
    public void AdvancedEnumeratorCopy_InvalidatesLaggingCopy()
    {
        var sequence = RangeUpdateAssert.Create(Enumerable.Range(0, 128).ToArray())
            .ApplyRange(0, 128, RangeUpdateTag.Add(1));
        var first = sequence.GetEnumerator();
        for (var index = 0; index < 17; index++)
            Assert.True(first.MoveNext());
        Assert.Equal(17, first.Current);

        var lagging = first;
        Assert.True(first.MoveNext());
        Assert.Equal(18, first.Current);
        Assert.Throws<InvalidOperationException>(() => lagging.MoveNext());

        Assert.True(first.MoveNext());
        Assert.Equal(19, first.Current);
    }

    /// <summary>Verifies one selected copy can consume the full remainder and exhausted copies stay exhausted.</summary>
    [Fact]
    public void SelectedAndExhaustedCopies_HaveDeterministicState()
    {
        var original = RangeUpdateAssert.Create(Enumerable.Range(0, 50).ToArray()).GetEnumerator();
        for (var index = 0; index < 5; index++)
            Assert.True(original.MoveNext());

        var selected = original;
        var observed = new List<int>();
        while (selected.MoveNext())
            observed.Add(selected.Current);
        Assert.Equal(Enumerable.Range(5, 45), observed);

        var exhaustedCopy = selected;
        Assert.False(selected.MoveNext());
        Assert.False(exhaustedCopy.MoveNext());

        var empty = RangeUpdateAssert.Create().GetEnumerator();
        var emptyCopy = empty;
        Assert.False(empty.MoveNext());
        Assert.False(emptyCopy.MoveNext());
    }

    /// <summary>Verifies independently created enumerators advance independently.</summary>
    [Fact]
    public void FreshEnumerators_HaveIndependentTraversalState()
    {
        var sequence = RangeUpdateAssert.Create(Enumerable.Range(0, 20).ToArray());
        var first = sequence.GetEnumerator();
        var second = sequence.GetEnumerator();

        Assert.True(first.MoveNext());
        Assert.True(first.MoveNext());
        Assert.Equal(1, first.Current);
        Assert.True(second.MoveNext());
        Assert.Equal(0, second.Current);
        Assert.True(first.MoveNext());
        Assert.Equal(2, first.Current);
        Assert.True(second.MoveNext());
        Assert.Equal(1, second.Current);
    }

    /// <summary>Verifies an enumerator remains bound to the immutable version that created it.</summary>
    [Fact]
    public void Enumerator_IsBoundToCreatingVersion()
    {
        var source = RangeUpdateAssert.Create(1, 2, 3, 4);
        var enumerator = source.GetEnumerator();
        var successor = source.ApplyRange(0, source.Count, RangeUpdateTag.Add(10))
            .Insert(2, 99)
            .RemoveAt(0);

        var observed = new List<int>();
        while (enumerator.MoveNext())
            observed.Add(enumerator.Current);

        Assert.Equal(new[] { 1, 2, 3, 4 }, observed);
        RangeUpdateAssert.Matches([12, 99, 13, 14], successor);
        RangeUpdateAssert.Matches([1, 2, 3, 4], source);
    }
}
