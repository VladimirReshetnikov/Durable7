using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies the interval tree against a brute-force linear-scan model: insertion keeps low order, single
/// overlap queries agree on existence and return a genuine overlap, and full overlap enumeration matches the
/// model exactly.
/// </summary>
public sealed class IntervalTreeTests
{
    /// <summary>Verifies the interval primitive's containment and overlap predicates.</summary>
    [Fact]
    public void Interval_ContainsAndOverlaps_AreCorrect()
    {
        var interval = new Interval<int>(3, 8);

        Assert.True(interval.Contains(3));
        Assert.True(interval.Contains(8));
        Assert.True(interval.Contains(5));
        Assert.False(interval.Contains(2));
        Assert.False(interval.Contains(9));

        Assert.True(interval.Overlaps(new Interval<int>(8, 10)));   // touch at 8
        Assert.True(interval.Overlaps(new Interval<int>(0, 3)));    // touch at 3
        Assert.True(interval.Overlaps(new Interval<int>(4, 6)));    // contained
        Assert.True(interval.Overlaps(new Interval<int>(0, 100)));  // contains
        Assert.False(interval.Overlaps(new Interval<int>(9, 12)));
        Assert.False(interval.Overlaps(new Interval<int>(-2, 2)));
    }

    /// <summary>Verifies an empty tree answers queries negatively and reports zero count.</summary>
    [Fact]
    public void Empty_AnswersQueriesNegatively()
    {
        var empty = IntervalTree<int>.Empty;

        Assert.True(empty.IsEmpty);
        Assert.Equal(0, empty.Count);
        Assert.False(empty.TryFindOverlap(new Interval<int>(0, 10), out _));
        Assert.False(empty.TryFindContaining(5, out _));
        Assert.Empty(empty.FindOverlaps(new Interval<int>(0, 10)));
    }

    /// <summary>Verifies insertion keeps intervals ordered by low endpoint regardless of insertion order.</summary>
    [Fact]
    public void Insert_KeepsLowEndpointOrder()
    {
        var random = new Random(7);
        var tree = IntervalTree<int>.Empty;
        var model = new List<Interval<int>>();
        for (var i = 0; i < 200; i++)
        {
            var low = random.Next(0, 1000);
            var interval = new Interval<int>(low, low + random.Next(0, 50));
            tree = tree.Insert(interval);
            model.Add(interval);
        }

        Assert.Equal(model.Count, tree.Count);
        var lows = tree.ToArray().Select(iv => iv.Low).ToArray();
        var sortedLows = lows.OrderBy(v => v).ToArray();
        Assert.Equal(sortedLows, lows);
    }

    /// <summary>
    /// Verifies single-overlap queries against brute force: existence agrees, and any returned interval
    /// genuinely overlaps and is present in the tree.
    /// </summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    public void TryFindOverlap_AgreesWithBruteForce(int seed)
    {
        var random = new Random(seed);
        var intervals = new List<Interval<int>>();
        for (var i = 0; i < 150; i++)
        {
            var low = random.Next(0, 500);
            intervals.Add(new Interval<int>(low, low + random.Next(0, 30)));
        }

        var tree = IntervalTree<int>.CreateRange(intervals);

        for (var q = 0; q < 300; q++)
        {
            var low = random.Next(-10, 520);
            var query = new Interval<int>(low, low + random.Next(0, 30));

            var anyOverlap = intervals.Any(iv => iv.Overlaps(query));
            var found = tree.TryFindOverlap(query, out var match);

            Assert.Equal(anyOverlap, found);
            if (found)
            {
                Assert.True(match.Overlaps(query));
                Assert.Contains(match, intervals);
            }
        }
    }

    /// <summary>Verifies full overlap enumeration matches the brute-force set exactly, in low order.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(10)]
    [InlineData(11)]
    [InlineData(12)]
    public void FindOverlaps_MatchesBruteForce(int seed)
    {
        var random = new Random(seed);
        var intervals = new List<Interval<int>>();
        for (var i = 0; i < 120; i++)
        {
            var low = random.Next(0, 300);
            intervals.Add(new Interval<int>(low, low + random.Next(0, 40)));
        }

        var tree = IntervalTree<int>.CreateRange(intervals);

        for (var q = 0; q < 200; q++)
        {
            var low = random.Next(-10, 320);
            var query = new Interval<int>(low, low + random.Next(0, 40));

            var expected = intervals
                .Where(iv => iv.Overlaps(query))
                .OrderBy(iv => iv.Low)
                .ThenBy(iv => iv.High)
                .ToArray();

            var actual = tree.FindOverlaps(query)
                .OrderBy(iv => iv.Low)
                .ThenBy(iv => iv.High)
                .ToArray();

            Assert.Equal(expected, actual);

            // Results must come back already in nondecreasing low order from the tree.
            var raw = tree.FindOverlaps(query);
            for (var i = 1; i < raw.Count; i++)
                Assert.True(raw[i - 1].Low <= raw[i].Low);
        }
    }

    /// <summary>Verifies point-stabbing finds a containing interval exactly when one exists.</summary>
    [Fact]
    public void TryFindContaining_StabsPointsCorrectly()
    {
        var tree = IntervalTree<int>.Empty
            .Insert(1, 5)
            .Insert(10, 15)
            .Insert(3, 8)
            .Insert(20, 20);

        Assert.True(tree.TryFindContaining(4, out var a));
        Assert.True(a.Contains(4));
        Assert.True(tree.TryFindContaining(20, out var b));
        Assert.Equal(new Interval<int>(20, 20), b);
        Assert.False(tree.TryFindContaining(9, out _));
        Assert.False(tree.TryFindContaining(16, out _));
    }

    /// <summary>Verifies exact-membership and removal of value-equal intervals, including duplicates.</summary>
    [Fact]
    public void ContainsAndRemove_HandleValueEqualityAndDuplicates()
    {
        var tree = IntervalTree<int>.Empty
            .Insert(1, 5)
            .Insert(3, 8)
            .Insert(3, 8)   // duplicate
            .Insert(10, 15);

        Assert.Equal(4, tree.Count);
        Assert.True(tree.Contains(new Interval<int>(3, 8)));
        Assert.False(tree.Contains(new Interval<int>(3, 9)));
        Assert.False(tree.Contains(new Interval<int>(2, 5)));

        // Removing a duplicate takes out exactly one occurrence.
        Assert.True(tree.TryRemove(new Interval<int>(3, 8), out var once));
        Assert.Equal(3, once.Count);
        Assert.True(once.Contains(new Interval<int>(3, 8)));
        Assert.True(once.TryRemove(new Interval<int>(3, 8), out var twice));
        Assert.Equal(2, twice.Count);
        Assert.False(twice.Contains(new Interval<int>(3, 8)));

        // Removing something absent reports false and leaves the tree unchanged.
        Assert.False(tree.TryRemove(new Interval<int>(0, 0), out var unchanged));
        Assert.Equal(tree.ToArray(), unchanged.ToArray());
        Assert.Equal(tree.ToArray(), tree.Remove(new Interval<int>(99, 100)).ToArray());
    }

    /// <summary>Verifies overlap counting agrees with brute force.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(20)]
    [InlineData(21)]
    public void CountOverlaps_AgreesWithBruteForce(int seed)
    {
        var random = new Random(seed);
        var intervals = Enumerable.Range(0, 120)
            .Select(_ => { var low = random.Next(0, 300); return new Interval<int>(low, low + random.Next(0, 40)); })
            .ToList();
        var tree = IntervalTree<int>.CreateRange(intervals);

        for (var q = 0; q < 200; q++)
        {
            var low = random.Next(-10, 320);
            var query = new Interval<int>(low, low + random.Next(0, 40));
            Assert.Equal(intervals.Count(iv => iv.Overlaps(query)), tree.CountOverlaps(query));
        }
    }

    /// <summary>Verifies coalescing produces disjoint maximal intervals matching a sweep-merge model.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(30)]
    [InlineData(31)]
    [InlineData(32)]
    public void Coalesce_MatchesSweepMergeModel(int seed)
    {
        var random = new Random(seed);
        var intervals = Enumerable.Range(0, 80)
            .Select(_ => { var low = random.Next(0, 100); return new Interval<int>(low, low + random.Next(0, 20)); })
            .ToList();
        var tree = IntervalTree<int>.CreateRange(intervals);

        var coalesced = tree.Coalesce().ToArray();

        // Model: sort by low, sweep-merge overlapping (touching counts as overlap).
        var expected = new List<Interval<int>>();
        foreach (var iv in intervals.OrderBy(i => i.Low).ThenBy(i => i.High))
        {
            if (expected.Count > 0 && iv.Low <= expected[^1].High)
            {
                var last = expected[^1];
                expected[^1] = new Interval<int>(last.Low, Math.Max(last.High, iv.High));
            }
            else
            {
                expected.Add(iv);
            }
        }

        Assert.Equal(expected, coalesced);

        // The result is disjoint: each interval starts strictly after the previous one ends.
        for (var i = 1; i < coalesced.Length; i++)
            Assert.True(coalesced[i].Low > coalesced[i - 1].High);

        // Coalescing covers exactly the same point set: every original interval is contained in some merged one.
        foreach (var iv in intervals)
            Assert.Contains(coalesced, m => m.Low <= iv.Low && iv.High <= m.High);
    }

    /// <summary>Verifies coalescing an empty tree yields an empty tree.</summary>
    [Fact]
    public void Coalesce_OnEmpty_IsEmpty()
    {
        Assert.True(IntervalTree<int>.Empty.Coalesce().IsEmpty);
    }

    /// <summary>Verifies the documented worked example produces the stated results.</summary>
    [Fact]
    public void DocumentedExample_ProducesStatedResults()
    {
        var tree = IntervalTree<int>.Empty
            .Insert(1, 5)
            .Insert(10, 15)
            .Insert(3, 8);

        Assert.True(tree.TryFindOverlap(new Interval<int>(6, 9), out var hit));
        Assert.Equal(new Interval<int>(3, 8), hit);

        var all = tree.FindOverlaps(new Interval<int>(4, 12))
            .OrderBy(iv => iv.Low)
            .ToArray();
        Assert.Equal(
            new[] { new Interval<int>(1, 5), new Interval<int>(3, 8), new Interval<int>(10, 15) },
            all);
    }
}
