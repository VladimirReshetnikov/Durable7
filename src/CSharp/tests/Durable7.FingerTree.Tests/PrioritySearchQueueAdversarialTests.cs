// Tests for the priority search queue adversarial.

using System.Reflection;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Adversarial comparer, persistence, structural-invariant, and pruning coverage for the
/// winner-cached AVL priority-search queue.
/// </summary>
public sealed class PrioritySearchQueueAdversarialTests
{
    private const BindingFlags InstanceMembers =
        BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

    /// <summary>
    /// Proves that no-op detection requires both priority-order equivalence and stored-value equality:
    /// neither relation alone is sufficient to discard a replacement.
    /// </summary>
    [Fact]
    public void ReplacementNoOpDetection_UsesBothComparerAndStoredEquality()
    {
        var priorityComparer = Comparer<PriorityProbe>.Create(
            static (left, right) => left.Rank.CompareTo(right.Rank));
        var originalPriority = new PriorityProbe(rank: 10, equalityClass: 1);
        var queue = PrioritySearchQueue<int, PriorityProbe, string>
            .Create(priorityComparer: priorityComparer)
            .SetItem(7, originalPriority, "payload");

        // The ordering says these are equivalent, but the replacement carries a distinct stored value.
        var comparerEqualButDefaultUnequal = new PriorityProbe(rank: 10, equalityClass: 2);
        var representationUpdate = queue.SetItem(7, comparerEqualButDefaultUnequal, "payload");
        Assert.NotSame(queue, representationUpdate);
        Assert.True(representationUpdate.TryGetEntry(7, out var represented));
        Assert.Same(comparerEqualButDefaultUnequal, represented.Priority);
        Assert.Same(originalPriority, queue.Minimum.Priority);

        // Ordinary equality says these are equal, but the retained ordering changes the winner semantics.
        var defaultEqualButComparerDistinct = new PriorityProbe(rank: 1, equalityClass: 2);
        var semanticUpdate = representationUpdate.SetItem(7, defaultEqualButComparerDistinct, "payload");
        Assert.NotSame(representationUpdate, semanticUpdate);
        Assert.True(semanticUpdate.TryGetEntry(7, out var reprioritized));
        Assert.Same(defaultEqualButComparerDistinct, reprioritized.Priority);

        Assert.Same(semanticUpdate, semanticUpdate.SetItem(7, defaultEqualButComparerDistinct, "payload"));
        AssertStructure(queue);
        AssertStructure(representationUpdate);
        AssertStructure(semanticUpdate);
    }

    /// <summary>Checks that comparer-equivalent key operations never replace the first stored key representative.</summary>
    [Fact]
    public void EquivalentKeys_RetainOriginalRepresentativeThroughUpdatesAndRemoval()
    {
        var originalKey = new string(['A', 'l', 'p', 'h', 'a']);
        var queue = PrioritySearchQueue<string, int, string>
            .Create(StringComparer.OrdinalIgnoreCase)
            .SetItem(originalKey, 9, "first");

        var updated = queue.SetItem("ALPHA", 2, "second");
        Assert.True(updated.TryGetEntry("alpha", out var stored));
        Assert.Same(originalKey, stored.Key);
        Assert.Equal(2, stored.Priority);
        Assert.Equal("second", stored.Value);

        Assert.False(updated.TryAdd("aLpHa", 0, "third", out var duplicate));
        Assert.Same(updated, duplicate);
        Assert.True(updated.TryRemove("ALpha", out var removed, out var empty));
        Assert.Same(originalKey, removed.Key);
        Assert.True(empty.IsEmpty);

        AssertStructure(queue);
        AssertStructure(updated);
    }

    /// <summary>
    /// Exercises every AVL rotation direction and deletion rebalancing while independently checking BST order,
    /// balance, cached count/height, and the cached priority winner at every private node.
    /// </summary>
    [Fact]
    public void ReflectionInvariantAudit_SurvivesRotationsAndDeletionRebalancing()
    {
        var queue = PrioritySearchQueue<int, int, int>.Empty;
        var insertionOrder = Enumerable.Range(0, 2_048)
            .OrderBy(static value => ReverseBits((uint)value))
            .ToArray();

        for (var index = 0; index < insertionOrder.Length; index++)
        {
            var key = insertionOrder[index];
            queue = queue.SetItem(key, (key * 37) % 101, -key);
            if ((index & 63) == 0)
                AssertStructure(queue);
        }

        foreach (var key in Enumerable.Range(0, 2_048).Where(static value => value % 3 != 1))
        {
            queue = queue.Remove(key);
            if ((key & 63) == 0)
                AssertStructure(queue);
        }

        Assert.Equal(Enumerable.Range(0, 2_048).Where(static value => value % 3 == 1),
            queue.Select(static entry => entry.Key));
        AssertStructure(queue);
    }

    /// <summary>
    /// Models a branching randomized history and revisits retained versions after later updates to catch accidental
    /// mutation as well as stale winner/count/height metadata.
    /// </summary>
    [Fact]
    public void RandomizedRetainedHistory_MatchesModelAndPreservesEverySnapshot()
    {
        var random = new Random(0x5A17_2026);
        var queue = PrioritySearchQueue<int, int, int>.Empty;
        var model = new Dictionary<int, (int Priority, int Value)>();
        var retained = new List<RetainedSnapshot>();

        for (var step = 0; step < 20_000; step++)
        {
            var key = random.Next(-768, 769);
            switch (random.Next(8))
            {
                case <= 3:
                    {
                        var priority = random.Next(64);
                        queue = queue.SetItem(key, priority, step);
                        model[key] = (priority, step);
                        break;
                    }
                case 4:
                    queue = queue.Remove(key);
                    model.Remove(key);
                    break;
                case 5:
                    {
                        var priority = random.Next(64);
                        var expectedAdded = !model.ContainsKey(key);
                        Assert.Equal(expectedAdded, queue.TryAdd(key, priority, step, out var added));
                        queue = added;
                        if (expectedAdded)
                            model.Add(key, (priority, step));
                        break;
                    }
                default:
                    if (model.Count == 0)
                    {
                        var priority = random.Next(64);
                        queue = queue.SetItem(key, priority, step);
                        model[key] = (priority, step);
                    }
                    else
                    {
                        var expected = model
                            .OrderBy(static pair => pair.Value.Priority)
                            .ThenBy(static pair => pair.Key)
                            .First();
                        queue = queue.DeleteMinimum(out var removed);
                        Assert.Equal(
                            new PrioritySearchEntry<int, int, int>(
                                expected.Key,
                                expected.Value.Priority,
                                expected.Value.Value),
                            removed);
                        model.Remove(expected.Key);
                    }
                    break;
            }

            if (step % 127 == 0)
                AssertModelAndStructure(queue, model);
            if (step % 239 == 0)
                retained.Add(new RetainedSnapshot(queue, ModelEntries(model)));
        }

        AssertModelAndStructure(queue, model);
        foreach (var snapshot in retained)
        {
            Assert.Equal(snapshot.Entries, snapshot.Queue.ToArray());
            AssertStructure(snapshot.Queue);
        }
    }

    /// <summary>
    /// Uses comparison counts as traversal evidence: an impossible threshold stops at the root, an exact-key range
    /// visits no more than one search path, and a mixed threshold query remains in key order.
    /// </summary>
    [Fact]
    public void EnumerateAtMost_PreservesOrderAndComparatorCountsExposePruning()
    {
        var keyComparer = new CountingComparer<int>(Comparer<int>.Default);
        var priorityComparer = new CountingComparer<int>(Comparer<int>.Default);
        var queue = PrioritySearchQueue<int, int, int>.Create(keyComparer, priorityComparer);
        for (var key = 0; key < 4_095; key++)
            queue = queue.SetItem(key, key % 17, -key);
        AssertStructure(queue);

        keyComparer.Reset();
        priorityComparer.Reset();
        Assert.Empty(queue.EnumerateAtMost(0, 4_094, -1));
        Assert.Equal(1, keyComparer.ComparisonCount); // range validation only
        Assert.Equal(1, priorityComparer.ComparisonCount); // the root winner prunes the entire tree

        keyComparer.Reset();
        priorityComparer.Reset();
        var exact = queue.EnumerateAtMost(2_047, 2_047, 16).ToArray();
        Assert.Equal([new PrioritySearchEntry<int, int, int>(2_047, 2_047 % 17, -2_047)], exact);
        var visitedNodes = priorityComparer.ComparisonCount - exact.Length;
        Assert.InRange(visitedNodes, 1, queue.Height);
        Assert.Equal(1 + (2 * visitedNodes), keyComparer.ComparisonCount);

        var expected = Enumerable.Range(512, 513)
            .Where(static key => key % 17 <= 3)
            .Select(static key => new PrioritySearchEntry<int, int, int>(key, key % 17, -key));
        Assert.Equal(expected, queue.EnumerateAtMost(512, 1_024, 3));
    }

    /// <summary>Locks down exact no-op identity and sharing of the sibling subtree outside an updated search path.</summary>
    [Fact]
    public void NoOpsAndPathCopying_PreserveExactIdentities()
    {
        var queue = PrioritySearchQueue<int, int, int>.CreateRange(
            Enumerable.Range(0, 256)
                .Select(static key => new PrioritySearchEntry<int, int, int>(key, key + 10, -key)));
        var root = GetRoot(queue)!;
        var rootEntry = GetNodeProperty<PrioritySearchEntry<int, int, int>>(root, "Entry");
        var untouchedRight = GetNodeProperty<object?>(root, "Right");
        Assert.NotNull(GetNodeProperty<object?>(root, "Left"));

        var exactNoOp = queue.SetItem(rootEntry.Key, rootEntry.Priority, rootEntry.Value);
        Assert.Same(queue, exactNoOp);
        Assert.Same(root, GetRoot(exactNoOp));
        Assert.Same(queue, queue.Remove(-1));
        Assert.False(queue.TryAdd(rootEntry.Key, 0, 0, out var duplicate));
        Assert.Same(queue, duplicate);

        var updated = queue.SetItem(0, -1, 42);
        var updatedRoot = GetRoot(updated)!;
        Assert.NotSame(root, updatedRoot);
        Assert.Same(untouchedRight, GetNodeProperty<object?>(updatedRoot, "Right"));
        Assert.True(queue.TryGetEntry(0, out var oldEntry));
        Assert.Equal(new PrioritySearchEntry<int, int, int>(0, 10, 0), oldEntry);
        Assert.True(updated.TryGetEntry(0, out var newEntry));
        Assert.Equal(new PrioritySearchEntry<int, int, int>(0, -1, 42), newEntry);

        AssertStructure(queue);
        AssertStructure(updated);
    }

    /// <summary>Verifies that comparer-equal priority ties drain by the retained key order, not insertion order.</summary>
    [Fact]
    public void DeleteMinimum_UsesRetainedKeyOrderForComparerEqualPriorityTies()
    {
        var descendingKeys = Comparer<int>.Create(static (left, right) => right.CompareTo(left));
        var priorityBuckets = Comparer<PriorityProbe>.Create(
            static (left, right) => (left.Rank / 10).CompareTo(right.Rank / 10));
        var queue = PrioritySearchQueue<int, PriorityProbe, string>.Create(descendingKeys, priorityBuckets);

        foreach (var key in new[] { 2, 8, 0, 6, 9, 1, 7, 3, 5, 4 })
            queue = queue.SetItem(key, new PriorityProbe(10 + key, 100 + key), $"v{key}");

        for (var expectedKey = 9; expectedKey >= 0; expectedKey--)
        {
            Assert.Equal(expectedKey, queue.Minimum.Key);
            queue = queue.DeleteMinimum(out var removed);
            Assert.Equal(expectedKey, removed.Key);
            Assert.Equal($"v{expectedKey}", removed.Value);
            AssertStructure(queue);
        }
        Assert.True(queue.IsEmpty);
    }

    private static void AssertModelAndStructure(
        PrioritySearchQueue<int, int, int> queue,
        Dictionary<int, (int Priority, int Value)> model)
    {
        Assert.Equal(ModelEntries(model), queue.ToArray());
        if (model.Count == 0)
        {
            Assert.False(queue.TryGetMinimum(out _));
        }
        else
        {
            var expected = model
                .OrderBy(static pair => pair.Value.Priority)
                .ThenBy(static pair => pair.Key)
                .First();
            Assert.Equal(expected.Key, queue.Minimum.Key);
            Assert.Equal(expected.Value.Priority, queue.Minimum.Priority);
            Assert.Equal(expected.Value.Value, queue.Minimum.Value);
        }
        AssertStructure(queue);
    }

    private static PrioritySearchEntry<int, int, int>[] ModelEntries(
        Dictionary<int, (int Priority, int Value)> model) =>
        [.. model.OrderBy(static pair => pair.Key)
            .Select(static pair => new PrioritySearchEntry<int, int, int>(
                pair.Key,
                pair.Value.Priority,
                pair.Value.Value))];

    private static void AssertStructure<TKey, TPriority, TValue>(
        PrioritySearchQueue<TKey, TPriority, TValue> queue)
    {
        var root = GetRoot(queue);
        var validated = ValidateNode<TKey, TPriority, TValue>(
            root,
            default,
            default,
            queue.KeyComparer,
            queue.PriorityComparer);

        Assert.Equal(queue.Count, validated.Count);
        Assert.Equal(queue.Height, validated.Height);
        var publicStatistics = queue.ValidateStructure();
        Assert.Equal(queue.Count, publicStatistics.Count);
        Assert.Equal(queue.Height, publicStatistics.Height);
        Assert.InRange(publicStatistics.MaximumAbsoluteBalanceFactor, 0, 1);
        var entries = queue.ToArray();
        Assert.Equal(queue.Count, entries.Length);
        for (var index = 1; index < entries.Length; index++)
            Assert.True(queue.KeyComparer.Compare(entries[index - 1].Key, entries[index].Key) < 0);
    }

    private static ValidatedNode<TKey, TPriority, TValue> ValidateNode<TKey, TPriority, TValue>(
        object? node,
        KeyBound<TKey> lower,
        KeyBound<TKey> upper,
        IComparer<TKey> keyComparer,
        IComparer<TPriority> priorityComparer)
    {
        if (node is null)
            return default;

        var entry = GetNodeProperty<PrioritySearchEntry<TKey, TPriority, TValue>>(node, "Entry");
        if (lower.HasValue)
            Assert.True(keyComparer.Compare(entry.Key, lower.Value) > 0);
        if (upper.HasValue)
            Assert.True(keyComparer.Compare(entry.Key, upper.Value) < 0);

        var leftNode = GetNodeProperty<object?>(node, "Left");
        var rightNode = GetNodeProperty<object?>(node, "Right");
        var left = ValidateNode<TKey, TPriority, TValue>(
            leftNode,
            lower,
            new KeyBound<TKey>(entry.Key),
            keyComparer,
            priorityComparer);
        var right = ValidateNode<TKey, TPriority, TValue>(
            rightNode,
            new KeyBound<TKey>(entry.Key),
            upper,
            keyComparer,
            priorityComparer);

        var expectedCount = checked(1 + left.Count + right.Count);
        var expectedHeight = checked(1 + Math.Max(left.Height, right.Height));
        Assert.Equal(expectedCount, GetNodeProperty<int>(node, "Count"));
        Assert.Equal(expectedHeight, GetNodeProperty<int>(node, "Height"));
        Assert.InRange(left.Height - right.Height, -1, 1);

        var expectedWinner = entry;
        if (left.HasWinner && IsBefore(left.Winner, expectedWinner, keyComparer, priorityComparer))
            expectedWinner = left.Winner;
        if (right.HasWinner && IsBefore(right.Winner, expectedWinner, keyComparer, priorityComparer))
            expectedWinner = right.Winner;
        var actualWinner = GetNodeProperty<PrioritySearchEntry<TKey, TPriority, TValue>>(node, "Winner");
        Assert.True(SameStoredEntry(expectedWinner, actualWinner, keyComparer, priorityComparer));

        return new ValidatedNode<TKey, TPriority, TValue>(
            expectedCount,
            expectedHeight,
            HasWinner: true,
            expectedWinner);
    }

    private static bool IsBefore<TKey, TPriority, TValue>(
        PrioritySearchEntry<TKey, TPriority, TValue> left,
        PrioritySearchEntry<TKey, TPriority, TValue> right,
        IComparer<TKey> keyComparer,
        IComparer<TPriority> priorityComparer)
    {
        var priority = priorityComparer.Compare(left.Priority, right.Priority);
        return priority < 0 || (priority == 0 && keyComparer.Compare(left.Key, right.Key) < 0);
    }

    private static bool SameStoredEntry<TKey, TPriority, TValue>(
        PrioritySearchEntry<TKey, TPriority, TValue> left,
        PrioritySearchEntry<TKey, TPriority, TValue> right,
        IComparer<TKey> keyComparer,
        IComparer<TPriority> priorityComparer) =>
        keyComparer.Compare(left.Key, right.Key) == 0
        && EqualityComparer<TKey>.Default.Equals(left.Key, right.Key)
        && priorityComparer.Compare(left.Priority, right.Priority) == 0
        && EqualityComparer<TPriority>.Default.Equals(left.Priority, right.Priority)
        && EqualityComparer<TValue>.Default.Equals(left.Value, right.Value);

    private static object? GetRoot<TKey, TPriority, TValue>(
        PrioritySearchQueue<TKey, TPriority, TValue> queue) =>
        typeof(PrioritySearchQueue<TKey, TPriority, TValue>)
            .GetField("_root", InstanceMembers)!
            .GetValue(queue);

    private static T GetNodeProperty<T>(object node, string name) =>
        (T)node.GetType().GetProperty(name, InstanceMembers)!.GetValue(node)!;

    private static uint ReverseBits(uint value)
    {
        value = ((value & 0x5555_5555U) << 1) | ((value >> 1) & 0x5555_5555U);
        value = ((value & 0x3333_3333U) << 2) | ((value >> 2) & 0x3333_3333U);
        value = ((value & 0x0F0F_0F0FU) << 4) | ((value >> 4) & 0x0F0F_0F0FU);
        value = ((value & 0x00FF_00FFU) << 8) | ((value >> 8) & 0x00FF_00FFU);
        return (value << 16) | (value >> 16);
    }

    private sealed class CountingComparer<T>(IComparer<T> inner) : IComparer<T>
    {
        internal int ComparisonCount { get; private set; }

        public int Compare(T? left, T? right)
        {
            ComparisonCount++;
            return inner.Compare(left!, right!);
        }

        internal void Reset() => ComparisonCount = 0;
    }

    private sealed class PriorityProbe(int rank, int equalityClass) : IEquatable<PriorityProbe>
    {
        internal int Rank { get; } = rank;

        private int EqualityClass { get; } = equalityClass;

        public bool Equals(PriorityProbe? other) =>
            other is not null && EqualityClass == other.EqualityClass;

        public override bool Equals(object? obj) => obj is PriorityProbe other && Equals(other);

        public override int GetHashCode() => EqualityClass;
    }

    private readonly record struct KeyBound<TKey>
    {
        internal KeyBound(TKey value)
        {
            HasValue = true;
            Value = value;
        }

        internal bool HasValue { get; }

        internal TKey Value { get; }
    }

    private readonly record struct ValidatedNode<TKey, TPriority, TValue>(
        int Count,
        int Height,
        bool HasWinner,
        PrioritySearchEntry<TKey, TPriority, TValue> Winner);

    private sealed record RetainedSnapshot(
        PrioritySearchQueue<int, int, int> Queue,
        PrioritySearchEntry<int, int, int>[] Entries);
}
