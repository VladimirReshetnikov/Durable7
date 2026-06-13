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
