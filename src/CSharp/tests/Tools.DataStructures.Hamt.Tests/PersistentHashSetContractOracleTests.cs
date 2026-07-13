using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>
/// Pins the persistent-set semantics that lifecycle and frozen representations must preserve.
/// </summary>
public sealed class PersistentHashSetContractOracleTests
{
    /// <summary>Verifies comparer object identity survives every persistent transition.</summary>
    [Fact]
    public void ComparerIdentity_IsPreservedByEveryVersion()
    {
        var comparer = new ConstantHashOrdinalIgnoreCaseComparer();
        var empty = PersistentHashSet<string>.Create(comparer);
        var added = empty.Add("alpha");
        var duplicate = added.Add("ALPHA");
        var removed = duplicate.Remove("Alpha");
        var cleared = added.Clear();

        Assert.Same(comparer, empty.Comparer);
        Assert.Same(comparer, added.Comparer);
        Assert.Same(comparer, duplicate.Comparer);
        Assert.Same(comparer, removed.Comparer);
        Assert.Same(comparer, cleared.Comparer);
    }

    /// <summary>Verifies adding equivalent values retains the first stored object.</summary>
    [Fact]
    public void EquivalentAdd_RetainsFirstStoredRepresentative()
    {
        var stored = NewString("Alpha");
        var equivalent = NewString("ALPHA");
        var set = PersistentHashSet<string>.Create(StringComparer.OrdinalIgnoreCase).Add(stored);

        var noOp = set.Add(equivalent);

        Assert.Same(set, noOp);
        Assert.True(noOp.TryGetValue(equivalent, out var actual));
        Assert.Same(stored, actual);
        Assert.Same(stored, Assert.Single(noOp));
    }

    /// <summary>Verifies CRUD and representative semantics inside an equal-hash collision bucket.</summary>
    [Fact]
    public void EqualHashCollisionBucket_PreservesAllDistinctItems()
    {
        var comparer = new ConstantHashOrdinalIgnoreCaseComparer();
        var firstAlpha = NewString("Alpha");
        var secondAlpha = NewString("ALPHA");
        var beta = NewString("Beta");
        var gamma = NewString("Gamma");
        var original = PersistentHashSet<string>.Create(comparer)
            .Add(firstAlpha)
            .Add(beta)
            .Add(gamma);

        var duplicate = original.Add(secondAlpha);
        var removed = duplicate.Remove(beta);

        Assert.Same(original, duplicate);
        Assert.Equal(3, original.Count);
        Assert.True(original.TryGetValue(secondAlpha, out var actualAlpha));
        Assert.Same(firstAlpha, actualAlpha);
        Assert.True(original.Contains(beta));
        Assert.True(original.Contains(gamma));
        Assert.False(removed.Contains(beta));
        Assert.True(removed.Contains(firstAlpha));
        Assert.True(removed.Contains(gamma));
    }

    /// <summary>Verifies null items participate in ordinary persistent operations.</summary>
    [Fact]
    public void NullItem_IsStoredFoundAndRemoved()
    {
        var empty = PersistentHashSet<string?>.Empty;
        var withNull = empty.Add(null);
        var withBoth = withNull.Add("present");
        var duplicate = withBoth.Add(null);
        var removed = duplicate.Remove(null);

        Assert.False(empty.Contains(null));
        Assert.True(withNull.Contains(null));
        Assert.True(withNull.TryGetValue(null, out var storedNull));
        Assert.Null(storedNull);
        Assert.Same(withBoth, duplicate);
        Assert.False(removed.Contains(null));
        Assert.True(removed.Contains("present"));
        Assert.True(withBoth.Contains(null));
    }

    /// <summary>Verifies trie and collision-bucket traversal remains stable for retained versions.</summary>
    [Fact]
    public void Enumeration_IsStableForAnUnchangedVersion()
    {
        var comparer = new ScriptedHashComparer();
        var items = new[]
        {
            new ScriptedHashItem("zero", 0),
            new ScriptedHashItem("deep-a", 1),
            new ScriptedHashItem("deep-b", 33),
            new ScriptedHashItem("collision-a", 7),
            new ScriptedHashItem("collision-b", 7),
            new ScriptedHashItem("high", unchecked((int)0x80000000)),
        };
        var set = PersistentHashSet<ScriptedHashItem>.CreateRange(items, comparer);
        var expected = set.ToArray();
        var changed = set.Add(new ScriptedHashItem("additional", 65));

        Assert.Equal(expected, set.ToArray());
        Assert.Equal(expected, set.ToArray());
        Assert.Equal(changed.ToArray(), changed.ToArray());
        Assert.Equal(expected, set.ToArray());
    }

    /// <summary>Verifies logically ineffective operations preserve set reference identity.</summary>
    [Fact]
    public void LogicalNoOps_ReturnTheSameSetInstance()
    {
        var set = PersistentHashSet<string>.Create(StringComparer.OrdinalIgnoreCase).Add("Alpha");

        Assert.Same(set, set.Add("ALPHA"));
        Assert.Same(set, set.Remove("missing"));
        Assert.False(set.TryAdd("alpha", out var duplicate));
        Assert.Same(set, duplicate);
        Assert.False(set.TryRemove("missing", out var notRemoved));
        Assert.Same(set, notRemoved);

        var empty = set.Clear();
        Assert.Same(empty, empty.Clear());
        Assert.Same(empty, empty.Remove("missing"));
    }

    /// <summary>Verifies later edits cannot change any retained older set version.</summary>
    [Fact]
    public void RetainedVersions_RemainIndependentSnapshots()
    {
        var empty = PersistentHashSet<int>.Empty;
        var one = empty.Add(1);
        var two = one.Add(2);
        var three = two.Add(3);
        var removed = three.Remove(2);

        Assert.Empty(empty);
        Assert.Equal([1], one.ToArray());
        Assert.Equal([1, 2], two.OrderBy(value => value).ToArray());
        Assert.Equal([1, 2, 3], three.OrderBy(value => value).ToArray());
        Assert.Equal([1, 3], removed.OrderBy(value => value).ToArray());
        Assert.True(two.Contains(2));
        Assert.False(one.Contains(2));
    }

    /// <summary>Verifies comparer callback failures propagate without affecting the source version.</summary>
    [Fact]
    public void ComparerCallbackExceptions_LeaveTheSourceVersionUnchanged()
    {
        var comparer = new SwitchableThrowingComparer();
        var set = PersistentHashSet<string>.Create(comparer)
            .Add("alpha")
            .Add("beta");
        var expected = set.ToArray();
        var expectedRoot = set.RootForTesting;

        comparer.ThrowFromEquals = true;
        var equalsFailure = Assert.Throws<ComparerCallbackException>(() => set.Add("gamma"));
        Assert.Same(comparer.Failure, equalsFailure);

        comparer.ThrowFromEquals = false;
        Assert.Same(expectedRoot, set.RootForTesting);
        Assert.Equal(expected, set.ToArray());

        comparer.ThrowFromGetHashCode = true;
        var hashFailure = Assert.Throws<ComparerCallbackException>(() => set.Remove("alpha"));
        Assert.Same(comparer.Failure, hashFailure);

        comparer.ThrowFromGetHashCode = false;
        Assert.Same(expectedRoot, set.RootForTesting);
        Assert.Equal(expected, set.ToArray());
        Assert.True(set.Contains("alpha"));
        Assert.True(set.Contains("beta"));
    }

    private static string NewString(string value) => new(value.ToCharArray());

    private sealed class ConstantHashOrdinalIgnoreCaseComparer : IEqualityComparer<string>
    {
        public bool Equals(string? x, string? y) => StringComparer.OrdinalIgnoreCase.Equals(x, y);

        public int GetHashCode(string obj) => 0;
    }

    private sealed record ScriptedHashItem(string Name, int Hash);

    private sealed class ScriptedHashComparer : IEqualityComparer<ScriptedHashItem>
    {
        public bool Equals(ScriptedHashItem? x, ScriptedHashItem? y) => x?.Name == y?.Name;

        public int GetHashCode(ScriptedHashItem obj) => obj.Hash;
    }

    private sealed class SwitchableThrowingComparer : IEqualityComparer<string>
    {
        internal ComparerCallbackException Failure { get; } = new();

        internal bool ThrowFromEquals { get; set; }

        internal bool ThrowFromGetHashCode { get; set; }

        public bool Equals(string? x, string? y)
        {
            if (ThrowFromEquals)
                throw Failure;

            return StringComparer.Ordinal.Equals(x, y);
        }

        public int GetHashCode(string obj)
        {
            if (ThrowFromGetHashCode)
                throw Failure;

            return 0;
        }
    }

    private sealed class ComparerCallbackException : Exception
    {
    }
}
