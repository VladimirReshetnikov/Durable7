using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>White-box coverage for mutable unpublished HAMT bulk construction.</summary>
public sealed class PersistentHashMapBulkBuilderTests
{
    /// <summary>Verifies frozen maps are detached from every later mutation of the builder.</summary>
    [Fact]
    public void FrozenSnapshots_RemainImmutableAcrossBuilderMutations()
    {
        var comparer = new ExplicitHashComparer();
        var first = new ExplicitHashKey(1, 0x10);
        var collision = new ExplicitHashKey(2, 0x10);
        var branch = new ExplicitHashKey(3, 0x11);
        var builder = PersistentHashMap<ExplicitHashKey, string>.CreateBulkBuilder(comparer);

        builder.SetItem(first, "first");
        var leafSnapshot = builder.ToImmutable();
        builder.SetItem(collision, "collision");
        var collisionSnapshot = builder.ToImmutable();
        builder.SetItem(branch, "branch");
        builder.SetItem(first, "updated");
        var branchSnapshot = builder.ToImmutable();

        Assert.Equal("first", leafSnapshot[first]);
        Assert.False(leafSnapshot.ContainsKey(collision));
        Assert.Equal("first", collisionSnapshot[first]);
        Assert.Equal("collision", collisionSnapshot[collision]);
        Assert.False(collisionSnapshot.ContainsKey(branch));
        Assert.Equal("updated", branchSnapshot[first]);
        Assert.Equal("collision", branchSnapshot[collision]);
        Assert.Equal("branch", branchSnapshot[branch]);
    }

    /// <summary>Verifies last-wins values and first-equivalent key/value retention during a bulk build.</summary>
    [Fact]
    public void EquivalentKeys_RetainFirstKeyAndEqualValueInstances()
    {
        var builder = PersistentHashMap<string, string>.CreateBulkBuilder(StringComparer.OrdinalIgnoreCase);
        var storedKey = new string(['A', 'l', 'p', 'h', 'a']);
        var storedValue = new string(['v', 'a', 'l', 'u', 'e']);
        var equalValue = new string(['v', 'a', 'l', 'u', 'e']);

        builder.SetItem(storedKey, storedValue);
        builder.SetItem("ALPHA", equalValue);
        var equalSnapshot = builder.ToImmutable();
        builder.SetItem("alpha", "replacement");
        var replacedSnapshot = builder.ToImmutable();

        Assert.Equal(1, builder.Count);
        Assert.True(equalSnapshot.TryGetKey("alpha", out var actualKey));
        Assert.Same(storedKey, actualKey);
        Assert.Same(storedValue, equalSnapshot["alpha"]);
        Assert.Same(storedKey, replacedSnapshot.Single().Key);
        Assert.Equal("replacement", replacedSnapshot["ALPHA"]);
    }

    /// <summary>Verifies null keys and final-level hash branching use the ordinary map contracts.</summary>
    [Fact]
    public void NullAndDeepPrefixKeys_RoundTripThroughFrozenMap()
    {
        var nullable = PersistentHashMap<string?, int>.CreateBulkBuilder();
        nullable.SetItem(null, 1);
        nullable.SetItem("value", 2);
        var nullableMap = nullable.ToImmutable();
        Assert.Equal(1, nullableMap[null]);
        Assert.Equal(2, nullableMap["value"]);

        var comparer = new ExplicitHashComparer();
        var low = new ExplicitHashKey(1, 0);
        var high = new ExplicitHashKey(2, 1 << 30);
        var deep = PersistentHashMap<ExplicitHashKey, int>.CreateBulkBuilder(comparer);
        deep.SetItem(low, 10);
        deep.SetItem(high, 20);
        var deepMap = deep.ToImmutable();
        Assert.Equal(10, deepMap[low]);
        Assert.Equal(20, deepMap[high]);
    }

    /// <summary>Checks a large collision-heavy bulk build against ordinary persistent updates.</summary>
    [Fact]
    public void RandomizedBuild_MatchesPersistentUpdateShapeAndContents()
    {
        var comparer = new ExplicitHashComparer();
        var builder = PersistentHashMap<ExplicitHashKey, int>.CreateBulkBuilder(comparer);
        var persistent = PersistentHashMap<ExplicitHashKey, int>.Create(comparer);
        var random = new Random(20260710);

        for (var i = 0; i < 10_000; i++)
        {
            var id = random.Next(2_000);
            var hash = id % 4 == 0 ? id & 31 : unchecked(id * 0x01010101);
            var key = new ExplicitHashKey(id, hash);
            var value = random.Next();
            builder.SetItem(key, value);
            persistent = persistent.SetItem(key, value);
        }

        var built = builder.ToImmutable();
        Assert.Equal(persistent.Count, built.Count);
        Assert.Equal(persistent.ToArray(), built.ToArray());
    }

    private readonly record struct ExplicitHashKey(int Id, int Hash);

    private sealed class ExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        public bool Equals(ExplicitHashKey x, ExplicitHashKey y) => x.Id == y.Id;

        public int GetHashCode(ExplicitHashKey obj) => obj.Hash;
    }
}
