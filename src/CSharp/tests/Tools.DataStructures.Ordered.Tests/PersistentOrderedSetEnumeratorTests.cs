using System.Collections;
using Xunit;

namespace Tools.DataStructures.Ordered.Tests;

/// <summary>State-machine, interface, copy-divergence, and snapshot tests for ordered enumeration.</summary>
public sealed class PersistentOrderedSetEnumeratorTests
{
    /// <summary>Verifies a default-initialized enumerator is safely and permanently exhausted.</summary>
    [Fact]
    public void DefaultEnumerator_IsExhaustedAndHasDefaultCurrent()
    {
        var enumerator = default(PersistentOrderedSet<string>.Enumerator);
        Assert.Null(enumerator.Current);
        Assert.Null(((IEnumerator)enumerator).Current);
        Assert.False(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        enumerator.Dispose();
        Assert.Null(enumerator.Current);
    }

    /// <summary>Verifies Current is default before traversal and after idempotent exhaustion.</summary>
    [Fact]
    public void Current_IsBracketedByDefaultValues()
    {
        var enumerator = PersistentOrderedSet<int>.CreateRange([3, 1, 2]).GetEnumerator();
        Assert.Equal(0, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(3, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(1, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(2, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.Equal(0, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.Equal(0, enumerator.Current);
    }

    /// <summary>Verifies pattern, generic-interface, non-generic, and IReadOnlySet paths agree.</summary>
    [Fact]
    public void EveryEnumerationPath_UsesOrderedRepresentativeOrder()
    {
        var comparer = new RepresentativeComparer();
        var items = Enumerable.Range(0, 32).Select(index => new Representative(index, $"item-{index}")).ToArray();
        var set = PersistentOrderedSet<Representative>.CreateRange(items, comparer);

        OrderedSetAssert.AssertReferenceSequence(items, set);
        OrderedSetAssert.AssertReferenceSequence(items, (IEnumerable<Representative>)set);
        OrderedSetAssert.AssertReferenceSequence(items, ((IEnumerable)set).Cast<Representative>());
        OrderedSetAssert.AssertReferenceSequence(items, (IReadOnlySet<Representative>)set);
        OrderedSetAssert.AssertReferenceSequence(items, set.ToArray());
    }

    /// <summary>Verifies interface Reset is rejected before, during, and after traversal.</summary>
    [Fact]
    public void InterfaceReset_IsAlwaysUnsupported()
    {
        using var enumerator = ((IEnumerable<int>)PersistentOrderedSet<int>.CreateRange([1, 2])).GetEnumerator();
        Assert.Throws<NotSupportedException>(enumerator.Reset);
        Assert.True(enumerator.MoveNext());
        Assert.Throws<NotSupportedException>(enumerator.Reset);
        Assert.True(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        Assert.Throws<NotSupportedException>(enumerator.Reset);
    }

    /// <summary>Verifies Dispose is a no-op throughout traversal.</summary>
    [Fact]
    public void Dispose_ReleasesNothingAndDoesNotChangePosition()
    {
        var enumerator = PersistentOrderedSet<int>.CreateRange([1, 2, 3]).GetEnumerator();
        enumerator.Dispose();
        Assert.True(enumerator.MoveNext());
        Assert.Equal(1, enumerator.Current);
        enumerator.Dispose();
        Assert.Equal(1, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.True(enumerator.MoveNext());
        enumerator.Dispose();
        Assert.False(enumerator.MoveNext());
    }

    /// <summary>Locks the inherited FingerTree fail-fast contract for divergent active value copies.</summary>
    [Fact]
    public void AdvancedEnumeratorCopy_InvalidatesLaggingCopies()
    {
        var set = PersistentOrderedSet<int>.CreateRange(Enumerable.Range(0, 100));
        var first = set.GetEnumerator();
        for (var index = 0; index < 10; index++)
            Assert.True(first.MoveNext());
        Assert.Equal(9, first.Current);

        var laggingCopy = first;
        Assert.True(first.MoveNext());
        Assert.Equal(10, first.Current);
        Assert.Throws<InvalidOperationException>(() => laggingCopy.MoveNext());

        Assert.True(first.MoveNext());
        Assert.Equal(11, first.Current);
        Assert.Equal(Enumerable.Range(0, 100), set.ToArray());
    }

    /// <summary>Verifies one chosen value copy can consume the full remaining sequence.</summary>
    [Fact]
    public void SingleActiveCopy_YieldsEveryRemainingRepresentative()
    {
        var set = PersistentOrderedSet<int>.CreateRange(Enumerable.Range(0, 50));
        var original = set.GetEnumerator();
        for (var index = 0; index < 5; index++)
            Assert.True(original.MoveNext());
        var copy = original;
        List<int> observed = [];
        while (copy.MoveNext())
            observed.Add(copy.Current);
        Assert.Equal(Enumerable.Range(5, 45), observed);
    }

    /// <summary>Verifies empty and exhausted enumerator copies remain idempotently exhausted.</summary>
    [Fact]
    public void EmptyAndExhaustedCopies_StayExhausted()
    {
        var empty = PersistentOrderedSet<int>.Empty.GetEnumerator();
        var emptyCopy = empty;
        Assert.False(empty.MoveNext());
        Assert.False(emptyCopy.MoveNext());

        var exhausted = PersistentOrderedSet<int>.CreateRange([1, 2]).GetEnumerator();
        Assert.True(exhausted.MoveNext());
        Assert.True(exhausted.MoveNext());
        Assert.False(exhausted.MoveNext());
        var exhaustedCopy = exhausted;
        Assert.False(exhausted.MoveNext());
        Assert.False(exhaustedCopy.MoveNext());
    }

    /// <summary>Verifies separately created enumerators own independent traversal states.</summary>
    [Fact]
    public void FreshEnumerators_AdvanceIndependently()
    {
        var set = PersistentOrderedSet<int>.CreateRange(Enumerable.Range(0, 20));
        var first = set.GetEnumerator();
        var second = set.GetEnumerator();
        Assert.True(first.MoveNext());
        Assert.True(first.MoveNext());
        Assert.Equal(1, first.Current);
        Assert.True(second.MoveNext());
        Assert.Equal(0, second.Current);
        Assert.True(second.MoveNext());
        Assert.Equal(1, second.Current);
        Assert.True(first.MoveNext());
        Assert.Equal(2, first.Current);
    }

    /// <summary>Verifies an enumerator remains bound to the immutable version that created it.</summary>
    [Fact]
    public void Enumerator_IsVersionBoundAcrossSuccessorUpdates()
    {
        var source = PersistentOrderedSet<int>.CreateRange([1, 2, 3]);
        var enumerator = source.GetEnumerator();
        var successor = source.MoveToFirst(3).Add(4).Remove(2);
        List<int> observed = [];
        while (enumerator.MoveNext())
            observed.Add(enumerator.Current);

        Assert.Equal(new[] { 1, 2, 3 }, observed);
        OrderedSetAssert.Matches(new[] { 3, 1, 4 }, successor);
        OrderedSetAssert.Matches(new[] { 1, 2, 3 }, source);
    }
}
