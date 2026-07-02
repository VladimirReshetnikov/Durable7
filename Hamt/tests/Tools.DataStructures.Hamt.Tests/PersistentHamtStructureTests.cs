using Xunit;
using IntSet = Tools.DataStructures.Hamt.PersistentHashSet<int>;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>
/// White-box tests for trie shape, node collapse, and cross-version structural sharing.
/// </summary>
public sealed class PersistentHamtStructureTests
{
    /// <summary>Verifies root shapes for the empty, single-key, and colliding-key cases.</summary>
    [Fact]
    public void RootShape_TracksContents()
    {
        var comparer = new ExplicitHashComparer();
        var empty = PersistentHashMap<ExplicitHashKey, string>.Create(comparer);
        Assert.Null(empty.RootForTesting);

        var single = empty.SetItem(new ExplicitHashKey(1, 0x10), "a");
        Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.LeafNode>(single.RootForTesting);

        var colliding = single.SetItem(new ExplicitHashKey(2, 0x10), "b");
        Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.CollisionNode>(colliding.RootForTesting);

        var branching = colliding.SetItem(new ExplicitHashKey(3, 0x11), "c");
        Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode>(branching.RootForTesting);
    }

    /// <summary>Verifies removal collapses collision buckets and deep single-child chains to a leaf.</summary>
    [Fact]
    public void Removal_CollapsesToLeaf()
    {
        var comparer = new ExplicitHashComparer();
        var a = new ExplicitHashKey(1, 0x10);
        var b = new ExplicitHashKey(2, 0x10);
        var bucket = PersistentHashMap<ExplicitHashKey, string>.Create(comparer)
            .SetItem(a, "a")
            .SetItem(b, "b");
        Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.LeafNode>(bucket.Remove(b).RootForTesting);

        var deepA = new ExplicitHashKey(3, 0);
        var deepB = new ExplicitHashKey(4, 1 << 30);
        var deep = PersistentHashMap<ExplicitHashKey, string>.Create(comparer)
            .SetItem(deepA, "a")
            .SetItem(deepB, "b");
        Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode>(deep.RootForTesting);
        Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.LeafNode>(deep.Remove(deepB).RootForTesting);
    }

    /// <summary>Verifies updates share untouched sibling subtrees by reference across versions.</summary>
    [Fact]
    public void Update_SharesUntouchedSubtrees()
    {
        var comparer = new ExplicitHashComparer();
        var a = new ExplicitHashKey(1, 0x00);
        var b = new ExplicitHashKey(2, 0x01);
        var c = new ExplicitHashKey(3, 0x21);

        var map = PersistentHashMap<ExplicitHashKey, string>.Create(comparer)
            .SetItem(a, "a")
            .SetItem(b, "b")
            .SetItem(c, "c");
        var updated = map.SetItem(a, "a2");

        var rootBefore = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode>(map.RootForTesting);
        var rootAfter = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode>(updated.RootForTesting);

        Assert.NotSame(rootBefore, rootAfter);
        Assert.NotSame(rootBefore.Children[0], rootAfter.Children[0]);
        Assert.Same(rootBefore.Children[1], rootAfter.Children[1]);
    }

    /// <summary>Verifies no-op updates preserve the root node instance.</summary>
    [Fact]
    public void NoOpUpdates_PreserveRoot()
    {
        var comparer = new ExplicitHashComparer();
        var a = new ExplicitHashKey(1, 0x00);
        var map = PersistentHashMap<ExplicitHashKey, string>.Create(comparer).SetItem(a, "a");

        Assert.Same(map.RootForTesting, map.SetItem(a, "a").RootForTesting);
        Assert.Same(map.RootForTesting, map.Remove(new ExplicitHashKey(2, 0x01)).RootForTesting);
    }

    /// <summary>Verifies collision-bucket arrays are shared when an update touches a sibling subtree.</summary>
    [Fact]
    public void Update_SharesUntouchedCollisionBucket()
    {
        var comparer = new ExplicitHashComparer();
        var bucketed1 = new ExplicitHashKey(1, 0x10);
        var bucketed2 = new ExplicitHashKey(2, 0x10);
        var sibling = new ExplicitHashKey(3, 0x11);

        var map = PersistentHashMap<ExplicitHashKey, string>.Create(comparer)
            .SetItem(bucketed1, "a")
            .SetItem(bucketed2, "b")
            .SetItem(sibling, "c");
        var updated = map.SetItem(sibling, "c2");

        var rootBefore = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode>(map.RootForTesting);
        var rootAfter = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode>(updated.RootForTesting);

        var bucketBefore = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.CollisionNode>(rootBefore.Children[0]);
        var bucketAfter = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.CollisionNode>(rootAfter.Children[0]);
        Assert.Same(bucketBefore, bucketAfter);
        Assert.Same(bucketBefore.Entries, bucketAfter.Entries);
    }

    /// <summary>Verifies the set wrapper reports the underlying map root.</summary>
    [Fact]
    public void SetRoot_TracksUnderlyingMap()
    {
        Assert.Null(IntSet.Empty.RootForTesting);
        Assert.NotNull(IntSet.Empty.Add(1).RootForTesting);
    }

    private readonly record struct ExplicitHashKey(int Id, int Hash);

    private sealed class ExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        public bool Equals(ExplicitHashKey x, ExplicitHashKey y) => x.Id == y.Id;

        public int GetHashCode(ExplicitHashKey obj) => obj.Hash;
    }
}
