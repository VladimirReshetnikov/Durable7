using System.Collections;
using Xunit;
using IntSet = Durable7.Hamt.PersistentHashSet<int>;
using StringSet = Durable7.Hamt.PersistentHashSet<string>;

namespace Durable7.Hamt.Tests;

/// <summary>Direct state-machine tests for the set wrapper's allocation-free struct enumerator.</summary>
public sealed class PersistentHashSetEnumeratorTests
{
    /// <summary>Verifies a default-initialized wrapper enumerator is safely and permanently exhausted.</summary>
    [Fact]
    public void DefaultEnumerator_ReportsNoItems()
    {
        var enumerator = default(StringSet.Enumerator);

        Assert.Null(enumerator.Current);
        Assert.Null(((IEnumerator)enumerator).Current);
        Assert.False(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        Assert.Null(enumerator.Current);
    }

    /// <summary>Verifies <c>Current</c> is default before the first item and after exhaustion.</summary>
    [Fact]
    public void MoveNext_BracketsItemsWithDefaultCurrent()
    {
        var enumerator = IntSet.Empty.Add(7).GetEnumerator();

        Assert.Equal(0, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(7, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.Equal(0, enumerator.Current);
        Assert.False(enumerator.MoveNext());
    }

    /// <summary>Verifies copying the set wrapper enumerator copies independent traversal state.</summary>
    [Fact]
    public void CopiedEnumerator_AdvancesIndependently()
    {
        var set = IntSet.CreateRange([0, 1, 2, 31, 32, 33, 1024]);
        var expected = set.ToArray();
        var original = set.GetEnumerator();
        Assert.True(original.MoveNext());
        var copy = original;

        var fromOriginal = DrainIncludingCurrent(ref original);
        var fromCopy = DrainIncludingCurrent(ref copy);

        Assert.Equal(expected, fromOriginal);
        Assert.Equal(expected, fromCopy);
    }

    /// <summary>Verifies interface enumeration delegates state correctly and rejects <c>Reset</c>.</summary>
    [Fact]
    public void InterfaceEnumerator_EnumeratesAndRejectsReset()
    {
        var set = IntSet.CreateRange([1, 2, 3]);
        using var enumerator = ((IEnumerable<int>)set).GetEnumerator();

        Assert.Throws<NotSupportedException>(enumerator.Reset);
        var seen = new List<int>();
        while (enumerator.MoveNext())
            seen.Add(enumerator.Current);
        Assert.Equal(set.OrderBy(value => value), seen.OrderBy(value => value));
        Assert.Throws<NotSupportedException>(enumerator.Reset);
    }

    private static int[] DrainIncludingCurrent(ref IntSet.Enumerator enumerator)
    {
        var result = new List<int> { enumerator.Current };
        while (enumerator.MoveNext())
            result.Add(enumerator.Current);
        return result.ToArray();
    }
}
