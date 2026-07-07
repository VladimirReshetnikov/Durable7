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
        Assert.DoesNotContain("ALPHA", set.AsEnumerable(), StringComparer.Ordinal);
        Assert.True(set.TryGetValue("ALPHA", out var stored));
        Assert.Equal("Alpha", stored, StringComparer.Ordinal);
        Assert.Same(StringComparer.OrdinalIgnoreCase, set.Comparer);
    }

    /// <summary>Verifies that stored item objects are recoverable through <c>TryGetValue</c>.</summary>
    [Fact]
    public void TryGetValue_ReturnsStoredItemObject()
    {
        var storedItem = new string(['A', 'l', 'p', 'h', 'a']);
        var set = PersistentHashSet<string>.Create(StringComparer.OrdinalIgnoreCase).Add(storedItem);

        Assert.True(set.TryGetValue("ALPHA", out var actual));
        Assert.Same(storedItem, actual);

        var missing = "gamma";
        Assert.False(set.TryGetValue(missing, out var fallback));
        Assert.Same(missing, fallback);
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
                Assert.Equal(ha.IsProperSubsetOf(hb), a.IsProperSubsetOf(right));
                Assert.Equal(ha.IsProperSupersetOf(hb), a.IsProperSupersetOf(right));
                Assert.Equal(ha.Overlaps(hb), a.Overlaps(right));
                Assert.Equal(ha.SetEquals(hb), a.SetEquals(right));
            }, iter: 300);
    }

    /// <summary>Verifies set algebra honors a custom comparer and retains stored item objects.</summary>
    [Fact]
    public void Algebra_HonorsCustomComparer()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var set = PersistentHashSet<string>.CreateRange(["Alpha", "Beta"], comparer);

        var intersection = set.Intersect(["ALPHA"]);
        Assert.Equal("Alpha", Assert.Single(intersection), StringComparer.Ordinal);
        Assert.Same(comparer, intersection.Comparer);

        var union = set.Union(["ALPHA", "gamma"]);
        Assert.Equal(["Alpha", "Beta", "gamma"], union.OrderBy(x => x, StringComparer.Ordinal).ToArray());
        Assert.Same(comparer, union.Comparer);

        var symmetric = set.SymmetricExcept(["BETA", "gamma"]);
        Assert.Equal(["Alpha", "gamma"], symmetric.OrderBy(x => x, StringComparer.Ordinal).ToArray());

        Assert.True(set.IsSubsetOf(["ALPHA", "BETA", "x"]));
        Assert.True(set.IsProperSubsetOf(["ALPHA", "BETA", "x"]));
        Assert.False(set.IsProperSubsetOf(["ALPHA", "BETA"]));
        Assert.True(set.IsSupersetOf(["ALPHA"]));
        Assert.True(set.IsProperSupersetOf(["ALPHA"]));
        Assert.False(set.IsProperSupersetOf(["ALPHA", "BETA"]));
        Assert.True(set.SetEquals(["ALPHA", "beta"]));
        Assert.True(set.Overlaps(["ALPHA"]));
    }

    /// <summary>Verifies the set satisfies the <see cref="IReadOnlySet{T}"/> contract.</summary>
    [Fact]
    public void ImplementsReadOnlySet()
    {
        IReadOnlySet<int> set = IntSet.CreateRange([1, 2]);

        Assert.Equal(2, set.Count);
        Assert.True(set.Contains(1));
        Assert.True(set.IsSubsetOf([1, 2, 3]));
        Assert.True(set.IsProperSubsetOf([1, 2, 3]));
        Assert.True(set.IsSupersetOf([1]));
        Assert.True(set.IsProperSupersetOf([1]));
        Assert.True(set.Overlaps([2, 9]));
        Assert.True(set.SetEquals([2, 1]));
    }

    /// <summary>Verifies every sequence-accepting member rejects null arguments.</summary>
    [Fact]
    public void SequenceArguments_RejectNull()
    {
        var set = IntSet.Empty;

        Assert.Throws<ArgumentNullException>(() => PersistentHashSet<int>.CreateRange(null!));
        Assert.Throws<ArgumentNullException>(() => set.Union(null!));
        Assert.Throws<ArgumentNullException>(() => set.Intersect(null!));
        Assert.Throws<ArgumentNullException>(() => set.Except(null!));
        Assert.Throws<ArgumentNullException>(() => set.SymmetricExcept(null!));
        Assert.Throws<ArgumentNullException>(() => set.IsSubsetOf(null!));
        Assert.Throws<ArgumentNullException>(() => set.IsProperSubsetOf(null!));
        Assert.Throws<ArgumentNullException>(() => set.IsSupersetOf(null!));
        Assert.Throws<ArgumentNullException>(() => set.IsProperSupersetOf(null!));
        Assert.Throws<ArgumentNullException>(() => set.Overlaps(null!));
        Assert.Throws<ArgumentNullException>(() => set.SetEquals(null!));
    }

    /// <summary>Verifies default-comparer empty results canonicalize to the shared empty singleton.</summary>
    [Fact]
    public void EmptyResults_CanonicalizeToSharedSingleton()
    {
        Assert.Same(IntSet.Empty, IntSet.Create());
        Assert.Same(IntSet.Empty, IntSet.CreateRange([]));
        Assert.Same(IntSet.Empty, IntSet.Empty.Add(1).Remove(1));
        Assert.Same(IntSet.Empty, IntSet.Empty.Add(1).Clear());
    }

    /// <summary>Verifies the struct enumerator's direct-use contract.</summary>
    [Fact]
    public void StructEnumerator_EnumeratesAllItems()
    {
        var set = IntSet.CreateRange([1, 2, 3]);
        var seen = new List<int>();

        var enumerator = set.GetEnumerator();
        while (enumerator.MoveNext())
            seen.Add(enumerator.Current);

        Assert.Equal([1, 2, 3], seen.OrderBy(x => x).ToArray());
        Assert.False(enumerator.MoveNext());
    }

    /// <summary>Verifies symmetric difference treats duplicate right-side items as a set.</summary>
    [Fact]
    public void SymmetricExcept_TreatsInputDuplicatesAsOneItem()
    {
        var set = IntSet.Empty.Add(1).Add(2);

        AssertEqualSet(new[] { 2, 3 }, set.SymmetricExcept(new[] { 1, 1, 3, 3 }));
    }

    /// <summary>Verifies retained immutable set snapshots are safe for concurrent readers.</summary>
    [Fact]
    public void ConcurrentReaders_ObserveConsistentRetainedSnapshot()
    {
        var set = IntSet.Empty;
        for (var value = 0; value < 512; value++)
            set = set.Add(value);

        Parallel.For(0, Environment.ProcessorCount * 4, _ =>
        {
            for (var pass = 0; pass < 64; pass++)
            {
                Assert.Equal(512, set.Count);
                for (var value = 0; value < 512; value += 17)
                    Assert.True(set.Contains(value));

                Assert.Equal(Enumerable.Range(0, 512), set.OrderBy(value => value).ToArray());
                Assert.True(set.SetEquals(Enumerable.Range(0, 512)));
            }
        });
    }

    private static void AssertEqualSet(IEnumerable<int> expected, IntSet actual) =>
        Assert.Equal(expected.OrderBy(x => x).ToArray(), actual.OrderBy(x => x).ToArray());
}
