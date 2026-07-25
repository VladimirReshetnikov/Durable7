using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Verifies the persistent interval map and its measured overlap index.</summary>
public sealed class PersistentIntervalMapTests
{
    /// <summary>Verifies empty, insertion, lookup, strict-add, and retained-version behavior.</summary>
    [Fact]
    public void EmptyAndPointUpdates_PreserveMapSemantics()
    {
        var empty = PersistentIntervalMap<int, string>.Empty;
        var one = empty.Add(new(4, 9), "first");
        var two = one.SetItem(new(1, 2), "early");

        Assert.True(empty.IsEmpty);
        Assert.Equal(2, two.Count);
        Assert.Equal("first", two[new(4, 9)]);
        Assert.Equal(new[] { new Interval<int>(1, 2), new Interval<int>(4, 9) }, two.Keys);
        Assert.Throws<ArgumentException>(() => two.Add(new(4, 9), "duplicate"));
        Assert.False(two.TryAdd(new(4, 9), "duplicate", out var unchanged));
        Assert.Same(two, unchanged);
        Assert.False(empty.ContainsKey(new(4, 9)));
        Assert.True(one.ContainsKey(new(4, 9)));
    }

    /// <summary>Verifies configured value equality and first interval representatives.</summary>
    [Fact]
    public void SetItem_UsesValueComparerAndRetainsIntervalRepresentative()
    {
        var map = PersistentIntervalMap<OrderedEndpoint, string>
            .Create(StringComparer.OrdinalIgnoreCase)
            .SetItem(new(new(2, "stored-low"), new(5, "stored-high")), "Alpha");

        var equivalent = new Interval<OrderedEndpoint>(new(2, "query-low"), new(5, "query-high"));
        Assert.Same(map, map.SetItem(equivalent, "ALPHA"));
        var changed = map.SetItem(equivalent, "beta");
        Assert.Equal("beta", changed[equivalent]);
        Assert.True(changed.TryGetKey(equivalent, out var actual));
        Assert.Equal("stored-low", actual.Low.Representation);
        Assert.Equal("stored-high", actual.High.Representation);
    }

    /// <summary>Verifies range construction replaces values without replacing interval keys.</summary>
    [Fact]
    public void CreateRange_IsLastDistinctValueWinsAndKeepsFirstKeyRepresentative()
    {
        var first = new Interval<OrderedEndpoint>(new(1, "first-low"), new(3, "first-high"));
        var later = new Interval<OrderedEndpoint>(new(1, "later-low"), new(3, "later-high"));
        var map = PersistentIntervalMap<OrderedEndpoint, string>.CreateRange(
            new[]
            {
                KeyValuePair.Create(first, "one"),
                KeyValuePair.Create(later, "two"),
            });

        Assert.Single(map);
        Assert.Equal("two", map[later]);
        Assert.True(map.TryGetKey(later, out var actual));
        Assert.Equal("first-low", actual.Low.Representation);
    }

    /// <summary>Verifies overlap search, enumeration, and counting against a linear model.</summary>
    [Fact]
    public void OverlapQueries_MatchBruteForceModel()
    {
        var random = new Random(723);
        var entries = Enumerable.Range(0, 180)
            .Select(index =>
            {
                var low = random.Next(0, 500);
                return KeyValuePair.Create(new Interval<int>(low, low + random.Next(0, 40)), index);
            })
            .GroupBy(pair => pair.Key)
            .Select(group => group.Last())
            .ToArray();
        var map = PersistentIntervalMap<int, int>.CreateRange(entries);

        for (var index = 0; index < 250; index++)
        {
            var low = random.Next(-10, 520);
            var query = new Interval<int>(low, low + random.Next(0, 40));
            var expected = entries
                .Where(pair => pair.Key.Overlaps(query))
                .OrderBy(pair => pair.Key.Low)
                .ThenBy(pair => pair.Key.High)
                .ToArray();

            Assert.Equal(expected, map.FindOverlaps(query));
            Assert.Equal(expected.Length, map.CountOverlaps(query));
            Assert.Equal(expected.Length != 0, map.TryFindOverlap(query, out var hit));
            if (expected.Length != 0)
                Assert.Contains(hit, expected);
        }
    }

    /// <summary>Verifies point stabbing returns a genuinely containing interval.</summary>
    [Fact]
    public void PointStabbing_ReturnsContainingEntry()
    {
        var map = PersistentIntervalMap<int, string>.Empty
            .Add(new(1, 5), "a")
            .Add(new(3, 8), "b")
            .Add(new(10, 12), "c");

        Assert.True(map.TryFindContaining(4, out var hit));
        Assert.True(hit.Key.Contains(4));
        Assert.False(map.TryFindContaining(9, out _));
    }

    /// <summary>Verifies lexicographic exact-key order while overlapping keys remain distinct.</summary>
    [Fact]
    public void ExactOrdering_IsLexicographicAndAllowsOverlaps()
    {
        var map = PersistentIntervalMap<int, string>.Empty
            .Add(new(5, 10), "wide")
            .Add(new(5, 7), "short")
            .Add(new(1, 100), "cover")
            .Add(new(6, 6), "point");

        Assert.Equal(
            new[] { new Interval<int>(1, 100), new(5, 7), new(5, 10), new(6, 6) },
            map.Keys);
        Assert.Equal(4, map.CountOverlaps(new(6, 6)));
        map.ValidateInvariants();
    }

    /// <summary>Verifies removal, policy-preserving clear, and source-version retention.</summary>
    [Fact]
    public void RemoveAndClear_PreserveEarlierVersionsAndPolicy()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var baseline = PersistentIntervalMap<int, string>.Create(comparer)
            .Add(new(1, 3), "one")
            .Add(new(4, 7), "two");

        Assert.True(baseline.TryRemove(new(1, 3), out var removed, out var value));
        Assert.Equal("one", value);
        Assert.Single(removed);
        Assert.Equal(2, baseline.Count);
        Assert.False(removed.TryRemove(new(1, 3), out var unchanged, out _));
        Assert.Same(removed, unchanged);
        Assert.Same(comparer, removed.Clear().ValueComparer);
    }

    /// <summary>Verifies reversed endpoints are rejected consistently before publication.</summary>
    [Fact]
    public void InvalidIntervals_AreRejectedAcrossMutationAndQuerySurfaces()
    {
        var invalid = new Interval<int>(8, 3);
        var map = PersistentIntervalMap<int, string>.Empty;

        Assert.Throws<ArgumentException>(() => map.Add(invalid, "x"));
        Assert.Throws<ArgumentException>(() => map.SetItem(invalid, "x"));
        Assert.Throws<ArgumentException>(() => map.ContainsKey(invalid));
        Assert.Throws<ArgumentException>(() => map.FindOverlaps(invalid));
        Assert.Throws<ArgumentException>(() => PersistentIntervalMap<int, string>.CreateRange(
            new[] { KeyValuePair.Create(invalid, "x") }));
    }

    /// <summary>Verifies independently retained branches and their annotations remain valid.</summary>
    [Fact]
    public void RetainedBranchingHistories_RemainIndependent()
    {
        var root = PersistentIntervalMap<int, int>.Empty
            .Add(new(0, 5), 1)
            .Add(new(10, 15), 2);
        var left = root.SetItem(new(0, 5), 10).Add(new(4, 11), 3);
        var right = root.Remove(new(10, 15)).Add(new(20, 25), 4);

        Assert.Equal(new[] { 1, 2 }, root.Values);
        Assert.Equal(new[] { 10, 3, 2 }, left.Values);
        Assert.Equal(new[] { 1, 4 }, right.Values);
        root.ValidateInvariants();
        left.ValidateInvariants();
        right.ValidateInvariants();
    }

    private readonly record struct OrderedEndpoint(int Order, string Representation) :
        IComparable<OrderedEndpoint>
    {
        public int CompareTo(OrderedEndpoint other) => Order.CompareTo(other.Order);
    }
}
