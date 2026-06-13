using Xunit;
using FtSortedSet = Tools.DataStructures.FingerTree.SortedSet<int>;
using BclSortedSet = System.Collections.Generic.SortedSet<int>;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies <see cref="Tools.DataStructures.FingerTree.SortedSet{T}"/> against the BCL
/// <see cref="System.Collections.Generic.SortedSet{T}"/> as an oracle: uniqueness, ordering, navigable
/// neighbor queries, order-statistic indexing, range extraction, and set algebra/relations.
/// </summary>
public sealed class SortedSetTests
{
    /// <summary>Verifies construction dedupes and sorts, and Add is a no-op for present elements.</summary>
    [Fact]
    public void CreateAndAdd_EnforceUniquenessAndOrder()
    {
        var set = FtSortedSet.CreateRange(new[] { 5, 1, 3, 3, 9, 1, 5 });

        Assert.Equal(new[] { 1, 3, 5, 9 }, set.ToArray());
        Assert.Equal(4, set.Count);

        var again = set.Add(3);
        Assert.Same(set, again);                 // present -> unchanged instance
        Assert.Equal(new[] { 1, 3, 4, 5, 9 }, set.Add(4).ToArray());
    }

    /// <summary>Verifies indexing, ranking, and navigable neighbor queries against a sorted array model.</summary>
    [Fact]
    public void IndexingRankingAndNavigation_MatchModel()
    {
        var values = new[] { 2, 4, 6, 8, 10 };
        var set = FtSortedSet.CreateRange(values);

        for (var i = 0; i < values.Length; i++)
        {
            Assert.Equal(values[i], set[i]);
            Assert.Equal(i, set.IndexOf(values[i]));
        }

        Assert.Equal(-1, set.IndexOf(5));
        Assert.Throws<ArgumentOutOfRangeException>(() => set[values.Length]);

        foreach (var key in new[] { 1, 2, 5, 6, 11 })
        {
            AssertNeighbor(set.TryFloor(key, out var f), f, values.Where(v => v <= key).Cast<int?>().LastOrDefault());
            AssertNeighbor(set.TryCeiling(key, out var c), c, values.Where(v => v >= key).Cast<int?>().FirstOrDefault());
            AssertNeighbor(set.TryLower(key, out var lo), lo, values.Where(v => v < key).Cast<int?>().LastOrDefault());
            AssertNeighbor(set.TryHigher(key, out var hi), hi, values.Where(v => v > key).Cast<int?>().FirstOrDefault());
        }

        static void AssertNeighbor(bool found, int actual, int? expected)
        {
            Assert.Equal(expected.HasValue, found);
            if (expected.HasValue)
                Assert.Equal(expected.Value, actual);
        }
    }

    /// <summary>Verifies inclusive range extraction against the BCL view-between.</summary>
    [Fact]
    public void GetRange_MatchesBclView()
    {
        var values = Enumerable.Range(0, 30).Select(v => v * 2).ToArray();
        var set = FtSortedSet.CreateRange(values);
        var bcl = new BclSortedSet(values);

        Assert.Equal(bcl.GetViewBetween(5, 21).ToArray(), set.GetRange(5, 21).ToArray());
        Assert.Equal(bcl.GetViewBetween(0, 0).ToArray(), set.GetRange(0, 0).ToArray());
        Assert.Empty(set.GetRange(100, 200).ToArray());
        Assert.Empty(set.GetRange(21, 5).ToArray());
    }

    /// <summary>Verifies the binary set operations against the BCL set operations.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    public void SetAlgebra_MatchesBcl(int seed)
    {
        var random = new Random(seed);
        var a = Enumerable.Range(0, 60).Select(_ => random.Next(100)).ToArray();
        var b = Enumerable.Range(0, 60).Select(_ => random.Next(100)).ToArray();

        var setA = FtSortedSet.CreateRange(a);
        var setB = FtSortedSet.CreateRange(b);
        var bclA = new BclSortedSet(a);
        var bclB = new BclSortedSet(b);

        Assert.Equal(Combined(bclA, bclB, (x, y) => x.UnionWith(y)), setA.Union(setB).ToArray());
        Assert.Equal(Combined(bclA, bclB, (x, y) => x.IntersectWith(y)), setA.Intersect(setB).ToArray());
        Assert.Equal(Combined(bclA, bclB, (x, y) => x.ExceptWith(y)), setA.Except(setB).ToArray());
        Assert.Equal(Combined(bclA, bclB, (x, y) => x.SymmetricExceptWith(y)), setA.SymmetricExcept(setB).ToArray());

        Assert.Equal(bclA.IsSubsetOf(bclB), setA.IsSubsetOf(setB));
        Assert.Equal(bclA.IsSupersetOf(bclB), setA.IsSupersetOf(setB));
        Assert.Equal(bclA.IsProperSubsetOf(bclB), setA.IsProperSubsetOf(setB));
        Assert.Equal(bclA.IsProperSupersetOf(bclB), setA.IsProperSupersetOf(setB));
        Assert.Equal(bclA.Overlaps(bclB), setA.Overlaps(setB));
        Assert.Equal(bclA.SetEquals(bclB), setA.SetEquals(setB));

        static int[] Combined(BclSortedSet x, BclSortedSet y, Action<BclSortedSet, BclSortedSet> op)
        {
            var copy = new BclSortedSet(x);
            op(copy, y);
            return copy.ToArray();
        }
    }

    /// <summary>Verifies a custom (reverse) comparer is honored and preserved across operations.</summary>
    [Fact]
    public void CustomComparer_OrdersDescendingAndIsPreserved()
    {
        var descending = Comparer<int>.Create((x, y) => y.CompareTo(x));
        var set = FtSortedSet.CreateRange(new[] { 5, 1, 9, 3, 5 }, descending);

        Assert.Equal(new[] { 9, 5, 3, 1 }, set.ToArray());
        Assert.Equal(9, set.Min);   // least under the reversed order is the numeric maximum
        Assert.True(set.TryCeiling(6, out var c));
        Assert.Equal(5, c);         // least element (in reversed order) that is >= 6 in that order is 5
        Assert.Same(descending, set.Add(7).Comparer);
        Assert.Equal(new[] { 9, 7, 5, 3, 1 }, set.Add(7).ToArray());
    }

    /// <summary>Runs a deterministic mixed history against the BCL sorted set.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    public void MixedHistory_MatchesBcl(int seed)
    {
        var random = new Random(seed);
        var model = new BclSortedSet();
        var set = FtSortedSet.Empty;

        for (var step = 0; step < 400; step++)
        {
            var value = random.Next(50);
            if (random.Next(2) == 0)
            {
                model.Add(value);
                set = set.Add(value);
            }
            else
            {
                model.Remove(value);
                set.TryRemove(value, out set);
            }

            Assert.Equal(model.Count, set.Count);
            Assert.Equal(model.ToArray(), set.ToArray());
            Assert.Equal(model.Contains(value), set.Contains(value));
        }
    }

    /// <summary>
    /// Verifies the navigable queries on a reference-type set: the not-found path reports false (exercising
    /// the <c>[MaybeNullWhen(false)]</c> contract on the out parameter for a nullable reference type).
    /// </summary>
    [Fact]
    public void NavigableQueries_OnReferenceType_HandleAbsence()
    {
        var set = Tools.DataStructures.FingerTree.SortedSet<string>.CreateRange(new[] { "bravo", "delta" });

        Assert.False(set.TryLower("alpha", out _));     // nothing strictly before "alpha"
        Assert.False(set.TryHigher("echo", out _));     // nothing strictly after "echo"
        Assert.True(set.TryCeiling("charlie", out var ceiling));
        Assert.Equal("delta", ceiling);
        Assert.True(set.TryFloor("charlie", out var floor));
        Assert.Equal("bravo", floor);
    }

    /// <summary>Verifies emptying a default-comparer set collapses to the shared <c>Empty</c> singleton.</summary>
    [Fact]
    public void EmptyingWithDefaultComparer_CollapsesToSingleton()
    {
        var set = FtSortedSet.Empty.Add(7).Remove(7);
        Assert.True(set.IsEmpty);
        Assert.Same(FtSortedSet.Empty, set);
    }

    /// <summary>Verifies emptying a custom-comparer set keeps its own comparer (no singleton collapse).</summary>
    [Fact]
    public void EmptyingWithCustomComparer_PreservesComparer()
    {
        var descending = Comparer<int>.Create((x, y) => y.CompareTo(x));
        var set = FtSortedSet.Create(descending).Add(7).Remove(7);
        Assert.True(set.IsEmpty);
        Assert.NotSame(FtSortedSet.Empty, set);
        Assert.Same(descending, set.Comparer);
    }

    /// <summary>Verifies empty-set degenerate behavior.</summary>
    [Fact]
    public void Empty_HasNoElements()
    {
        var empty = FtSortedSet.Empty;
        Assert.True(empty.IsEmpty);
        Assert.Empty(empty);
        Assert.False(empty.Contains(1));
        Assert.Equal(-1, empty.IndexOf(1));
        Assert.False(empty.TryFloor(1, out _));
        Assert.False(empty.TryCeiling(1, out _));
        Assert.Throws<InvalidOperationException>(() => empty.Min);
    }
}
