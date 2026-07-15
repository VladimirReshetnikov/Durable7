using System.Collections;
using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>State-machine and view-order tests for the expanded persistent hash-bag enumerator.</summary>
public sealed class PersistentHashBagEnumeratorTests
{
    /// <summary>Verifies a default-initialized bag enumerator is safely and permanently exhausted.</summary>
    [Fact]
    public void DefaultEnumerator_ReportsNoOccurrences()
    {
        var enumerator = default(PersistentHashBag<string>.Enumerator);

        Assert.Null(enumerator.Current);
        Assert.Null(((IEnumerator)enumerator).Current);
        Assert.False(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        Assert.Null(enumerator.Current);
    }

    /// <summary>Verifies <c>Current</c> is default before the first occurrence and after exhaustion.</summary>
    [Fact]
    public void MoveNext_BracketsExpandedOccurrencesWithDefaultCurrent()
    {
        var enumerator = PersistentHashBag<int>.Empty.AddCopies(7, 2).GetEnumerator();

        Assert.Equal(0, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(7, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(7, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.Equal(0, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.Equal(0, enumerator.Current);
    }

    /// <summary>Verifies copying an active enumerator copies independent traversal and repetition state.</summary>
    [Fact]
    public void CopiedEnumerator_AdvancesIndependentlyWithinRepeatedClass()
    {
        var bag = PersistentHashBag<int>.Empty.AddCopies(7, 4);
        var expected = bag.ToArray();
        var original = bag.GetEnumerator();
        Assert.True(original.MoveNext());
        var copy = original;

        var fromOriginal = DrainIncludingCurrent(ref original);
        var fromCopy = DrainIncludingCurrent(ref copy);

        Assert.Equal(expected, fromOriginal);
        Assert.Equal(expected, fromCopy);
    }

    /// <summary>Verifies disposing the concrete value enumerator is a no-op.</summary>
    [Fact]
    public void Dispose_IsANoOp()
    {
        var enumerator = PersistentHashBag<string>.Empty.AddCopies("value", 3).GetEnumerator();

        Assert.True(enumerator.MoveNext());
        enumerator.Dispose();
        Assert.Equal("value", enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal("value", enumerator.Current);
        enumerator.Dispose();
        Assert.True(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        enumerator.Dispose();
        Assert.Null(enumerator.Current);
    }

    /// <summary>Verifies generic and non-generic interface enumeration produce the expanded sequence.</summary>
    [Fact]
    public void InterfaceEnumeration_ProducesTheConcreteExpandedOrder()
    {
        var bag = PersistentHashBag<string>.Empty
            .AddCopies("alpha", 2)
            .Add("beta")
            .AddCopies("gamma", 3);
        var expected = bag.ToArray();

        Assert.Equal(expected, ((IEnumerable<string>)bag).ToArray());
        Assert.Equal(expected, ((IEnumerable)bag).Cast<string>().ToArray());
    }

    /// <summary>Verifies interface reset is rejected before, during, and after traversal.</summary>
    [Fact]
    public void InterfaceEnumerator_RejectsResetAtEveryPosition()
    {
        var bag = PersistentHashBag<string>.Empty.AddCopies("value", 2);
        using var enumerator = ((IEnumerable<string>)bag).GetEnumerator();

        Assert.Null(enumerator.Current);
        Assert.Throws<NotSupportedException>(enumerator.Reset);
        Assert.True(enumerator.MoveNext());
        Assert.Throws<NotSupportedException>(enumerator.Reset);
        Assert.True(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        Assert.Null(enumerator.Current);
        Assert.Throws<NotSupportedException>(enumerator.Reset);
    }

    /// <summary>Verifies distinct, entry, and expanded views have the same representative order.</summary>
    [Fact]
    public void Views_ShareDistinctOrderAndExpandedClassesAreContiguous()
    {
        var alpha = NewString("alpha");
        var beta = NewString("beta");
        var gamma = NewString("gamma");
        var bag = PersistentHashBag<string>.Empty
            .AddCopies(alpha, 3)
            .Add(beta)
            .AddCopies(gamma, 2);
        var entries = bag.Entries.ToArray();
        var distinct = bag.DistinctItems.ToArray();

        Assert.Equal(entries.Length, distinct.Length);
        for (var index = 0; index < entries.Length; index++)
            Assert.Same(entries[index].Key, distinct[index]);

        var expectedExpanded = entries
            .SelectMany(entry => Enumerable.Repeat(entry.Key, entry.Value))
            .ToArray();
        var actualExpanded = bag.ToArray();

        Assert.Equal(expectedExpanded.Length, actualExpanded.Length);
        for (var index = 0; index < expectedExpanded.Length; index++)
            Assert.Same(expectedExpanded[index], actualExpanded[index]);
    }

    /// <summary>Verifies view objects remain bound to the immutable version from which they came.</summary>
    [Fact]
    public void Views_AreVersionBoundAndStableAcrossSuccessorUpdates()
    {
        var source = PersistentHashBag<int>.Empty
            .AddCopies(1, 2)
            .Add(2);
        var distinctView = source.DistinctItems;
        var entryView = source.Entries;
        var expectedDistinct = distinctView.ToArray();
        var expectedEntries = entryView.ToArray();

        var successor = source.AddCopies(3, 4).Remove(1);

        Assert.Equal(expectedDistinct, distinctView.ToArray());
        Assert.Equal(expectedEntries, entryView.ToArray());
        Assert.Equal(expectedDistinct, source.DistinctItems.ToArray());
        Assert.Equal(expectedEntries, source.Entries.ToArray());
        Assert.Contains(3, successor.DistinctItems);
        Assert.DoesNotContain(3, distinctView);
        Assert.Equal(2, source.CountOf(1));
        Assert.Equal(1, successor.CountOf(1));
    }

    /// <summary>Verifies all three enumeration projections are stable for an unchanged version.</summary>
    [Fact]
    public void EnumerationOrder_IsStableForAnUnchangedVersion()
    {
        var bag = PersistentHashBag<int>.Create(new FewBucketsComparer());
        for (var value = 0; value < 64; value++)
            bag = bag.AddCopies(value, (value % 4) + 1);

        Assert.Equal(bag.ToArray(), bag.ToArray());
        Assert.Equal(bag.DistinctItems.ToArray(), bag.DistinctItems.ToArray());
        Assert.Equal(bag.Entries.ToArray(), bag.Entries.ToArray());
        bag.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies concrete expanded traversal performs no managed allocations after warmup.</summary>
    [Fact]
    public void ConcreteEnumeration_AllocatesNothingAfterWarmup()
    {
        var bag = PersistentHashBag<int>.Empty;
        for (var value = 0; value < 64; value++)
            bag = bag.AddCopies(value, (value % 5) + 1);

        var expected = DrainWithoutAllocation(bag);
        var before = GC.GetAllocatedBytesForCurrentThread();
        var actual = DrainWithoutAllocation(bag);
        var allocated = GC.GetAllocatedBytesForCurrentThread() - before;

        Assert.Equal(expected, actual);
        Assert.Equal(0, allocated);
    }

    private static int[] DrainIncludingCurrent(ref PersistentHashBag<int>.Enumerator enumerator)
    {
        var result = new List<int> { enumerator.Current };
        while (enumerator.MoveNext())
            result.Add(enumerator.Current);
        return result.ToArray();
    }

    private static int DrainWithoutAllocation(PersistentHashBag<int> bag)
    {
        var checksum = 0;
        var enumerator = bag.GetEnumerator();
        while (enumerator.MoveNext())
            checksum = unchecked((checksum * 31) + enumerator.Current);
        return checksum;
    }

    private static string NewString(string value) => new(value.ToCharArray());

    private sealed class FewBucketsComparer : IEqualityComparer<int>
    {
        public bool Equals(int left, int right) => left == right;

        public int GetHashCode(int value) => value & 3;
    }
}
