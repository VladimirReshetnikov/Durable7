using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Contract coverage for ordered, augmented-search, and sparse set-bit cursors.</summary>
public sealed class OrderedSearchCursorTests
{
    /// <summary>Verifies stable duplicate placement and deletion by exact occurrence rank.</summary>
    [Fact]
    public void SortedBagCursor_PreservesDuplicateOccurrenceIdentity()
    {
        var first = new RankedItem(2, "first");
        var second = new RankedItem(2, "second");
        var third = new RankedItem(2, "third");
        var bag = SortedBag<RankedItem>.CreateRange(
            [new(1, "low"), first, second, third, new(3, "high")],
            RankedItemComparer.Instance);

        var lower = bag.GetCursorAtLowerBound(new(2, "probe"));
        var upper = bag.GetCursorAtUpperBound(new(2, "probe"));
        Assert.Equal(1, lower.Position);
        Assert.Equal(4, upper.Position);
        Assert.True(bag.TryGetCursor(new(2, "probe"), out var exact));
        Assert.Same(first, PeekNext(exact));

        var deleted = bag.GetCursor(2).DeleteNext();
        Assert.Equal(["low", "first", "third", "high"], deleted.Snapshot().Select(x => x.Name));
        Assert.Same(second, bag[2]);
        Assert.Equal(2, deleted.Position);

        var added = lower.Add(new(2, "fourth"));
        Assert.Equal(5, added.Position);
        Assert.Equal(["first", "second", "third", "fourth"],
            added.Snapshot().Where(x => x.Rank == 2).Select(x => x.Name));
    }

    /// <summary>Verifies set bounds, representative retention, edits, and usable exact misses.</summary>
    [Fact]
    public void SortedSetCursor_PreservesComparerRepresentative()
    {
        var set = SortedSet<string>.Create(StringComparer.OrdinalIgnoreCase).Add("a").Add("c");
        Assert.False(set.TryGetCursor("b", out var miss));
        Assert.Equal(1, miss.Position);

        var present = miss.Add("A");
        Assert.Same(set, present.Snapshot());
        Assert.Equal("a", PeekPrevious(present));

        var inserted = miss.Add("b");
        Assert.Equal(2, inserted.Position);
        Assert.Equal(["a", "b", "c"], inserted.Snapshot());
        Assert.Equal(["a", "c"], inserted.DeletePrevious().Snapshot());
    }

    /// <summary>Verifies strict insert, focused value replacement, and exact-key deletion.</summary>
    [Fact]
    public void SortedDictionaryCursor_UsesKeyOrderedGapSemantics()
    {
        var map = SortedDictionary<int, string>.Empty.SetItem(1, "one").SetItem(3, "three");
        var cursor = map.GetCursorAtLowerBound(2);
        Assert.False(map.TryGetCursor(2, out var miss));
        Assert.Equal(cursor.Position, miss.Position);

        var inserted = cursor.Insert(2, "two");
        Assert.Equal(2, inserted.Position);
        var focused = inserted.MovePrevious().SetNextValue("TWO");
        Assert.Equal(1, focused.Position);
        Assert.Equal("TWO", focused.Snapshot()[2]);
        Assert.Equal([1, 3], focused.DeleteNext().Snapshot().Keys);
        Assert.False(cursor.TryInsert(1, "duplicate", out var rejected));
        Assert.Same(map, rejected.Snapshot());
    }

    /// <summary>Verifies canonical edits produce the same topology as ordinary operations.</summary>
    [Fact]
    public void CanonicalSortedSetCursor_DelegatesCanonicalTopology()
    {
        var policy = ZipTreeRankPolicy<int>.Create(seed: 42);
        var set = CanonicalSortedSet<int>.CreateRange([1, 4, 7], policy);
        var edited = set.GetCursorAtLowerBound(4).Add(3).DeletePrevious();
        var expected = set.Add(3).Remove(3);

        Assert.True(expected.SetEquals(edited.Snapshot()));
        Assert.Equal(expected.ShapeForTesting(), edited.Snapshot().ShapeForTesting());
        Assert.Same(policy, edited.Snapshot().Policy);
        edited.Snapshot().ValidateStructure();
    }

    /// <summary>Verifies key navigation and winner-to-key cursor projection.</summary>
    [Fact]
    public void PrioritySearchQueueCursor_RepairsWinnerAugmentation()
    {
        var queue = PrioritySearchQueue<int, int, string>.Empty
            .SetItem(1, 30, "one")
            .SetItem(2, 10, "two")
            .SetItem(3, 20, "three");
        var winner = queue.GetCursorAtMinimumPriority();
        Assert.Equal(1, winner.Position);
        Assert.Equal(2, PeekNext(winner).Key);

        var updated = winner.SetNext(40, "TWO");
        Assert.Equal(3, updated.Snapshot().Minimum.Key);
        Assert.Equal(1, updated.Position);
        updated.Snapshot().ValidateStructure();
        Assert.Equal([1, 3], updated.DeleteNext().Snapshot().Select(entry => entry.Key));
    }

    /// <summary>Verifies equal-low occurrence identity and exclusive overlap continuation.</summary>
    [Fact]
    public void IntervalTreeCursor_DeletesExactOccurrenceAndContinuesOverlap()
    {
        var firstLow = new Endpoint(1, "first-low");
        var firstHigh = new Endpoint(5, "first-high");
        var secondLow = new Endpoint(1, "second-low");
        var secondHigh = new Endpoint(5, "second-high");
        var tree = IntervalTree<Endpoint>.Empty
            .Insert(new(firstLow, firstHigh))
            .Insert(new(new(3, "middle-low"), new(8, "middle-high")))
            .Insert(new(secondLow, secondHigh));

        var last = tree.GetCursor(1);
        Assert.Same(firstLow, PeekNext(last).Low);
        var deleted = last.DeleteNext();
        Assert.Equal(2, deleted.Count);
        Assert.Same(secondLow, deleted.Snapshot().ToArray()[0].Low);

        Assert.True(tree.TryGetOverlapCursor(
            new(new(4, "query-low"), new(4, "query-high")), out var overlap));
        var firstPosition = overlap.Position;
        Assert.True(overlap.TrySeekNextOverlap(
            new(new(4, "query-low"), new(4, "query-high")), out var next));
        Assert.True(next.Position > firstPosition);
    }

    /// <summary>Verifies lexicographic interval keys, payload no-op policy, and overlap continuation.</summary>
    [Fact]
    public void PersistentIntervalMapCursor_PublishesExactAndAugmentedStateTogether()
    {
        var map = PersistentIntervalMap<int, string>.Empty
            .SetItem(new(1, 4), "a")
            .SetItem(new(3, 7), "b")
            .SetItem(new(9, 10), "c");
        Assert.True(map.TryGetCursor(new(3, 7), out var exact));
        var updated = exact.SetNextValue("B");
        Assert.Equal("B", updated.Snapshot()[new(3, 7)]);
        updated.Snapshot().ValidateInvariants();

        Assert.True(updated.Snapshot().TryGetOverlapCursor(new(4, 9), out var overlap));
        Assert.True(overlap.TrySeekNextOverlap(new(4, 9), out var next));
        Assert.True(next.Position > overlap.Position);
        Assert.Equal(2, next.DeleteNext().Snapshot().Count);
    }

    /// <summary>Verifies wide population ranks, at-or-after misses, and word-removing deletes.</summary>
    [Fact]
    public void ChunkedBitSetCursor_TraversesOnlyPresentBits()
    {
        var bits = PersistentChunkedBitSet.CreateRange([1, 63, 64, 130]);
        var cursor = bits.GetCursorAtOrAfter(62);
        Assert.Equal(1, cursor.Position);
        Assert.Equal(63, PeekNext(cursor));
        Assert.False(bits.TryGetCursor(62, out var miss));
        Assert.Equal(cursor.Position, miss.Position);

        var added = cursor.Add(62);
        Assert.Equal(2, added.Position);
        Assert.Equal([1, 62, 63, 64, 130], added.Snapshot().ToArray());
        Assert.Equal([1, 63, 64, 130], added.DeletePrevious().Snapshot().ToArray());
        Assert.Same(bits, cursor.Add(63).Snapshot());
        Assert.Throws<InvalidOperationException>(() => default(PersistentChunkedBitSetCursor).Snapshot());
    }

    private static T PeekNext<T>(SortedBagCursor<T> cursor)
    {
        Assert.True(cursor.TryPeekNext(out var item));
        return item;
    }

    private static T PeekPrevious<T>(SortedSetCursor<T> cursor)
    {
        Assert.True(cursor.TryPeekPrevious(out var item));
        return item;
    }

    private static PrioritySearchEntry<TKey, TPriority, TValue> PeekNext<TKey, TPriority, TValue>(
        PrioritySearchQueueCursor<TKey, TPriority, TValue> cursor)
    {
        Assert.True(cursor.TryPeekNext(out var entry));
        return entry;
    }

    private static Interval<T> PeekNext<T>(IntervalTreeCursor<T> cursor)
    {
        Assert.True(cursor.TryPeekNext(out var interval));
        return interval;
    }

    private static int PeekNext(PersistentChunkedBitSetCursor cursor)
    {
        Assert.True(cursor.TryPeekNext(out var bitIndex));
        return bitIndex;
    }

    private sealed record RankedItem(int Rank, string Name);

    private sealed class RankedItemComparer : IComparer<RankedItem>
    {
        internal static RankedItemComparer Instance { get; } = new();
        public int Compare(RankedItem? x, RankedItem? y) => x!.Rank.CompareTo(y!.Rank);
    }

    private sealed record Endpoint(int Order, string Name) : IComparable<Endpoint>
    {
        public int CompareTo(Endpoint? other) => Order.CompareTo(other!.Order);
    }
}
