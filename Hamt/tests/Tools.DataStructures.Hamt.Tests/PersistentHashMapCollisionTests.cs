using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>
/// Tests HAMT behavior under adversarial hash layouts: equal full hashes and very deep shared prefixes.
/// </summary>
public sealed class PersistentHashMapCollisionTests
{
    /// <summary>Verifies linear collision buckets for many unequal keys with the same full hash.</summary>
    [Fact]
    public void EqualHashCollisionBucket_PreservesEveryKey()
    {
        var comparer = new ConstantHashComparer();
        var map = PersistentHashMap<CollisionKey, int>.Create(comparer);
        var model = new Dictionary<CollisionKey, int>(comparer);

        for (var i = 0; i < 100; i++)
        {
            var key = new CollisionKey(i);
            map = map.SetItem(key, i * 10);
            model[key] = i * 10;
        }

        for (var i = 0; i < 100; i += 3)
        {
            var key = new CollisionKey(i);
            map = map.Remove(key);
            model.Remove(key);
        }

        for (var i = 1; i < 100; i += 4)
        {
            var key = new CollisionKey(i);
            map = map.SetItem(key, -i);
            model[key] = -i;
        }

        AssertMatches(model, map);
    }

    /// <summary>Verifies branching at the final 32-bit hash group and collapse after removal.</summary>
    [Fact]
    public void DeepSharedHashPrefixes_LookupAndRemoveCorrectly()
    {
        var comparer = new ExplicitHashComparer();
        var a = new ExplicitHashKey(1, 0);
        var b = new ExplicitHashKey(2, 1 << 30);
        var c = new ExplicitHashKey(3, unchecked((int)0x80000000));
        var d = new ExplicitHashKey(4, unchecked((int)0xC0000000));

        var map = PersistentHashMap<ExplicitHashKey, string>.Create(comparer)
            .SetItem(a, "a")
            .SetItem(b, "b")
            .SetItem(c, "c")
            .SetItem(d, "d");

        Assert.Equal("a", map[a]);
        Assert.Equal("b", map[b]);
        Assert.Equal("c", map[c]);
        Assert.Equal("d", map[d]);

        var reduced = map.Remove(b).Remove(c).Remove(d);
        Assert.Single(reduced);
        Assert.Equal("a", reduced[a]);
        Assert.False(reduced.ContainsKey(b));
        Assert.Equal("b", map[b]);
    }

    /// <summary>Verifies collision-bucket removal reports the removed value and preserves older versions.</summary>
    [Fact]
    public void CollisionRemoval_ReportsValueAndPreservesOriginal()
    {
        var comparer = new ConstantHashComparer();
        var first = new CollisionKey(1);
        var second = new CollisionKey(2);
        var map = PersistentHashMap<CollisionKey, string>.Create(comparer)
            .SetItem(first, "first")
            .SetItem(second, "second");

        Assert.True(map.TryRemove(first, out var removed, out var value));
        Assert.Equal("first", value);
        Assert.False(removed.ContainsKey(first));
        Assert.Equal("second", removed[second]);
        Assert.Equal("first", map[first]);
    }

    private static void AssertMatches<TKey, TValue>(
        Dictionary<TKey, TValue> model,
        PersistentHashMap<TKey, TValue> map)
        where TKey : notnull
    {
        Assert.Equal(model.Count, map.Count);
        foreach (var (key, expected) in model)
        {
            Assert.True(map.TryGetValue(key, out var actual));
            Assert.Equal(expected, actual);
        }
    }

    private readonly record struct CollisionKey(int Id);

    private sealed class ConstantHashComparer : IEqualityComparer<CollisionKey>
    {
        public bool Equals(CollisionKey x, CollisionKey y) => x.Id == y.Id;

        public int GetHashCode(CollisionKey obj) => 0x12345;
    }

    private readonly record struct ExplicitHashKey(int Id, int Hash);

    private sealed class ExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        public bool Equals(ExplicitHashKey x, ExplicitHashKey y) => x.Id == y.Id;

        public int GetHashCode(ExplicitHashKey obj) => obj.Hash;
    }
}
