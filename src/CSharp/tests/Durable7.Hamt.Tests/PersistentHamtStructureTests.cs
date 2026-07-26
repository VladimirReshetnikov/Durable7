// Tests for the persistent hash-array mapped trie structure.

using Xunit;
using IntSet = Durable7.Hamt.PersistentHashSet<int>;

namespace Durable7.Hamt.Tests;

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

    /// <summary>Verifies independent insertion histories produce the same canonical CHAMP topology.</summary>
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
        Assert.Empty(forward.Diff(reverse));
        AssertCanonicalTopologyEqual(forward.RootForTesting, reverse.RootForTesting);

        var removedAndRestored = forward.Remove(items[42].Key).SetItem(items[42].Key, items[42].Value);
        Assert.True(forward.MapEquals(removedAndRestored));
        Assert.Empty(forward.Diff(removedAndRestored));
        AssertCanonicalTopologyEqual(forward.RootForTesting, removedAndRestored.RootForTesting);
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

    /// <summary>Verifies diff follows CHAMP slots through leaf, collision, and branch transitions.</summary>
    [Fact]
    public void Diff_HandlesEveryNodeShapeTransition()
    {
        var comparer = new ExplicitHashComparer();
        var a = new ExplicitHashKey(1, 0);
        var b = new ExplicitHashKey(2, 1 << 30);
        var collision = new ExplicitHashKey(3, 0);
        var sibling = new ExplicitHashKey(4, 1);
        var empty = PersistentHashMap<ExplicitHashKey, string>.Create(comparer);
        var versions = new[]
        {
            empty,
            empty.SetItem(a, "a"),
            empty.SetItem(a, "a").SetItem(b, "b"),
            empty.SetItem(a, "a").SetItem(collision, "c"),
            empty.SetItem(a, "a").SetItem(collision, "c").SetItem(sibling, "s"),
            empty.SetItem(a, "A").SetItem(collision, "c2").SetItem(b, "b").SetItem(sibling, "s"),
        };

        foreach (var source in versions)
        {
            foreach (var target in versions)
                AssertDiffMatchesModel(source, target);
        }
    }

    /// <summary>Verifies diff prunes all unchanged branches in a lineage-related map.</summary>
    [Fact]
    public void Diff_PrunesReferenceEqualSubtries()
    {
        var source = PersistentHashMap<int, string>.CreateRange(
            Enumerable.Range(0, 4_096).Select(key => KeyValuePair.Create(key, $"v{key}")));
        var target = source.SetItem(2_048, "changed");
        var values = new CountingStringComparer();

        var difference = Assert.Single(source.Diff(target, values));

        Assert.Equal(MapDifferenceKind.Changed, difference.Kind);
        Assert.Equal(2_048, difference.Key);
        Assert.Equal("v2048", difference.OldValue);
        Assert.Equal("changed", difference.NewValue);
        Assert.InRange(values.ComparisonCount, 1, 32);
    }

    /// <summary>Verifies argument failures occur when Diff is called, not when its result is consumed.</summary>
    [Fact]
    public void Diff_ValidatesArgumentsEagerly()
    {
        var firstComparer = new ExplicitHashComparer();
        var secondComparer = new ExplicitHashComparer();
        var first = PersistentHashMap<ExplicitHashKey, string>.Create(firstComparer);
        var second = PersistentHashMap<ExplicitHashKey, string>.Create(secondComparer);

        Assert.Throws<ArgumentNullException>(() => first.Diff(null!));
        Assert.Throws<ArgumentException>(() => first.Diff(second));
    }

    /// <summary>Verifies diff exposes the documented source and target key representatives.</summary>
    [Fact]
    public void Diff_PreservesStoredKeyRepresentatives()
    {
        var sourceKey = new string(['A', 'l', 'p', 'h', 'a']);
        var targetKey = new string(['A', 'L', 'P', 'H', 'A']);
        var addedKey = new string(['B', 'e', 't', 'a']);
        var source = PersistentHashMap<string, int>.Create(StringComparer.OrdinalIgnoreCase)
            .SetItem(sourceKey, 1);
        var target = PersistentHashMap<string, int>.Create(StringComparer.OrdinalIgnoreCase)
            .SetItem(targetKey, 2)
            .SetItem(addedKey, 3);

        var differences = source.Diff(target).ToArray();
        var changed = Assert.Single(differences, difference => difference.Kind == MapDifferenceKind.Changed);
        var added = Assert.Single(differences, difference => difference.Kind == MapDifferenceKind.Added);

        Assert.Same(sourceKey, changed.Key);
        Assert.Same(addedKey, added.Key);
    }

    /// <summary>Checks structural diff against independent randomized dictionary histories.</summary>
    [Fact]
    public void Diff_RandomizedIndependentHistoriesMatchModel()
    {
        var comparer = new ExplicitHashComparer();
        var source = PersistentHashMap<ExplicitHashKey, string>.Create(comparer);
        var target = PersistentHashMap<ExplicitHashKey, string>.Create(comparer);
        var random = new Random(0x4348414D);

        for (var step = 0; step < 5_000; step++)
        {
            var keyId = random.Next(128);
            var key = new ExplicitHashKey(keyId, HashFor(keyId));
            var updateSource = random.Next(2) == 0;
            if (random.Next(4) == 0)
            {
                if (updateSource)
                    source = source.Remove(key);
                else
                    target = target.Remove(key);
            }
            else
            {
                var value = $"v{random.Next(32)}";
                if (updateSource)
                    source = source.SetItem(key, value);
                else
                    target = target.SetItem(key, value);
            }

            if (step % 31 == 0)
            {
                AssertValidStructure(source);
                AssertValidStructure(target);
                AssertDiffMatchesModel(source, target);
                AssertDiffMatchesModel(target, source);
            }
        }

        AssertValidStructure(source);
        AssertValidStructure(target);
        AssertDiffMatchesModel(source, target);
        AssertDiffMatchesModel(target, source);
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

    private sealed class CountingStringComparer : IEqualityComparer<string>
    {
        public int ComparisonCount { get; private set; }

        public bool Equals(string? x, string? y)
        {
            ComparisonCount++;
            return StringComparer.Ordinal.Equals(x, y);
        }

        public int GetHashCode(string obj) => StringComparer.Ordinal.GetHashCode(obj);
    }

    private static int HashFor(int keyId) => keyId % 11 == 0
        ? 0x13579BDF
        : unchecked((keyId * 0x01010101) ^ ((keyId & 3) << 30));

    private static void AssertDiffMatchesModel(
        PersistentHashMap<ExplicitHashKey, string> source,
        PersistentHashMap<ExplicitHashKey, string> target)
    {
        var sourceModel = source.ToDictionary(entry => entry.Key.Id, entry => entry.Value);
        var targetModel = target.ToDictionary(entry => entry.Key.Id, entry => entry.Value);
        var actual = source.Diff(target).ToDictionary(difference => difference.Key.Id);
        var expectedKeys = sourceModel.Keys.Union(targetModel.Keys)
            .Where(key => !sourceModel.TryGetValue(key, out var oldValue)
                || !targetModel.TryGetValue(key, out var newValue)
                || oldValue != newValue)
            .ToHashSet();

        Assert.Equal(expectedKeys, actual.Keys.ToHashSet());
        foreach (var key in expectedKeys)
        {
            var hasOldValue = sourceModel.TryGetValue(key, out var oldValue);
            var hasNewValue = targetModel.TryGetValue(key, out var newValue);
            var difference = actual[key];
            Assert.Equal(
                hasOldValue && hasNewValue
                    ? MapDifferenceKind.Changed
                    : hasOldValue ? MapDifferenceKind.Removed : MapDifferenceKind.Added,
                difference.Kind);
            Assert.Equal(hasOldValue ? oldValue : null, difference.OldValue);
            Assert.Equal(hasNewValue ? newValue : null, difference.NewValue);
        }
    }

    private static void AssertValidStructure(PersistentHashMap<ExplicitHashKey, string> map)
    {
        var entries = ValidateNode(map.RootForTesting, shift: 0);
        Assert.Equal(map.Count, entries.Count);
        Assert.Equal(map.Count, entries.Select(entry => entry.Key.Id).Distinct().Count());
        foreach (var entry in entries)
            Assert.Equal(entry.Value, map[entry.Key]);
    }

    private static List<(uint Hash, ExplicitHashKey Key, string Value)> ValidateNode(
        PersistentHashMap<ExplicitHashKey, string>.Node? node,
        int shift)
    {
        switch (node)
        {
            case null:
                return [];
            case PersistentHashMap<ExplicitHashKey, string>.LeafNode leaf:
                return [(leaf.Hash, leaf.Key, leaf.Value)];
            case PersistentHashMap<ExplicitHashKey, string>.CollisionNode collision:
                Assert.True(collision.Entries.Length >= 2);
                Assert.All(collision.Entries, entry => Assert.Equal(collision.Hash, entry.Hash));
                Assert.Equal(collision.Entries.Length,
                    collision.Entries.Select(entry => entry.Key.Id).Distinct().Count());
                return collision.Entries.Select(entry => (entry.Hash, entry.Key, entry.Value)).ToList();
            case PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode branch:
            {
                Assert.InRange(shift, 0, 30);
                Assert.Equal(0u, branch.DataMap & branch.NodeMap);
                Assert.Equal(System.Numerics.BitOperations.PopCount(branch.DataMap), branch.Data.Length);
                Assert.Equal(System.Numerics.BitOperations.PopCount(branch.NodeMap), branch.Children.Length);
                Assert.True(branch.Data.Length + branch.Children.Length >= 2
                    || branch.Children.SingleOrDefault() is PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode);
                Assert.DoesNotContain(branch.Children,
                    child => child is PersistentHashMap<ExplicitHashKey, string>.LeafNode);

                List<(uint Hash, ExplicitHashKey Key, string Value)> entries = [];
                for (var index = 0; index < 32; index++)
                {
                    var bit = 1u << index;
                    if ((branch.DataMap & bit) != 0)
                    {
                        var dataSlot = System.Numerics.BitOperations.PopCount(branch.DataMap & (bit - 1));
                        var entry = branch.Data[dataSlot];
                        Assert.Equal(index, (int)((entry.Hash >> shift) & 31));
                        entries.Add((entry.Hash, entry.Key, entry.Value));
                    }

                    if ((branch.NodeMap & bit) == 0)
                        continue;

                    var nodeSlot = System.Numerics.BitOperations.PopCount(branch.NodeMap & (bit - 1));
                    var childEntries = ValidateNode(branch.Children[nodeSlot], shift + 5);
                    Assert.All(childEntries, entry => Assert.Equal(index, (int)((entry.Hash >> shift) & 31)));
                    entries.AddRange(childEntries);
                }

                return entries;
            }
            default:
                throw new InvalidOperationException($"Unknown CHAMP node type {node.GetType()}.");
        }
    }

    private static void AssertCanonicalTopologyEqual(
        PersistentHashMap<ExplicitHashKey, string>.Node? left,
        PersistentHashMap<ExplicitHashKey, string>.Node? right)
    {
        Assert.Equal(left?.GetType(), right?.GetType());
        switch (left)
        {
            case null:
                return;
            case PersistentHashMap<ExplicitHashKey, string>.LeafNode leftLeaf:
            {
                var rightLeaf = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.LeafNode>(right);
                Assert.Equal((leftLeaf.Hash, leftLeaf.Key, leftLeaf.Value),
                    (rightLeaf.Hash, rightLeaf.Key, rightLeaf.Value));
                return;
            }
            case PersistentHashMap<ExplicitHashKey, string>.CollisionNode leftCollision:
            {
                var rightCollision = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.CollisionNode>(right);
                Assert.Equal(leftCollision.Hash, rightCollision.Hash);
                Assert.Equal(
                    leftCollision.Entries.Select(entry => (entry.Key, entry.Value)).OrderBy(entry => entry.Key.Id),
                    rightCollision.Entries.Select(entry => (entry.Key, entry.Value)).OrderBy(entry => entry.Key.Id));
                return;
            }
            case PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode leftBranch:
            {
                var rightBranch = Assert.IsType<PersistentHashMap<ExplicitHashKey, string>.BitmapIndexedNode>(right);
                Assert.Equal(leftBranch.DataMap, rightBranch.DataMap);
                Assert.Equal(leftBranch.NodeMap, rightBranch.NodeMap);
                Assert.Equal(leftBranch.Data.Select(entry => (entry.Hash, entry.Key, entry.Value)),
                    rightBranch.Data.Select(entry => (entry.Hash, entry.Key, entry.Value)));
                Assert.Equal(leftBranch.Children.Length, rightBranch.Children.Length);
                for (var i = 0; i < leftBranch.Children.Length; i++)
                    AssertCanonicalTopologyEqual(leftBranch.Children[i], rightBranch.Children[i]);
                return;
            }
        }
    }
}
