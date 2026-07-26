// Tests for the sorted collection property.

using CsCheck;
using Xunit;
using FtSortedSet = Durable7.FingerTree.SortedSet<int>;
using FtSortedBag = Durable7.FingerTree.SortedBag<int>;
using BclSortedSet = System.Collections.Generic.SortedSet<int>;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Property-based tests (CsCheck) for the sorted collections, checked against framework oracles
/// (<see cref="System.Collections.Generic.SortedSet{T}"/> / <see cref="System.Collections.Generic.HashSet{T}"/>
/// and a sorted-list multiset model) over thousands of generated inputs with shrinking.
/// </summary>
public sealed class SortedCollectionPropertyTests
{
    private static int[] ReadSorted(FtSortedSet set)
    {
        var result = new int[set.Count];
        for (var i = 0; i < result.Length; i++)
            result[i] = set[i];
        return result;
    }

    private static int[] Sorted(IEnumerable<int> values) => values.OrderBy(v => v).ToArray();

    /// <summary>
    /// Verifies that a <c>SortedSet</c> built from generated values has the same sorted-distinct content,
    /// membership, order-statistic indexing, and floor/ceiling/lower/higher answers as the framework set.
    /// </summary>
    [Fact]
    public void SortedSet_MatchesFrameworkSet()
    {
        Gen.Int[-50, 50].Array[0, 200].Sample(values =>
        {
            var ours = FtSortedSet.CreateRange(values);
            var bcl = new BclSortedSet(values);
            var sorted = bcl.ToArray();

            Assert.Equal(bcl.Count, ours.Count);
            Assert.Equal(sorted, ReadSorted(ours));

            for (var i = 0; i < sorted.Length; i++)
                Assert.Equal(i, ours.IndexOf(sorted[i]));

            for (var probe = -60; probe <= 60; probe++)
            {
                Assert.Equal(bcl.Contains(probe), ours.Contains(probe));

                AssertNeighbor(sorted.Where(x => x <= probe).LastOrNull(), ours.TryFloor(probe, out var floor), floor);
                AssertNeighbor(sorted.Where(x => x >= probe).FirstOrNull(), ours.TryCeiling(probe, out var ceil), ceil);
                AssertNeighbor(sorted.Where(x => x < probe).LastOrNull(), ours.TryLower(probe, out var lower), lower);
                AssertNeighbor(sorted.Where(x => x > probe).FirstOrNull(), ours.TryHigher(probe, out var higher), higher);
            }
        }, iter: 300);
    }

    /// <summary>
    /// Verifies the set-algebra operations and relation predicates against <see cref="HashSet{T}"/> over two
    /// independently generated sets.
    /// </summary>
    [Fact]
    public void SortedSet_Algebra_MatchesHashSet()
    {
        Gen.Select(Gen.Int[-30, 30].Array[0, 120], Gen.Int[-30, 30].Array[0, 120])
            .Sample(t =>
            {
                var (left, right) = t;
                var a = FtSortedSet.CreateRange(left);
                var b = FtSortedSet.CreateRange(right);
                var ha = new HashSet<int>(left);
                var hb = new HashSet<int>(right);

                Assert.Equal(Sorted(ha.Union(hb)), ReadSorted(a.Union(b)));
                Assert.Equal(Sorted(ha.Intersect(hb)), ReadSorted(a.Intersect(b)));
                Assert.Equal(Sorted(ha.Except(hb)), ReadSorted(a.Except(b)));

                var symmetric = new HashSet<int>(ha);
                symmetric.SymmetricExceptWith(hb);
                Assert.Equal(Sorted(symmetric), ReadSorted(a.SymmetricExcept(b)));

                Assert.Equal(ha.IsSubsetOf(hb), a.IsSubsetOf(b));
                Assert.Equal(ha.IsSupersetOf(hb), a.IsSupersetOf(b));
                Assert.Equal(ha.IsProperSubsetOf(hb), a.IsProperSubsetOf(b));
                Assert.Equal(ha.IsProperSupersetOf(hb), a.IsProperSupersetOf(b));
                Assert.Equal(ha.SetEquals(hb), a.SetEquals(b));
                Assert.Equal(ha.Overlaps(hb), a.Overlaps(b));
            }, iter: 300);
    }

    /// <summary>
    /// Verifies that a <c>SortedBag</c> preserves multiplicity: sorted order via order-statistic indexing and the
    /// counting queries match a sorted-list multiset model.
    /// </summary>
    [Fact]
    public void SortedBag_MatchesMultisetModel()
    {
        Gen.Int[-20, 20].Array[0, 200].Sample(values =>
        {
            var bag = FtSortedBag.CreateRange(values);
            var model = values.OrderBy(v => v).ToArray();

            Assert.Equal(values.Length, bag.Count);
            for (var i = 0; i < model.Length; i++)
                Assert.Equal(model[i], bag[i]);

            for (var probe = -25; probe <= 25; probe++)
            {
                Assert.Equal(values.Count(x => x == probe), bag.CountOf(probe));
                Assert.Equal(values.Count(x => x < probe), bag.CountLessThan(probe));
                Assert.Equal(values.Count(x => x <= probe), bag.CountAtMost(probe));
                Assert.Equal(values.Contains(probe), bag.Contains(probe));
            }
        }, iter: 300);
    }

    private static void AssertNeighbor(int? expected, bool found, int actual)
    {
        Assert.Equal(expected.HasValue, found);
        if (expected.HasValue)
            Assert.Equal(expected.Value, actual);
    }
}

/// <summary>Nullable first/last helpers that avoid the value/empty ambiguity of <c>DefaultIfEmpty</c>.</summary>
internal static class NeighborQueryExtensions
{
    public static int? FirstOrNull(this IEnumerable<int> source)
    {
        foreach (var value in source)
            return value;
        return null;
    }

    public static int? LastOrNull(this IEnumerable<int> source)
    {
        int? last = null;
        foreach (var value in source)
            last = value;
        return last;
    }
}
