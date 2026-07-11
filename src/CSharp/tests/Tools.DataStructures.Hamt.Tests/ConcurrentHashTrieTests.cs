using System.Collections.Concurrent;
using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Contract, snapshot, and contention coverage for <see cref="ConcurrentHashTrie{TKey,TValue}"/>.</summary>
public sealed class ConcurrentHashTrieTests
{
    /// <summary>Verifies ordinary mutable-map operations and no-op generation behavior.</summary>
    [Fact]
    public void BasicOperations_AreLinearizableAndGenerationStamped()
    {
        var trie = new ConcurrentHashTrie<string, int>(StringComparer.OrdinalIgnoreCase);
        Assert.True(trie.TryAdd("Alpha", 1));
        Assert.False(trie.TryAdd("ALPHA", 2));
        Assert.Equal(1, trie["alpha"]);
        Assert.Equal(1, trie.Generation);

        trie["ALPHA"] = 1;
        Assert.Equal(1, trie.Generation);
        Assert.True(trie.TryUpdate("alpha", 2, 1));
        Assert.False(trie.TryUpdate("alpha", 3, 1));
        Assert.Equal(2, trie["ALPHA"]);

        Assert.True(trie.TryRemove("aLpHa", out var removed));
        Assert.Equal(2, removed);
        Assert.False(trie.TryRemove("alpha", out _));
        Assert.True(trie.IsEmpty);
    }

    /// <summary>Verifies snapshots and enumerators remain stable after later publications.</summary>
    [Fact]
    public void Snapshot_RemainsImmutableWhileTrieChanges()
    {
        var trie = ConcurrentHashTrie<int, string>.CreateRange(
            Enumerable.Range(0, 100).Select(i => KeyValuePair.Create(i, $"v{i}")));
        var snapshot = trie.Snapshot();
        var enumerator = trie.GetEnumerator();

        trie.Clear();
        trie.SetItem(1000, "later");

        Assert.Equal(100, snapshot.Count);
        Assert.Equal("v42", snapshot[42]);
        var enumerated = new List<int>();
        while (enumerator.MoveNext())
            enumerated.Add(enumerator.Current.Key);
        Assert.Equal(Enumerable.Range(0, 100).Order(), enumerated.Order());
        Assert.Single(trie);
        Assert.Equal("later", trie[1000]);
    }

    /// <summary>Verifies unique-key publishers do not lose successful updates under contention.</summary>
    [Fact]
    public void ParallelUniqueAdds_PublishEveryEntry()
    {
        var trie = new ConcurrentHashTrie<int, int>();
        Parallel.For(0, 10_000, i => Assert.True(trie.TryAdd(i, i * 3)));

        Assert.Equal(10_000, trie.Count);
        for (var i = 0; i < 10_000; i++)
            Assert.Equal(i * 3, trie[i]);
    }

    /// <summary>Verifies retryable update factories implement an atomic contended counter.</summary>
    [Fact]
    public void AddOrUpdate_AtomicallyAccumulatesUnderContention()
    {
        var trie = new ConcurrentHashTrie<string, int>();
        Parallel.For(0, 20_000, _ => trie.AddOrUpdate("counter", _ => 1, (_, value) => value + 1));
        Assert.Equal(20_000, trie["counter"]);
    }

    /// <summary>Verifies concurrently captured snapshots are always internally consistent.</summary>
    [Fact]
    public async Task ConcurrentSnapshots_AreStablePublishedGenerations()
    {
        var trie = new ConcurrentHashTrie<int, int>();
        var errors = new ConcurrentQueue<string>();
        using var stop = new CancellationTokenSource();
        var reader = Task.Run(() =>
        {
            while (!stop.IsCancellationRequested)
            {
                var snapshot = trie.Snapshot();
                if (snapshot.Count != snapshot.Count())
                    errors.Enqueue("Snapshot count disagreed with enumeration.");
                foreach (var (key, value) in snapshot)
                {
                    if (key != value)
                        errors.Enqueue($"Observed torn entry {key}/{value}.");
                }
            }
        });

        Parallel.For(0, 5_000, i => trie.SetItem(i, i));
        stop.Cancel();
        await reader;

        Assert.Empty(errors);
        Assert.Equal(5_000, trie.Count);
    }
}
