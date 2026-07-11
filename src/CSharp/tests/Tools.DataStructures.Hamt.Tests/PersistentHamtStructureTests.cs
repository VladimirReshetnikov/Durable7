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
        Assert.NotSame(rootBefore.Data, rootAfter.Data);
        Assert.Same(rootBefore.Children, rootAfter.Children);
        Assert.Same(rootBefore.Children[0], rootAfter.Children[0]);
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

    /// <summary>Verifies independent insertion histories produce the same canonical CHAMP shape.</summary>
    [Fact]
    public void IndependentHistories_ProduceCanonicalShapeAndEquality()
    {
        var comparer = new ExplicitHashComparer();
        var items = Enumerable.Range(0, 100)
            .Select(i => new KeyValuePair<ExplicitHashKey, string>(
                new ExplicitHashKey(i, i % 9 == 0 ? 17 : unchecked(i * 0x01010101)),
                $"v{i}"))
            .ToArray();

        var forward = PersistentHashMap<ExplicitHashKey, string>.CreateRange(items, comparer);
        var reverse = PersistentHashMap<ExplicitHashKey, string>.CreateRange(items.Reverse(), comparer);

        Assert.True(forward.MapEquals(reverse));
        AssertCanonicalShapeEqual(forward.RootForTesting, reverse.RootForTesting);

        var removedAndRestored = forward.Remove(items[42].Key).SetItem(items[42].Key, items[42].Value);
        Assert.True(forward.MapEquals(removedAndRestored));
        AssertCanonicalShapeEqual(forward.RootForTesting, removedAndRestored.RootForTesting);
    }

    /// <summary>Verifies diff classifies additions, removals, changes, and shared-root no-ops.</summary>
    [Fact]
    public void Diff_ReportsSemanticChanges()
    {
        var source = PersistentHashMap<int, string>.Empty
            .SetItem(1, "one")
            .SetItem(2, "two")
            .SetItem(3, "three");
        var target = source.Remove(1).SetItem(2, "TWO").SetItem(4, "four");

        Assert.Empty(source.Diff(source));
        var changes = source.Diff(target).ToDictionary(change => change.Key);
        Assert.Equal(MapDifferenceKind.Removed, changes[1].Kind);
        Assert.Equal(MapDifferenceKind.Changed, changes[2].Kind);
        Assert.Equal(MapDifferenceKind.Added, changes[4].Kind);
        Assert.Equal("two", changes[2].OldValue);
        Assert.Equal("TWO", changes[2].NewValue);
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

    private static void AssertCanonicalShapeEqual(
        PersistentHashMap<ExplicitHashKey, string>.Node? left,
        PersistentHashMap<ExplicitHashKey, string>.Node? right)
    {
        Assert.Equal(left?.GetType(), right?.GetType());
        if (left is PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode leftBranch)
        {
            var rightBranch = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode>(right);
            Assert.Equal(leftBranch.DataMap, rightBranch.DataMap);
            Assert.Equal(leftBranch.NodeMap, rightBranch.NodeMap);
            Assert.Equal(leftBranch.Data.Select(entry => (entry.Hash, entry.Key, entry.Value)),
                rightBranch.Data.Select(entry => (entry.Hash, entry.Key, entry.Value)));
            Assert.Equal(leftBranch.Children.Length, rightBranch.Children.Length);
            for (var i = 0; i < leftBranch.Children.Length; i++)
                AssertCanonicalShapeEqual(leftBranch.Children[i], rightBranch.Children[i]);
        }
    }
}
