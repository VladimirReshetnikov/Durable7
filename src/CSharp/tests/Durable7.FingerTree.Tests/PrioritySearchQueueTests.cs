// Tests for the priority search queue.

using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Keyed lookup, priority winner, range-query, balance, and model coverage for <see cref="PrioritySearchQueue{TKey,TPriority,TValue}"/>.</summary>
public sealed class PrioritySearchQueueTests
{
    /// <summary>Verifies that replacing a value with itself does not invoke user equality.</summary>
    [Fact]
    public void SameReferenceReplacement_BypassesValueEquality()
    {
        var value = new EqualityCountingValue();
        var queue = PrioritySearchQueue<string, int, EqualityCountingValue>.Empty
            .SetItem("key", 1, value);

        Assert.Same(queue, queue.SetItem("key", 1, value));
        Assert.Equal(0, value.EqualityCalls);
    }

    private sealed class EqualityCountingValue
    {
        /// <summary>Gets how many times the policy was asked to compare.</summary>
        public int EqualityCalls { get; private set; }
        /// <summary>Determines whether both values hold the same elements.</summary>
        public override bool Equals(object? obj) { EqualityCalls++; return ReferenceEquals(this, obj); }
        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public override int GetHashCode() => 0;
    }

    /// <summary>Verifies keyed updates and deterministic minimum-priority tie handling.</summary>
    [Fact]
    public void KeyedOperationsAndMinimum_WorkTogether()
    {
        var queue = PrioritySearchQueue<string, int, string>.Empty
            .SetItem("c", 3, "C")
            .SetItem("a", 1, "A")
            .SetItem("b", 1, "B")
            .SetItem("d", 4, "D");

        Assert.Equal(new[] { "a", "b", "c", "d" }, queue.Select(entry => entry.Key));
        Assert.Equal("a", queue.Minimum.Key);
        Assert.True(queue.TryGetEntry("c", out var c));
        Assert.Equal(new("c", 3, "C"), c);

        var reprioritized = queue.SetItem("c", 0, "new C");
        Assert.Equal("c", reprioritized.Minimum.Key);
        Assert.Equal("a", queue.Minimum.Key);
        Assert.Same(queue, queue.SetItem("c", 3, "C"));
        Assert.Same(queue, queue.Remove("missing"));
    }

    /// <summary>Checks inclusive key-range/priority-threshold queries against brute force.</summary>
    [Fact]
    public void EnumerateAtMost_PrunesByKeyAndPriority()
    {
        var random = new Random(20260720);
        var entries = Enumerable.Range(0, 10_000)
            .Select(key => new PrioritySearchEntry<int, int, int>(key, random.Next(1_000), key * 2))
            .ToArray();
        var queue = PrioritySearchQueue<int, int, int>.CreateRange(entries);

        for (var trial = 0; trial < 1_000; trial++)
        {
            var low = random.Next(10_000);
            var high = random.Next(low, 10_000);
            var threshold = random.Next(1_000);
            var expected = entries.Where(entry => entry.Key >= low && entry.Key <= high && entry.Priority <= threshold);
            Assert.Equal(expected, queue.EnumerateAtMost(low, high, threshold));
        }
        Assert.Throws<ArgumentException>(() => queue.EnumerateAtMost(2, 1, 0));
    }

    /// <summary>Checks randomized keyed histories and minimum deletion against a dictionary model.</summary>
    [Fact]
    public void RandomizedHistory_MatchesDictionaryAndPriorityModel()
    {
        var random = new Random(20260721);
        var queue = PrioritySearchQueue<int, int, int>.Empty;
        var model = new Dictionary<int, (int Priority, int Value)>();
        var snapshots = new List<(PrioritySearchQueue<int, int, int>, PrioritySearchEntry<int, int, int>[])>();

        for (var i = 0; i < 50_000; i++)
        {
            var key = random.Next(-5_000, 5_001);
            if (random.Next(4) == 0)
            {
                queue = queue.Remove(key);
                model.Remove(key);
            }
            else
            {
                var priority = random.Next(10_000);
                queue = queue.SetItem(key, priority, i);
                model[key] = (priority, i);
            }
            if (i % 3_001 == 0)
                snapshots.Add((queue, ModelEntries(model)));
        }

        Assert.Equal(ModelEntries(model), queue.ToArray());
        Assert.InRange(queue.Height, 1, 64);
        foreach (var (snapshot, expected) in snapshots)
            Assert.Equal(expected, snapshot.ToArray());

        while (model.Count != 0)
        {
            var expected = model.OrderBy(pair => pair.Value.Priority).ThenBy(pair => pair.Key).First();
            queue = queue.DeleteMinimum(out var removed);
            Assert.Equal(expected.Key, removed.Key);
            Assert.Equal(expected.Value.Priority, removed.Priority);
            Assert.Equal(expected.Value.Value, removed.Value);
            model.Remove(expected.Key);
        }
        Assert.True(queue.IsEmpty);
    }

    /// <summary>Verifies duplicate rejection, removal result, empty behavior, and representative retention.</summary>
    [Fact]
    public void TryPatternsAndComparerEquivalence()
    {
        var queue = PrioritySearchQueue<string, int, object>.Create(StringComparer.OrdinalIgnoreCase);
        var key = new string(['A', 'l', 'p', 'h', 'a']);
        var value = new object();
        Assert.True(queue.TryAdd(key, 10, value, out queue));
        Assert.False(queue.TryAdd("ALPHA", 1, new object(), out var unchanged));
        Assert.Same(queue, unchanged);

        queue = queue.SetItem("alpha", 5, value);
        Assert.True(queue.TryGetEntry("ALPHA", out var stored));
        Assert.Same(key, stored.Key);
        Assert.False(PrioritySearchQueue<int, int, int>.Empty.TryGetMinimum(out _));
        Assert.Throws<InvalidOperationException>(() => PrioritySearchQueue<int, int, int>.Empty.Minimum);

        Assert.True(queue.TryRemove("aLpHa", out var removed, out var empty));
        Assert.Same(key, removed.Key);
        Assert.True(empty.IsEmpty);
    }

    private static PrioritySearchEntry<int, int, int>[] ModelEntries(
        Dictionary<int, (int Priority, int Value)> model) =>
        [.. model.OrderBy(pair => pair.Key)
            .Select(pair => new PrioritySearchEntry<int, int, int>(pair.Key, pair.Value.Priority, pair.Value.Value))];
}
