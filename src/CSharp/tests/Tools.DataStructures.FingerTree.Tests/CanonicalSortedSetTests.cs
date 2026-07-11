using System.Collections.Concurrent;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Canonical-shape, persistence, policy, and model coverage for <see cref="CanonicalSortedSet{T}"/>.</summary>
public sealed class CanonicalSortedSetTests
{
    /// <summary>Verifies permutations and delete/reinsert histories converge on one shape.</summary>
    [Fact]
    public void IndependentHistories_ProduceIdenticalShapeAndDigest()
    {
        var policy = ZipTreeRankPolicy<int>.Create(seed: 0x123456789abcdef0UL);
        var items = Enumerable.Range(-500, 1_000).ToArray();
        var ascending = CanonicalSortedSet<int>.CreateRange(items, policy);
        var random = new Random(20260716);

        for (var trial = 0; trial < 25; trial++)
        {
            var permutation = items.OrderBy(_ => random.Next()).ToArray();
            var set = CanonicalSortedSet<int>.CreateRange(permutation, policy);
            Assert.Equal(ascending.ShapeForTesting(), set.ShapeForTesting());
            Assert.Equal(ascending.ContentHash, set.ContentHash);
            Assert.True(ascending.SetEquals(set));

            foreach (var item in permutation.Take(100))
                set = set.Remove(item);
            foreach (var item in permutation.Take(100).Reverse())
                set = set.Add(item);
            Assert.Equal(ascending.ShapeForTesting(), set.ShapeForTesting());
        }
    }

    /// <summary>Checks randomized update histories against a BCL sorted set while retaining snapshots.</summary>
    [Fact]
    public void RandomizedHistory_MatchesSortedSetModel()
    {
        var policy = ZipTreeRankPolicy<int>.Create(seed: 42);
        var set = CanonicalSortedSet<int>.Create(policy);
        var model = new System.Collections.Generic.SortedSet<int>();
        var snapshots = new List<(CanonicalSortedSet<int>, int[])>();
        var random = new Random(20260717);
        for (var i = 0; i < 50_000; i++)
        {
            var item = random.Next(-5_000, 5_001);
            if (random.Next(3) == 0)
            {
                set = set.Remove(item);
                model.Remove(item);
            }
            else
            {
                set = set.Add(item);
                model.Add(item);
            }
            if (i % 2_003 == 0)
                snapshots.Add((set, [.. model]));
        }

        Assert.Equal(model.ToArray(), set.ToArray());
        foreach (var (snapshot, expected) in snapshots)
            Assert.Equal(expected, snapshot);
        Assert.InRange(set.Height, 1, 64);
    }

    /// <summary>Verifies comparer-equivalent representatives and hash-equivalence policy.</summary>
    [Fact]
    public void ComparerEquivalentItems_RetainFirstRepresentative()
    {
        var policy = ZipTreeRankPolicy<string>.Create(
            StringComparer.OrdinalIgnoreCase,
            value => unchecked((ulong)StringComparer.OrdinalIgnoreCase.GetHashCode(value)),
            seed: 7);
        var stored = new string(['A', 'l', 'p', 'h', 'a']);
        var set = CanonicalSortedSet<string>.Create(policy).Add(stored).Add("ALPHA");

        Assert.Single(set);
        Assert.True(set.TryGetValue("alpha", out var actual));
        Assert.Same(stored, actual);
        Assert.Same(set, set.Add("aLpHa"));
    }

    /// <summary>Verifies policy-gated algebra and canonical results.</summary>
    [Fact]
    public void Algebra_RequiresPolicyIdentityAndCanonicalizesResults()
    {
        var policy = ZipTreeRankPolicy<int>.Create(seed: 99);
        var left = CanonicalSortedSet<int>.CreateRange([1, 2, 4, 8], policy);
        var right = CanonicalSortedSet<int>.CreateRange([2, 3, 4, 5], policy);

        Assert.Equal(new[] { 1, 2, 3, 4, 5, 8 }, left.Union(right));
        Assert.Equal(new[] { 2, 4 }, left.Intersect(right));
        Assert.Equal(new[] { 1, 8 }, left.Except(right));
        Assert.Same(left, left.Union(left));
        Assert.Same(left, left.Intersect(left));

        var incompatible = CanonicalSortedSet<int>.CreateRange([1, 2], ZipTreeRankPolicy<int>.Create(seed: 99));
        Assert.Throws<ArgumentException>(() => left.Union(incompatible));
        Assert.False(left.SetEquals(incompatible));
    }

    /// <summary>Verifies rank-hash collisions remain correct and history-independent.</summary>
    [Fact]
    public void CollidingRankHash_UsesComparerTieBreakDeterministically()
    {
        var policy = ZipTreeRankPolicy<int>.Create(rankHash: _ => 0, seed: 1);
        var forward = CanonicalSortedSet<int>.CreateRange(Enumerable.Range(0, 100), policy);
        var reverse = CanonicalSortedSet<int>.CreateRange(Enumerable.Range(0, 100).Reverse(), policy);

        Assert.Equal(forward.ShapeForTesting(), reverse.ShapeForTesting());
        Assert.Equal(Enumerable.Range(0, 100).ToArray(), forward.ToArray());
        Assert.Equal(100, forward.Height);
    }

    /// <summary>Verifies memoized digest publication is stable under concurrent readers.</summary>
    [Fact]
    public void ContentHash_PublishesSafelyAcrossReaders()
    {
        var set = CanonicalSortedSet<int>.CreateRange(Enumerable.Range(0, 20_000));
        var hashes = new ConcurrentBag<ulong>();
        Parallel.For(0, 1_000, _ => hashes.Add(set.ContentHash));
        Assert.Single(hashes.Distinct());
        Assert.NotEqual(0UL, hashes.First());
    }
}
