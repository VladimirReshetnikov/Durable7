using CsCheck;
using Xunit;
using IntSet = Tools.DataStructures.Hamt.PersistentHashSet<int>;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>
/// API and algebra tests for <see cref="PersistentHashSet{T}"/>.
/// </summary>
public sealed class PersistentHashSetTests
{
    /// <summary>Verifies basic add/remove/contains behavior and persistence.</summary>
    [Fact]
    public void AddRemoveAndContains_WorkPersistently()
    {
        var empty = IntSet.Empty;
        var one = empty.Add(1);
        var two = one.Add(2);
        var removed = two.Remove(1);

        Assert.Empty(empty);
        Assert.True(one.Contains(1));
        Assert.False(one.Contains(2));
        Assert.True(two.Contains(1));
        Assert.True(two.Contains(2));
        Assert.False(removed.Contains(1));
        Assert.True(removed.Contains(2));
        Assert.Same(one, one.Add(1));
        Assert.Same(one, one.Remove(9));
    }

    /// <summary>Verifies duplicate-aware add and remove helpers.</summary>
    [Fact]
    public void TryAddAndTryRemove_ReportWhetherMembershipChanged()
    {
        var set = IntSet.Empty.Add(1);

        Assert.False(set.TryAdd(1, out var same));
        Assert.Same(set, same);
        Assert.True(set.TryAdd(2, out var added));
        Assert.True(added.Contains(2));

        Assert.True(added.TryRemove(1, out var removed));
        Assert.False(removed.Contains(1));
        Assert.False(removed.TryRemove(1, out var stillRemoved));
        Assert.Same(removed, stillRemoved);
    }

    /// <summary>Verifies case-insensitive comparer behavior and first equivalent item retention.</summary>
    [Fact]
    public void CustomComparer_DefinesEqualityAndRetainsFirstItem()
    {
        var set = PersistentHashSet<string>.CreateRange(
            new[] { "Alpha", "ALPHA", "beta" },
            StringComparer.OrdinalIgnoreCase);

        Assert.Equal(2, set.Count);
        Assert.True(set.Contains("alpha"));
        Assert.Contains("Alpha", set);
        Assert.DoesNotContain("ALPHA", set);
        Assert.Same(StringComparer.OrdinalIgnoreCase, set.Comparer);
    }

    /// <summary>Verifies null reference items follow the configured comparer semantics.</summary>
    [Fact]
    public void NullReferenceItem_IsSupportedWhenComparerSupportsIt()
    {
        var set = PersistentHashSet<string?>.Empty.Add(null).Add("value");

        Assert.True(set.Contains(null));
        Assert.True(set.Contains("value"));
        Assert.True(set.TryRemove(null, out var removed));
        Assert.False(removed.Contains(null));
        Assert.True(removed.Contains("value"));
    }

    /// <summary>Verifies emptying a custom-comparer set preserves the comparer.</summary>
    [Fact]
    public void Clear_PreservesCustomComparer()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var set = PersistentHashSet<string>.Create(comparer).Add("x");
        var cleared = set.Clear();

        Assert.True(cleared.IsEmpty);
        Assert.Same(comparer, cleared.Comparer);
        Assert.NotSame(PersistentHashSet<string>.Empty, cleared);
        Assert.Same(cleared, cleared.Clear());
    }

    /// <summary>Verifies set algebra against <see cref="HashSet{T}"/> over generated inputs.</summary>
    [Fact]
    public void Algebra_MatchesHashSet()
    {
        Gen.Select(Gen.Int[-30, 30].Array[0, 120], Gen.Int[-30, 30].Array[0, 120])
            .Sample(t =>
            {
                var (left, right) = t;
                var a = IntSet.CreateRange(left);
                var ha = new HashSet<int>(left);
                var hb = new HashSet<int>(right);

                AssertEqualSet(ha.Union(hb), a.Union(right));
                AssertEqualSet(ha.Intersect(hb), a.Intersect(right));
                AssertEqualSet(ha.Except(hb), a.Except(right));

                var symmetric = new HashSet<int>(ha);
                symmetric.SymmetricExceptWith(hb);
                AssertEqualSet(symmetric, a.SymmetricExcept(right));

                Assert.Equal(ha.IsSubsetOf(hb), a.IsSubsetOf(right));
                Assert.Equal(ha.IsSupersetOf(hb), a.IsSupersetOf(right));
                Assert.Equal(ha.Overlaps(hb), a.Overlaps(right));
                Assert.Equal(ha.SetEquals(hb), a.SetEquals(right));
            }, iter: 300);
    }

    /// <summary>Verifies symmetric difference treats duplicate right-side items as a set.</summary>
    [Fact]
    public void SymmetricExcept_TreatsInputDuplicatesAsOneItem()
    {
        var set = IntSet.Empty.Add(1).Add(2);

        AssertEqualSet(new[] { 2, 3 }, set.SymmetricExcept(new[] { 1, 1, 3, 3 }));
    }

    private static void AssertEqualSet(IEnumerable<int> expected, IntSet actual) =>
        Assert.Equal(expected.OrderBy(x => x).ToArray(), actual.OrderBy(x => x).ToArray());
}
