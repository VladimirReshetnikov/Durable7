using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Canonical encoding, shape, digest, diff, persistence, and model coverage for <see cref="MerkleSearchTree{TKey,TValue}"/>.</summary>
public sealed class MerkleSearchTreeTests
{
    /// <summary>Verifies independent policies and histories produce identical content addresses and shapes.</summary>
    [Fact]
    public void IndependentProcessesAndHistories_ConvergeOnContentAddress()
    {
        var policy1 = IntStringPolicy();
        var policy2 = IntStringPolicy();
        var entries = Enumerable.Range(-500, 1_000)
            .Select(key => KeyValuePair.Create<int, string?>(key, $"v:{key}"))
            .ToArray();
        var forward = MerkleSearchTree<int, string?>.CreateRange(entries, policy1);
        var reverse = MerkleSearchTree<int, string?>.CreateRange(entries.Reverse(), policy2);

        Assert.Equal(policy1.DomainDigest, policy2.DomainDigest);
        Assert.Equal(forward.RootHash, reverse.RootHash);
        Assert.Equal(forward.ShapeForTesting(), reverse.ShapeForTesting());
        Assert.True(forward.ContentEquals(reverse));
        Assert.True(forward.MapEquals(reverse));
        Assert.Equal(64, forward.RootHash.ToString().Length);

        var restored = forward.Remove(42).SetItem(42, "v:42");
        Assert.Equal(forward.RootHash, restored.RootHash);
        Assert.Equal(forward.ShapeForTesting(), restored.ShapeForTesting());
    }

    /// <summary>Locks down canonical built-in codec byte order and null tagging.</summary>
    [Fact]
    public void BuiltInCodecs_AreCanonicalAndOwned()
    {
        Assert.Equal(new byte[] { 0x01, 0x02, 0x03, 0x04 }, MerkleCodecs.Int32.Encode(0x01020304));
        Assert.Equal(new byte[] { 0xff, 0xff, 0xff, 0xff }, MerkleCodecs.Int32.Encode(-1));
        Assert.Equal(new byte[] { 0 }, MerkleCodecs.Utf8String.Encode(null));
        Assert.Equal(new byte[] { 1, 0x68, 0x69 }, MerkleCodecs.Utf8String.Encode("hi"));

        byte[] source = [1, 2, 3];
        var encoded = MerkleCodecs.Bytes.Encode(source);
        source[0] = 9;
        Assert.Equal(new byte[] { 1, 1, 2, 3 }, encoded);
    }

    /// <summary>Verifies digest-pruned diff classifies changes and shared roots return immediately.</summary>
    [Fact]
    public void Diff_ClassifiesAddedRemovedAndChangedEntries()
    {
        var source = MerkleSearchTree<int, string?>.CreateRange(
            new[] { KeyValuePair.Create<int, string?>(1, "one"), KeyValuePair.Create<int, string?>(2, "two"), KeyValuePair.Create<int, string?>(3, "three") },
            IntStringPolicy());
        var target = source.Remove(1).SetItem(2, "TWO").SetItem(4, "four");

        Assert.Empty(source.Diff(source));
        var differences = source.Diff(target).ToDictionary(change => change.Key);
        Assert.Equal(MerkleMapDifferenceKind.Removed, differences[1].Kind);
        Assert.Equal(MerkleMapDifferenceKind.Changed, differences[2].Kind);
        Assert.Equal(MerkleMapDifferenceKind.Added, differences[4].Kind);
        Assert.Equal("two", differences[2].OldValue);
        Assert.Equal("TWO", differences[2].NewValue);
    }

    /// <summary>Checks randomized histories and retained snapshots against a sorted dictionary model.</summary>
    [Fact]
    public void RandomizedHistory_MatchesSortedDictionaryModel()
    {
        var random = new Random(20260722);
        var tree = MerkleSearchTree<int, string?>.Create(IntStringPolicy());
        var model = new SortedDictionary<int, string?>();
        var snapshots = new List<(MerkleSearchTree<int, string?>, KeyValuePair<int, string?>[])>();
        for (var i = 0; i < 30_000; i++)
        {
            var key = random.Next(-5_000, 5_001);
            if (random.Next(4) == 0)
            {
                tree = tree.Remove(key);
                model.Remove(key);
            }
            else
            {
                var value = random.Next(20) == 0 ? null : $"{i}:{random.Next()}";
                tree = tree.SetItem(key, value);
                model[key] = value;
            }
            if (i % 2_003 == 0)
                snapshots.Add((tree, [.. model]));
        }

        Assert.Equal(model, tree);
        Assert.InRange(tree.Height, 1, 80);
        foreach (var (snapshot, expected) in snapshots)
            Assert.Equal(expected, snapshot);
    }

    /// <summary>Verifies sorted range enumeration and no-op identity.</summary>
    [Fact]
    public void RangeAndNoOpContracts()
    {
        var tree = MerkleSearchTree<int, string?>.CreateRange(
            Enumerable.Range(-100, 201).Select(key => KeyValuePair.Create<int, string?>(key, key.ToString())),
            IntStringPolicy());
        Assert.Equal(Enumerable.Range(-10, 21), tree.EnumerateRange(-10, 10).Select(entry => entry.Key));
        Assert.Throws<ArgumentException>(() => tree.EnumerateRange(1, -1));
        Assert.Same(tree, tree.SetItem(0, "0"));
        Assert.Same(tree, tree.Remove(1_000));
        var empty = tree.Clear();
        Assert.True(empty.IsEmpty);
        Assert.Same(empty, empty.Clear());
    }

    /// <summary>Verifies policy domain mismatches prevent diff and content equality.</summary>
    [Fact]
    public void PolicyDomain_IsPartOfTheContentAddress()
    {
        var first = MerkleSearchTree<int, string?>.Create(IntStringPolicy("app-map-v1")).SetItem(1, "one");
        var second = MerkleSearchTree<int, string?>.Create(IntStringPolicy("app-map-v2")).SetItem(1, "one");
        Assert.NotEqual(first.RootHash, second.RootHash);
        Assert.False(first.ContentEquals(second));
        Assert.Throws<ArgumentException>(() => first.Diff(second));
    }

    private static MerkleSearchTreePolicy<int, string?> IntStringPolicy(string id = "test-int-string-v1") =>
        MerkleSearchTreePolicy<int, string?>.Create(id, Comparer<int>.Default, MerkleCodecs.Int32, MerkleCodecs.Utf8String);
}
