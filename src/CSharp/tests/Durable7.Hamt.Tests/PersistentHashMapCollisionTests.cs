// Tests for the persistent hash map collision.

using Xunit;

namespace Durable7.Hamt.Tests;

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
        Assert.Equal(
            new[] { "a", "b", "c", "d" },
            map.Select(kv => kv.Value).OrderBy(x => x).ToArray());

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

    /// <summary>Verifies that a different-hash key splits a root collision bucket downward.</summary>
    [Fact]
    public void CollisionBucket_SplitsWhenDifferentHashKeyArrives()
    {
        var comparer = new ExplicitHashComparer();
        var a = new ExplicitHashKey(1, 0x10);
        var b = new ExplicitHashKey(2, 0x10);
        var c = new ExplicitHashKey(3, 0x30);

        var map = PersistentHashMap<ExplicitHashKey, string>.Create(comparer)
            .SetItem(a, "a")
            .SetItem(b, "b")
            .SetItem(c, "c");

        Assert.Equal(3, map.Count);
        Assert.Equal("a", map[a]);
        Assert.Equal("b", map[b]);
        Assert.Equal("c", map[c]);
        Assert.Equal(
            new[] { "a", "b", "c" },
            map.Select(kv => kv.Value).OrderBy(x => x).ToArray());
    }

    /// <summary>Verifies bucket behavior for probe hashes that share the bucket's trie path.</summary>
    [Fact]
    public void CollisionBucket_HashMismatchProbesMissAndSplitDeeply()
    {
        // Hash 0x410 shares the low ten bits of 0x10, so it follows the bucket's trie path while
        // remaining a different full hash.
        var comparer = new ExplicitHashComparer();
        var a = new ExplicitHashKey(1, 0x10);
        var b = new ExplicitHashKey(2, 0x10);
        var probe = new ExplicitHashKey(9, 0x410);

        var map = PersistentHashMap<ExplicitHashKey, string>.Create(comparer)
            .SetItem(a, "a")
            .SetItem(b, "b");

        Assert.False(map.ContainsKey(probe));
        Assert.Same(map, map.Remove(probe));

        var expanded = map.SetItem(probe, "p");
        Assert.Equal(3, expanded.Count);
        Assert.Equal("p", expanded[probe]);
        Assert.Equal("a", expanded[a]);
        Assert.Equal("b", expanded[b]);
        Assert.Equal(
            new[] { "a", "b", "p" },
            expanded.Select(kv => kv.Value).OrderBy(x => x).ToArray());

        var reduced = expanded.Remove(probe);
        Assert.Equal(2, reduced.Count);
        Assert.Equal("a", reduced[a]);
        Assert.Equal("b", reduced[b]);
    }

    /// <summary>Verifies bucket updates preserve instance identity and stored key objects.</summary>
    [Fact]
    public void CollisionBucket_EqualValueKeepsInstanceAndReplaceKeepsKeyObject()
    {
        var comparer = new ConstantHashIgnoreCaseComparer();
        var storedKey = new string(['A', 'l', 'p', 'h', 'a']);
        var map = PersistentHashMap<string, int>.Create(comparer)
            .SetItem(storedKey, 1)
            .SetItem("beta", 2);

        Assert.Same(map, map.SetItem("ALPHA", 1));

        var replaced = map.SetItem("ALPHA", 3);
        Assert.Equal(3, replaced[storedKey]);
        Assert.Same(storedKey, replaced.Single(kv => kv.Value == 3).Key);

        Assert.True(replaced.TryGetKey("alpha", out var actualKey));
        Assert.Same(storedKey, actualKey);
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

        var enumerated = map.ToArray();
        Assert.Equal(model.Count, enumerated.Length);
        foreach (var (key, value) in enumerated)
        {
            Assert.True(model.TryGetValue(key, out var expected));
            Assert.Equal(expected, value);
        }
    }

    private readonly record struct CollisionKey(int Id);

    private sealed class ConstantHashComparer : IEqualityComparer<CollisionKey>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(CollisionKey x, CollisionKey y) => x.Id == y.Id;

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(CollisionKey obj) => 0x12345;
    }

    private sealed class ConstantHashIgnoreCaseComparer : IEqualityComparer<string>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(string? x, string? y) => StringComparer.OrdinalIgnoreCase.Equals(x, y);

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(string obj) => 0x77;
    }

    private readonly record struct ExplicitHashKey(int Id, int Hash);

    private sealed class ExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(ExplicitHashKey x, ExplicitHashKey y) => x.Id == y.Id;

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(ExplicitHashKey obj) => obj.Hash;
    }
}
