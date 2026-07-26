// Tests for the comparer equivalent facade.

using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Covers facade behavior when ordering equality is deliberately coarser than object equality, plus
/// the interval tree's equal-low run behavior under a large duplicate-heavy workload.
/// </summary>
public sealed class ComparerEquivalentFacadeTests
{
    private static readonly IComparer<Key> ByOrder =
        Comparer<Key>.Create((left, right) => left.Order.CompareTo(right.Order));

    /// <summary>
    /// Verifies a sorted bag keeps every comparer-equal object in stable insertion order and that
    /// comparer-based queries and removals do not accidentally fall back to object equality.
    /// </summary>
    [Fact]
    public void SortedBag_ComparerEquivalentObjects_RemainStableAndSearchable()
    {
        var first = new Key(1, "first");
        var second = new Key(1, "second");
        var probe = new Key(1, "probe");
        var bag = SortedBag<Key>.Create(ByOrder).Add(first).Add(second);

        var items = bag.ToArray();
        Assert.Same(first, items[0]);
        Assert.Same(second, items[1]);
        Assert.True(bag.Contains(probe));
        Assert.Equal(2, bag.CountOf(probe));
        Assert.Equal(items, bag.GetRange(probe, probe).ToArray());

        Assert.True(bag.TryRemove(probe, out var removedFirst));
        Assert.Same(second, Assert.Single(removedFirst));
        Assert.Empty(bag.RemoveAll(probe));
    }

    /// <summary>
    /// Verifies a sorted set retains the first comparer-equivalent object and returns that canonical
    /// instance from exact navigable queries; adding another equivalent object is a true no-op.
    /// </summary>
    [Fact]
    public void SortedSet_ComparerEquivalentObjects_RetainFirstCanonicalInstance()
    {
        var first = new Key(1, "first");
        var duplicate = new Key(1, "duplicate");
        var second = new Key(2, "second");
        var probe = new Key(1, "probe");
        var set = SortedSet<Key>.CreateRange([second, first, duplicate], ByOrder);

        Assert.Equal(2, set.Count);
        Assert.Same(first, set[0]);
        Assert.Same(second, set[1]);
        Assert.True(set.Contains(probe));
        Assert.Equal(0, set.IndexOf(probe));
        Assert.Same(set, set.Add(probe));

        Assert.True(set.TryFloor(probe, out var floor));
        Assert.Same(first, floor);
        Assert.True(set.TryCeiling(probe, out var ceiling));
        Assert.Same(first, ceiling);

        Assert.True(set.TryRemove(probe, out var removed));
        Assert.Equal([second], removed.ToArray());
    }

    /// <summary>
    /// Verifies dictionary lookup is comparer-based while replacement installs the supplied key
    /// instance, matching the documented sorted-dictionary (rather than HAMT) canonicalization rule.
    /// </summary>
    [Fact]
    public void SortedDictionary_ComparerEquivalentReplacement_InstallsSuppliedKey()
    {
        var first = new Key(1, "first");
        var lastFromRange = new Key(1, "last-from-range");
        var supplied = new Key(1, "supplied");
        var probe = new Key(1, "probe");
        var map = SortedDictionary<Key, string>.CreateRange(
            [KeyValuePair.Create(first, "one"), KeyValuePair.Create(lastFromRange, "two")],
            ByOrder);

        var rangeEntry = Assert.Single(map);
        Assert.Same(lastFromRange, rangeEntry.Key);
        Assert.Equal("two", map[probe]);

        var replaced = map.SetItem(supplied, "three");
        var replacedEntry = Assert.Single(replaced);
        Assert.Same(supplied, replacedEntry.Key);
        Assert.Equal("three", replaced[probe]);
        Assert.Equal(0, replaced.IndexOfKey(probe));
        Assert.True(replaced.TryFloorEntry(probe, out var floor));
        Assert.Same(supplied, floor.Key);
        Assert.True(replaced.TryCeilingEntry(probe, out var ceiling));
        Assert.Same(supplied, ceiling.Key);
        Assert.Same(supplied, Assert.Single(replaced.GetRange(probe, probe)).Key);

        Assert.False(replaced.TryAdd(probe, "duplicate", out var unchanged));
        Assert.Same(replaced, unchanged);
        Assert.Throws<ArgumentException>("key", () => replaced.Add(probe, "duplicate"));
        Assert.True(replaced.TryRemove(probe, out var removed));
        Assert.True(removed.IsEmpty);
    }

    /// <summary>
    /// Stresses the interval tree's comparer-equal-low scan with many duplicate highs. Insertion must
    /// place a new equal-low interval at the front of its run, and each removal must delete the first
    /// matching interval in that association order without disturbing either neighboring low run.
    /// </summary>
    [Fact]
    public void IntervalTree_DuplicateLowRun_MatchesAssociationOrderUnderStress()
    {
        const int repeatedLow = 10;
        const int runLength = 768;
        var before = new Interval<int>(5, 6);
        var after = new Interval<int>(20, 21);
        var tree = IntervalTree<int>.Empty.Insert(before).Insert(after);
        var model = new List<Interval<int>> { before, after };

        for (var i = 0; i < runLength; i++)
        {
            var interval = new Interval<int>(repeatedLow, repeatedLow + (i % 64));
            tree = tree.Insert(interval);
            model.Insert(1, interval);
        }

        Assert.Equal(model, tree.ToArray());
        for (var highOffset = 0; highOffset < 64; highOffset++)
            Assert.True(tree.Contains(new Interval<int>(repeatedLow, repeatedLow + highOffset)));
        Assert.False(tree.Contains(new Interval<int>(repeatedLow, repeatedLow + 64)));

        var random = new Random(0x5eed);
        for (var remaining = runLength; remaining > 0; remaining--)
        {
            var selected = model[1 + random.Next(remaining)];
            var firstMatch = model.FindIndex(interval => interval == selected);

            Assert.True(tree.TryRemove(selected, out tree));
            model.RemoveAt(firstMatch);

            if ((remaining & 31) == 0)
                Assert.Equal(model, tree.ToArray());
        }

        Assert.Equal([before, after], tree.ToArray());
        Assert.False(tree.Contains(new Interval<int>(repeatedLow, repeatedLow)));
    }

    private sealed record Key(int Order, string Tag);
}
