using System.Reflection;
using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>Movement, relabel, slicing, reversal, and stable one-shot sorting tests.</summary>
public sealed class PersistentOrderedSetMovementRangeAndSortTests
{
    /// <summary>Exhaustively verifies movement indexes denote final result positions.</summary>
    [Fact]
    public void MoveTo_AllSmallSourceAndDestinationPairsUseFinalIndexes()
    {
        for (var size = 1; size <= 9; size++)
        {
            var source = PersistentOrderedSet<int>.CreateRange(Enumerable.Range(0, size));
            for (var oldIndex = 0; oldIndex < size; oldIndex++)
            {
                for (var finalIndex = 0; finalIndex < size; finalIndex++)
                {
                    var expected = Enumerable.Range(0, size).ToList();
                    var item = expected[oldIndex];
                    expected.RemoveAt(oldIndex);
                    expected.Insert(finalIndex, item);

                    var actual = source.MoveTo(finalIndex, item);
                    OrderedSetAssert.Matches(expected, actual);
                    if (oldIndex == finalIndex)
                        Assert.Same(source, actual);
                    else
                        Assert.NotSame(source, actual);
                }
            }
            OrderedSetAssert.Matches(Enumerable.Range(0, size).ToArray(), source);
        }
    }

    /// <summary>Verifies explicit end movement retains the first stored representative.</summary>
    [Fact]
    public void MoveToEnds_RetainsRepresentativeAndHasIdentityFastPaths()
    {
        var comparer = new RepresentativeComparer();
        var first = new Representative(1, "first");
        var middle = new Representative(2, "middle");
        var last = new Representative(3, "last");
        var source = PersistentOrderedSet<Representative>.CreateRange([first, middle, last], comparer);

        Assert.Same(source, source.MoveToFirst(new Representative(1, "lookup-first")));
        Assert.Same(source, source.MoveToLast(new Representative(3, "lookup-last")));
        var toFirst = source.MoveToFirst(new Representative(2, "lookup-middle"));
        var toLast = source.MoveToLast(new Representative(2, "lookup-middle"));
        OrderedSetAssert.Matches(new[] { middle, first, last }, toFirst);
        OrderedSetAssert.Matches(new[] { first, last, middle }, toLast);
        Assert.Same(middle, toFirst.First);
        Assert.Same(middle, toLast.Last);
        OrderedSetAssert.Matches(new[] { first, middle, last }, source);
    }

    /// <summary>Verifies insertion- and movement-triggered relabels preserve snapshots and failure atomicity.</summary>
    [Fact]
    public void RelabelFallbacks_PreserveOrderRepresentativesAndFailureAtomicity()
    {
        var comparer = new RepresentativeComparer();
        var start = new Representative(-1, "start");
        var end = new Representative(-2, "end");
        var inserted = Enumerable.Range(0, 160)
            .Select(index => new Representative(index, $"inserted-{index}"))
            .ToArray();
        var set = PersistentOrderedSet<Representative>.CreateRange([start, end], comparer);

        foreach (var item in inserted)
        {
            set = set.Insert(1, item);
            set.ValidateInvariants();
        }

        var expected = new[] { start }.Concat(inserted.Reverse()).Append(end).ToArray();
        OrderedSetAssert.Matches(expected, set);
        for (var index = 0; index < inserted.Length; index++)
        {
            var item = inserted[index];
            Assert.Equal(1 + (inserted.Length - 1 - index), set.IndexOf(new Representative(item.EquivalenceClass, "probe")));
            Assert.True(set.TryGetValue(new Representative(item.EquivalenceClass, "probe"), out var stored));
            Assert.Same(item, stored);
        }

        var moveComparer = new SwitchableRepresentativeComparer(hashBuckets: 4096);
        var mover = new Representative(1000, "mover");
        var left = new Representative(1001, "left");
        var right = new Representative(1002, "right");
        var moveSource = PersistentOrderedSet<Representative>.CreateRange([mover, left, right], moveComparer);
        var nextClass = 1100;
        while (PrivateStampGapAt(moveSource, 2) != 1)
        {
            Assert.True(nextClass < 1356, "The private sparse-label policy did not expose an exhausted gap.");
            moveSource = moveSource.Insert(2, new Representative(nextClass, $"gap-{nextClass}"));
            nextClass++;
        }

        var moveSourceExpected = moveSource.ToArray();
        var lookup = new Representative(mover.EquivalenceClass, "lookup");
        moveComparer.ResetCounts();
        moveComparer.ThrowOnHashCall = 2;
        var rebuildFailure = Assert.Throws<ComparerCallbackException>(() => moveSource.MoveTo(1, lookup));
        Assert.Same(moveComparer.Failure, rebuildFailure);
        Assert.Equal(1, moveComparer.HashCalls);
        moveComparer.ThrowOnHashCall = null;
        OrderedSetAssert.Matches(moveSourceExpected, moveSource);

        var moved = moveSource.MoveTo(1, lookup);
        var movedExpected = moveSourceExpected.Skip(1).ToList();
        movedExpected.Insert(1, mover);
        OrderedSetAssert.Matches(movedExpected, moved);
        Assert.Same(mover, moved[1]);
        OrderedSetAssert.Matches(moveSourceExpected, moveSource);
    }

    /// <summary>Verifies sibling histories repay relabel work independently without altering their branch source.</summary>
    [Fact]
    public void RelabelBoundaryBranches_RetainTheirSharedSourceSnapshot()
    {
        var source = PersistentOrderedSet<int>.CreateRange([-2, -1]);
        for (var value = 0; value < 70; value++)
            source = source.Insert(1, value);
        var sourceExpected = source.ToArray();

        var left = source;
        var right = source;
        for (var value = 1000; value < 1080; value++)
            left = left.Insert(1, value);
        for (var value = 2000; value < 2080; value++)
            right = right.Insert(1, value);

        OrderedSetAssert.Matches(sourceExpected, source);
        Assert.Equal(152, left.Count);
        Assert.Equal(152, right.Count);
        Assert.Equal(1079, left[1]);
        Assert.Equal(2079, right[1]);
        Assert.False(left.Contains(2079));
        Assert.False(right.Contains(1079));
        left.ValidateInvariants();
        right.ValidateInvariants();
    }

    /// <summary>Checks every valid small contiguous range and both comparer-aware index reconciliation paths.</summary>
    [Fact]
    public void GetRange_AllSmallBoundariesMatchTheOrderedModel()
    {
        var comparer = new RepresentativeComparer();
        var items = Enumerable.Range(0, 12)
            .Select(index => new Representative(index, $"item-{index}"))
            .ToArray();
        var source = PersistentOrderedSet<Representative>.CreateRange(items, comparer);

        for (var index = 0; index <= source.Count; index++)
        {
            for (var count = 0; count <= source.Count - index; count++)
            {
                var actual = source.GetRange(index, count);
                OrderedSetAssert.Matches(items.Skip(index).Take(count).ToArray(), actual);
                Assert.Same(comparer, actual.Comparer);
                if (index == 0 && count == source.Count)
                    Assert.Same(source, actual);
            }
        }

        OrderedSetAssert.Matches(items, source.GetRange(0, source.Count));
        OrderedSetAssert.Matches([items[6]], source.GetRange(6, 1));
        OrderedSetAssert.Matches(items.Skip(1).Take(10).ToArray(), source.GetRange(1, 10));
        OrderedSetAssert.Matches(items, source);
    }

    /// <summary>Locks Take/Drop boundary identity and comparer-preserving empty results.</summary>
    [Fact]
    public void TakeAndDrop_HaveTheSpecifiedBoundaryIdentities()
    {
        var comparer = new RepresentativeComparer();
        var items = Enumerable.Range(0, 5).Select(i => new Representative(i, $"item-{i}")).ToArray();
        var source = PersistentOrderedSet<Representative>.CreateRange(items, comparer);

        Assert.Same(source, source.Take(source.Count));
        Assert.Same(source, source.Drop(0));
        OrderedSetAssert.Matches(items.Take(3).ToArray(), source.Take(3));
        OrderedSetAssert.Matches(items.Skip(2).ToArray(), source.Drop(2));

        var takeEmpty = source.Take(0);
        var dropEmpty = source.Drop(source.Count);
        Assert.True(takeEmpty.IsEmpty);
        Assert.True(dropEmpty.IsEmpty);
        Assert.Same(comparer, takeEmpty.Comparer);
        Assert.Same(comparer, dropEmpty.Comparer);

        var empty = PersistentOrderedSet<Representative>.Create(comparer);
        Assert.Same(empty, empty.GetRange(0, 0));
        Assert.Same(empty, empty.Take(0));
        Assert.Same(empty, empty.Drop(0));
    }

    /// <summary>Verifies range validation is eager and overflow-safe.</summary>
    [Fact]
    public void RangeValidation_RejectsEveryInvalidBoundaryWithoutAdditionOverflow()
    {
        var source = PersistentOrderedSet<int>.CreateRange([0, 1, 2]);
        Assert.Throws<ArgumentOutOfRangeException>("index", () => source.GetRange(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => source.GetRange(4, 0));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.GetRange(0, -1));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.GetRange(1, 3));
        Assert.Throws<ArgumentOutOfRangeException>("index", () => source.GetRange(int.MaxValue, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.GetRange(1, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.Take(-1));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.Take(4));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.Drop(-1));
        Assert.Throws<ArgumentOutOfRangeException>("count", () => source.Drop(4));
        OrderedSetAssert.Matches(new[] { 0, 1, 2 }, source);
    }

    /// <summary>Verifies reversal rebuilds order while trivial cases preserve identity.</summary>
    [Fact]
    public void Reverse_RebuildsNontrivialOrderAndPreservesRepresentatives()
    {
        Assert.Same(PersistentOrderedSet<int>.Empty, PersistentOrderedSet<int>.Empty.Reverse());
        var single = PersistentOrderedSet<int>.Empty.Add(1);
        Assert.Same(single, single.Reverse());

        var comparer = new RepresentativeComparer();
        var items = Enumerable.Range(0, 8).Select(i => new Representative(i, $"item-{i}")).ToArray();
        var source = PersistentOrderedSet<Representative>.CreateRange(items, comparer);
        var reversed = source.Reverse();
        OrderedSetAssert.Matches(items.Reverse().ToArray(), reversed);
        Assert.Same(comparer, reversed.Comparer);
        OrderedSetAssert.Matches(items, source);
    }

    /// <summary>Verifies Sort is stable, identity-aware, comparer-preserving, and one-shot.</summary>
    [Fact]
    public void Sort_IsStableAndTheResultRemainsAnOrdinaryOrderedSet()
    {
        var equalityComparer = new RepresentativeComparer();
        var items = new[]
        {
            new Representative(4, "four"),
            new Representative(1, "one"),
            new Representative(7, "seven"),
            new Representative(2, "two"),
            new Representative(5, "five"),
        };
        var source = PersistentOrderedSet<Representative>.CreateRange(items, equalityComparer);
        var byRemainder = Comparer<Representative>.Create(
            (left, right) => (left.EquivalenceClass % 3).CompareTo(right.EquivalenceClass % 3));
        var sorted = source.Sort(byRemainder);
        var expected = items.OrderBy(item => item.EquivalenceClass % 3).ToArray();

        OrderedSetAssert.Matches(expected, sorted);
        Assert.Same(equalityComparer, sorted.Comparer);
        Assert.Same(sorted, sorted.Sort(byRemainder));
        var appended = new Representative(10, "later");
        OrderedSetAssert.Matches(expected.Append(appended).ToArray(), sorted.Add(appended));
        OrderedSetAssert.Matches(items, source);

        var allTies = Comparer<Representative>.Create((_, _) => 0);
        Assert.Same(source, source.Sort(allTies));
        Assert.Same(PersistentOrderedSet<Representative>.Empty, PersistentOrderedSet<Representative>.Empty.Sort(allTies));
        var single = PersistentOrderedSet<Representative>.Create(equalityComparer).Add(items[0]);
        Assert.Same(single, single.Sort(allTies));

        var defaultSource = PersistentOrderedSet<int>.CreateRange([3, 1, 4, 2]);
        var defaultSorted = defaultSource.Sort();
        OrderedSetAssert.Matches(new[] { 1, 2, 3, 4 }, defaultSorted);
        Assert.Same(defaultSorted, defaultSorted.Sort());
        OrderedSetAssert.Matches(new[] { 3, 1, 4, 2 }, defaultSource);
    }

    private static ulong PrivateStampGapAt<T>(PersistentOrderedSet<T> set, int rightIndex)
    {
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic;
        var order = (System.Collections.IEnumerable)typeof(PersistentOrderedSet<T>)
            .GetField("_order", flags)!
            .GetValue(set)!;
        var stamps = order.Cast<object>()
            .Select(entry => (long)entry.GetType().GetProperty("Stamp")!.GetValue(entry)!)
            .ToArray();
        return unchecked((ulong)(stamps[rightIndex] - stamps[rightIndex - 1]));
    }
}
