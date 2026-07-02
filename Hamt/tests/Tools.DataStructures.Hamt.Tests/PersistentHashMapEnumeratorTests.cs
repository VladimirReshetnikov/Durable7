using Xunit;
using Map = Tools.DataStructures.Hamt.PersistentHashMap<int, string>;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>
/// Direct contract tests for <see cref="PersistentHashMap{TKey, TValue}.Enumerator"/>.
/// </summary>
public sealed class PersistentHashMapEnumeratorTests
{
    /// <summary>Verifies a default-initialized enumerator behaves as an exhausted enumerator.</summary>
    [Fact]
    public void DefaultEnumerator_ReportsNoEntries()
    {
        var enumerator = default(Map.Enumerator);

        Assert.Equal(default, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
    }

    /// <summary>Verifies position reporting before the first entry and after the final entry.</summary>
    [Fact]
    public void MoveNext_BracketsEntriesWithDefaultCurrent()
    {
        var map = Map.Empty.SetItem(1, "one");
        var enumerator = map.GetEnumerator();

        Assert.Equal(default, enumerator.Current);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(new KeyValuePair<int, string>(1, "one"), enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.Equal(default, enumerator.Current);
        Assert.False(enumerator.MoveNext());
    }

    /// <summary>Verifies the interface enumerator rejects <c>Reset</c>.</summary>
    [Fact]
    public void Reset_ThrowsNotSupported()
    {
        var map = Map.Empty.SetItem(1, "one");
        using var enumerator = ((IEnumerable<KeyValuePair<int, string>>)map).GetEnumerator();

        Assert.Throws<NotSupportedException>(enumerator.Reset);
    }

    /// <summary>Verifies a copied enumerator advances independently of the original.</summary>
    [Fact]
    public void CopiedEnumerator_AdvancesIndependently()
    {
        var map = Map.Empty.SetItem(0, "zero").SetItem(1, "one").SetItem(33, "thirty-three");
        var expected = map.Select(kv => kv.Key).ToArray();

        var original = map.GetEnumerator();
        Assert.True(original.MoveNext());
        var copy = original;

        var fromOriginal = new List<int> { original.Current.Key };
        while (original.MoveNext())
            fromOriginal.Add(original.Current.Key);

        var fromCopy = new List<int> { copy.Current.Key };
        while (copy.MoveNext())
            fromCopy.Add(copy.Current.Key);

        Assert.Equal(expected, fromOriginal);
        Assert.Equal(expected, fromCopy);
    }

    /// <summary>Verifies enumeration through collision buckets mixed with ordinary leaves.</summary>
    [Fact]
    public void CollisionBuckets_AreEnumeratedCompletely()
    {
        var comparer = new FewBucketsComparer();
        var model = new Dictionary<int, string>(comparer);
        var map = PersistentHashMap<int, string>.Create(comparer);
        for (var i = 0; i < 40; i++)
        {
            map = map.SetItem(i, $"v{i}");
            model[i] = $"v{i}";
        }

        Assert.Equal(
            model.OrderBy(kv => kv.Key).ToArray(),
            map.OrderBy(kv => kv.Key).ToArray());
    }

    /// <summary>Verifies enumeration of a map whose root is a single collision bucket.</summary>
    [Fact]
    public void RootCollisionBucket_IsEnumeratedCompletely()
    {
        var comparer = new FewBucketsComparer();
        var map = PersistentHashMap<int, string>.Create(comparer)
            .SetItem(0, "a")
            .SetItem(4, "b")
            .SetItem(8, "c");

        Assert.Equal(new[] { "a", "b", "c" }, map.Select(kv => kv.Value).OrderBy(x => x).ToArray());
    }

    /// <summary>Verifies enumeration of a full-depth trie whose keys differ only at the final level.</summary>
    [Fact]
    public void DeepTrie_IsEnumeratedCompletely()
    {
        var comparer = new HighBitsComparer();
        var map = PersistentHashMap<int, string>.Create(comparer)
            .SetItem(0, "a")
            .SetItem(1 << 30, "b")
            .SetItem(unchecked((int)0x80000000), "c")
            .SetItem(unchecked((int)0xC0000000), "d");

        Assert.Equal(new[] { "a", "b", "c", "d" }, map.Select(kv => kv.Value).OrderBy(x => x).ToArray());
        Assert.Equal(4, map.Count);
    }

    /// <summary>Verifies enumeration order is stable for an unchanged version.</summary>
    [Fact]
    public void EnumerationOrder_IsStableAcrossEnumerations()
    {
        var map = Map.Empty.SetItem(3, "three").SetItem(11, "eleven").SetItem(200, "two hundred");

        Assert.Equal(map.ToArray(), map.ToArray());
    }

    private sealed class FewBucketsComparer : IEqualityComparer<int>
    {
        public bool Equals(int x, int y) => x == y;

        public int GetHashCode(int obj) => obj & 3;
    }

    private sealed class HighBitsComparer : IEqualityComparer<int>
    {
        public bool Equals(int x, int y) => x == y;

        public int GetHashCode(int obj) => obj;
    }
}
