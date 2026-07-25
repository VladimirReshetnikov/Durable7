using System.Buffers.Binary;
using System.Reflection;
using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>
/// Adversarial canonical-shape and model tests for the wide-node Merkle search tree core.
/// </summary>
public sealed class MerkleSearchTreeCoreStressTests
{
    /// <summary>
    /// Forces an exact five-level geometry, including multiple entries at the same high layer, and
    /// checks that bulk construction, incremental insertion, and every contraction step converge.
    /// </summary>
    [Fact]
    public void AdversarialLayers_InsertionAndRemovalAlwaysRebuildTheCanonicalTree()
    {
        var policy = AdversarialPolicy();
        var entries = CreateAdversarialEntries(policy, -64, 64);
        var expected = MerkleSearchTree<LayeredKey, int>.CreateRange(
            entries.Select(entry => KeyValuePair.Create(entry.Key, entry.Key.Order * 17)),
            policy);

        var statistics = expected.ValidateStructure();
        Assert.Equal(entries.Length, statistics.Count);
        Assert.Equal(5, statistics.Height);
        Assert.True(statistics.MaximumEntriesPerBlock > 1);

        var ascending = MerkleSearchTree<LayeredKey, int>.Create(policy);
        foreach (var entry in entries.OrderBy(entry => entry.Key, LayeredKeyComparer.Instance))
        {
            ascending = ascending.SetItem(entry.Key, entry.Key.Order * 17);
            ascending.ValidateStructure();
        }

        var random = new Random(0x5a17_2026);
        var shuffledEntries = entries.OrderBy(_ => random.Next()).ToArray();
        var shuffled = MerkleSearchTree<LayeredKey, int>.Create(policy);
        foreach (var entry in shuffledEntries)
            shuffled = shuffled.SetItem(entry.Key, entry.Key.Order * 17);

        AssertCanonicalEquivalent(expected, ascending);
        AssertCanonicalEquivalent(expected, shuffled);

        var remaining = entries.ToDictionary(entry => entry.Key, entry => entry.Key.Order * 17);
        var current = shuffled;
        foreach (var entry in entries
                     .OrderByDescending(entry => entry.Layer)
                     .ThenBy(entry => Math.Abs(entry.Key.Order))
                     .ThenBy(entry => entry.Key.Order))
        {
            current = current.Remove(entry.Key);
            remaining.Remove(entry.Key);
            var rebuilt = MerkleSearchTree<LayeredKey, int>.CreateRange(remaining, policy);
            AssertCanonicalEquivalent(rebuilt, current);
            current.ValidateStructure();
        }

        Assert.True(current.IsEmpty);
        Assert.Equal(policy.EmptyDigest, current.RootHash);
    }

    /// <summary>
    /// Exercises mixed updates, removals, retained snapshots, and inclusive ranges against a
    /// comparer-ordered reference model.
    /// </summary>
    [Fact]
    public void RandomMutationHistory_RangesSnapshotsAndFinalCanonicalBuildMatchTheModel()
    {
        var policy = IntPolicy();
        var tree = MerkleSearchTree<int, int>.Create(policy);
        var model = new SortedDictionary<int, int>();
        var snapshots = new List<(MerkleSearchTree<int, int> Tree, KeyValuePair<int, int>[] Model)>();
        var random = new Random(0x2b16_0711);

        for (var operation = 0; operation < 12_000; operation++)
        {
            var key = random.Next(-1_000, 1_001);
            if (random.Next(5) == 0)
            {
                tree = tree.Remove(key);
                model.Remove(key);
            }
            else
            {
                var value = random.Next();
                tree = tree.SetItem(key, value);
                model[key] = value;
            }

            if (operation % 41 == 0)
            {
                var first = random.Next(-1_100, 1_101);
                var second = random.Next(-1_100, 1_101);
                var minimum = Math.Min(first, second);
                var maximum = Math.Max(first, second);
                var expectedRange = model
                    .Where(entry => entry.Key >= minimum && entry.Key <= maximum)
                    .ToArray();
                Assert.Equal(expectedRange, tree.EnumerateRange(minimum, maximum).ToArray());
            }

            if (operation % 173 == 0)
            {
                Assert.Equal(model.ToArray(), tree.ToArray());
                var statistics = tree.ValidateStructure();
                Assert.Equal(model.Count, statistics.Count);
            }

            if (operation % 1_997 == 0)
                snapshots.Add((tree, model.ToArray()));
        }

        foreach (var snapshot in snapshots)
        {
            Assert.Equal(snapshot.Model, snapshot.Tree.ToArray());
            snapshot.Tree.ValidateStructure();
        }

        var finalEntries = model.OrderBy(_ => random.Next()).ToArray();
        var rebuilt = MerkleSearchTree<int, int>.CreateRange(finalEntries, policy);
        AssertCanonicalEquivalent(rebuilt, tree);
    }

    /// <summary>
    /// Reaches one final map through independently shuffled insertions, wrong-value churn, and
    /// add/remove noise, proving that mutation history does not survive in shape or digest.
    /// </summary>
    [Fact]
    public void IndependentRandomHistories_ConvergeAfterChurn()
    {
        var policy = IntPolicy("independent-history-v1");
        var finalEntries = Enumerable.Range(-384, 769)
            .Select(key => KeyValuePair.Create(key, unchecked(key * 1_000_003)))
            .ToArray();
        var canonical = MerkleSearchTree<int, int>.CreateRange(finalEntries, policy);

        for (var history = 0; history < 12; history++)
        {
            var random = new Random(unchecked(0x71c3_0000 + history * 7919));
            var tree = MerkleSearchTree<int, int>.Create(policy);

            foreach (var entry in finalEntries.OrderBy(_ => random.Next()))
            {
                if (random.Next(3) == 0)
                    tree = tree.SetItem(entry.Key, ~entry.Value);
                tree = tree.SetItem(entry.Key, entry.Value);
            }

            for (var noise = 0; noise < 512; noise++)
            {
                var key = random.Next(-700, 701);
                tree = tree.SetItem(key, random.Next());
                if (key is < -384 or > 384)
                    tree = tree.Remove(key);
            }
            foreach (var entry in finalEntries.OrderBy(_ => random.Next()))
                tree = tree.SetItem(entry.Key, entry.Value);
            foreach (var key in tree.Keys.Where(key => key is < -384 or > 384).ToArray())
                tree = tree.Remove(key);

            AssertCanonicalEquivalent(canonical, tree);
        }
    }

    /// <summary>
    /// Removes the highest-layer separators, changes values, and adds new high-layer separators so
    /// diff must remain correct while the canonical block topology changes on both sides.
    /// </summary>
    [Fact]
    public void ShapeChangingDiff_MatchesAddedRemovedAndChangedModelInBothDirections()
    {
        var policy = AdversarialPolicy("adversarial-diff-v1");
        var sourceEntries = CreateAdversarialEntries(policy, -80, 80);
        var sourceModel = new SortedDictionary<LayeredKey, int>(LayeredKeyComparer.Instance);
        foreach (var entry in sourceEntries)
            sourceModel.Add(entry.Key, entry.Key.Order * 13);

        var targetModel = new SortedDictionary<LayeredKey, int>(sourceModel, LayeredKeyComparer.Instance);
        foreach (var entry in sourceEntries.Where(entry => entry.Layer >= 3 || entry.Key.Order % 17 == 0))
            targetModel.Remove(entry.Key);
        foreach (var entry in sourceEntries.Where(entry => entry.Layer == 0 && entry.Key.Order % 11 == 0))
            targetModel[entry.Key] = -entry.Key.Order * 101 - 1;

        var additions = new[]
        {
            FindKeyAtExactLayer(policy, -120, 4),
            FindKeyAtExactLayer(policy, 120, 4),
            FindKeyAtExactLayer(policy, 121, 2),
        };
        foreach (var key in additions)
            targetModel.Add(key, key.Order * 19);

        var source = MerkleSearchTree<LayeredKey, int>.CreateRange(sourceModel, policy);
        var target = MerkleSearchTree<LayeredKey, int>.CreateRange(
            targetModel.Reverse().Select(entry => entry),
            policy);
        source.ValidateStructure();
        target.ValidateStructure();
        Assert.NotEqual(source.ShapeForTesting(), target.ShapeForTesting());

        AssertDiffMatchesModel(source, target, sourceModel, targetModel);
        AssertDiffMatchesModel(target, source, targetModel, sourceModel);
    }

    /// <summary>
    /// Verifies a value-only update preserves every off-path block and that remove/reinsert and
    /// change/revert recover the exact canonical address and block set.
    /// </summary>
    [Fact]
    public void VersionChanges_ShareOffPathBlocksAndRevertingRestoresTheExactRoot()
    {
        var policy = IntPolicy("sharing-v1");
        var original = MerkleSearchTree<int, int>.CreateRange(
            Enumerable.Range(-4_096, 8_193).Select(key => KeyValuePair.Create(key, key * 31)),
            policy);
        var key = 137;

        Assert.Same(original.RootIdentity, original.SetItem(key, key * 31).RootIdentity);
        Assert.Same(original.RootIdentity, original.Remove(20_000).RootIdentity);

        var updated = original.SetItem(key, int.MinValue);
        Assert.NotSame(original.RootIdentity, updated.RootIdentity);
        Assert.Equal(original.BlockCount, updated.BlockCount);
        updated.ValidateStructure();

        var originalBlocks = original.BlocksForTesting().Select(block => block.Digest).ToHashSet();
        var updatedBlocks = updated.BlocksForTesting().Select(block => block.Digest).ToHashSet();
        var sharedBlocks = originalBlocks.Intersect(updatedBlocks).Count();
        Assert.True(
            sharedBlocks >= original.BlockCount - original.Height,
            $"Expected at least {original.BlockCount - original.Height} off-path blocks, but found {sharedBlocks}.");

        var originalIdentities = NodeIdentities(original.RootIdentity);
        var updatedIdentities = NodeIdentities(updated.RootIdentity);
        var sharedIdentities = originalIdentities.Intersect(updatedIdentities).Count();
        Assert.True(
            sharedIdentities >= original.BlockCount - original.Height,
            $"Expected at least {original.BlockCount - original.Height} shared node objects, but found {sharedIdentities}.");

        var valueRestored = updated.SetItem(key, key * 31);
        AssertCanonicalEquivalent(original, valueRestored);
        Assert.Equal(originalBlocks, valueRestored.BlocksForTesting().Select(block => block.Digest).ToHashSet());

        var removed = original.Remove(key);
        var keyRestored = removed.SetItem(key, key * 31);
        AssertCanonicalEquivalent(original, keyRestored);
        Assert.Equal(originalBlocks, keyRestored.BlocksForTesting().Select(block => block.Digest).ToHashSet());
    }

    private static HashSet<object> NodeIdentities(object? root)
    {
        var result = new HashSet<object>(ReferenceEqualityComparer.Instance);
        if (root is null)
            return result;

        var childrenProperty = root.GetType().GetProperty(
            "Children",
            BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException("The Merkle node's child array is unavailable.");
        var pending = new Stack<object>();
        pending.Push(root);
        while (pending.TryPop(out var node))
        {
            if (!result.Add(node))
                continue;
            var children = (Array)(childrenProperty.GetValue(node)
                ?? throw new InvalidOperationException("A Merkle node returned a null child array."));
            foreach (var child in children)
            {
                if (child is not null)
                    pending.Push(child);
            }
        }
        return result;
    }

    private static void AssertDiffMatchesModel<TKey>(
        MerkleSearchTree<TKey, int> source,
        MerkleSearchTree<TKey, int> target,
        SortedDictionary<TKey, int> sourceModel,
        SortedDictionary<TKey, int> targetModel)
        where TKey : notnull
    {
        var expected = new Dictionary<TKey, (MerkleMapDifferenceKind Kind, int OldValue, int NewValue)>();
        foreach (var entry in sourceModel)
        {
            if (!targetModel.TryGetValue(entry.Key, out var targetValue))
                expected.Add(entry.Key, (MerkleMapDifferenceKind.Removed, entry.Value, default));
            else if (entry.Value != targetValue)
                expected.Add(entry.Key, (MerkleMapDifferenceKind.Changed, entry.Value, targetValue));
        }
        foreach (var entry in targetModel)
        {
            if (!sourceModel.ContainsKey(entry.Key))
                expected.Add(entry.Key, (MerkleMapDifferenceKind.Added, default, entry.Value));
        }

        var actual = source.Diff(target);
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected.Count, actual.Select(difference => difference.Key).Distinct().Count());
        Assert.Equal(
            actual.Select(difference => difference.Key).OrderBy(key => key, source.Policy.Comparer),
            actual.Select(difference => difference.Key));
        foreach (var difference in actual)
        {
            var expectedDifference = expected[difference.Key];
            Assert.Equal(expectedDifference.Kind, difference.Kind);
            if (difference.Kind is MerkleMapDifferenceKind.Removed or MerkleMapDifferenceKind.Changed)
                Assert.Equal(expectedDifference.OldValue, difference.OldValue);
            if (difference.Kind is MerkleMapDifferenceKind.Added or MerkleMapDifferenceKind.Changed)
                Assert.Equal(expectedDifference.NewValue, difference.NewValue);
        }
    }

    private static void AssertCanonicalEquivalent<TKey, TValue>(
        MerkleSearchTree<TKey, TValue> expected,
        MerkleSearchTree<TKey, TValue> actual)
    {
        Assert.Equal(expected.RootHash, actual.RootHash);
        Assert.Equal(expected.ShapeForTesting(), actual.ShapeForTesting());
        Assert.Equal(expected.ToArray(), actual.ToArray());
        Assert.Equal(expected.ValidateStructure(), actual.ValidateStructure());
    }

    private static (LayeredKey Key, int Layer)[] CreateAdversarialEntries(
        MerkleSearchTreePolicy<LayeredKey, int> policy,
        int minimumOrder,
        int maximumOrder)
    {
        var result = new List<(LayeredKey, int)>(maximumOrder - minimumOrder + 1);
        for (var order = minimumOrder; order <= maximumOrder; order++)
        {
            var absolute = Math.Abs(order);
            var layer = order == 0
                ? 4
                : absolute % 48 == 0
                    ? 3
                    : absolute % 16 == 0
                        ? 2
                        : absolute % 4 == 0
                            ? 1
                            : 0;
            result.Add((FindKeyAtExactLayer(policy, order, layer), layer));
        }
        return [.. result];
    }

    private static LayeredKey FindKeyAtExactLayer(
        MerkleSearchTreePolicy<LayeredKey, int> policy,
        int order,
        int expectedLayer)
    {
        for (var nonce = 0; nonce < 4_000_000; nonce++)
        {
            var key = new LayeredKey(order, nonce);
            if (Layer(policy.HashKey(LayeredKeyCodec.Instance.Encode(key))) == expectedLayer)
                return key;
        }
        throw new InvalidOperationException($"Could not find a layer-{expectedLayer} key for order {order}.");
    }

    private static int Layer(MerkleDigest digest)
    {
        Span<byte> bytes = stackalloc byte[MerkleDigest.ByteLength];
        digest.WriteBytes(bytes);
        var result = 0;
        foreach (var value in bytes)
        {
            if ((value & 0xf0) != 0)
                return result;
            result++;
            if ((value & 0x0f) != 0)
                return result;
            result++;
        }
        return result;
    }

    private static MerkleSearchTreePolicy<int, int> IntPolicy(string id = "core-stress-int-v1") =>
        MerkleSearchTreePolicy<int, int>.Create(id, Comparer<int>.Default, MerkleCodecs.Int32, MerkleCodecs.Int32);

    private static MerkleSearchTreePolicy<LayeredKey, int> AdversarialPolicy(
        string id = "core-stress-layered-v1") =>
        MerkleSearchTreePolicy<LayeredKey, int>.Create(
            id,
            LayeredKeyComparer.Instance,
            LayeredKeyCodec.Instance,
            MerkleCodecs.Int32);

    private readonly record struct LayeredKey(int Order, int Nonce);

    private sealed class LayeredKeyComparer : IComparer<LayeredKey>
    {
        internal static LayeredKeyComparer Instance { get; } = new();

        public int Compare(LayeredKey left, LayeredKey right)
        {
            var comparison = left.Order.CompareTo(right.Order);
            return comparison != 0 ? comparison : left.Nonce.CompareTo(right.Nonce);
        }
    }

    private sealed class LayeredKeyCodec : IMerkleCodec<LayeredKey>
    {
        internal static LayeredKeyCodec Instance { get; } = new();

        public string EncodingId => "test-layered-key-v1";

        public byte[] Encode(LayeredKey value)
        {
            var result = new byte[sizeof(int) * 2];
            BinaryPrimitives.WriteInt32BigEndian(result, value.Order);
            BinaryPrimitives.WriteInt32BigEndian(result.AsSpan(sizeof(int)), value.Nonce);
            return result;
        }

        public LayeredKey Decode(ReadOnlySpan<byte> encoding)
        {
            if (encoding.Length != sizeof(int) * 2)
                throw new FormatException("A layered test key must contain exactly eight bytes.");
            return new LayeredKey(
                BinaryPrimitives.ReadInt32BigEndian(encoding),
                BinaryPrimitives.ReadInt32BigEndian(encoding[sizeof(int)..]));
        }
    }
}
