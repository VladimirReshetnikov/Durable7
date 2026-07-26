// Tests for the concurrent hash trie.

using System.Collections.Concurrent;
using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Contract, snapshot, and contention coverage for <see cref="ConcurrentHashTrie{TKey,TValue}"/>.</summary>
public sealed class ConcurrentHashTrieTests
{
    /// <summary>Verifies same-reference values bypass user equality and publication.</summary>
    [Fact]
    public void SameReferenceUpdates_BypassValueEqualityAndKeepGeneration()
    {
        var trie = new ConcurrentHashTrie<string, EqualityCountingValue>();
        var value = new EqualityCountingValue();
        trie.SetItem("key", value);
        var generation = trie.Generation;

        trie.SetItem("key", value);
        Assert.Same(value, trie.AddOrUpdate("key", _ => value, (_, _) => value));
        Assert.True(trie.TryUpdate("key", value, value));

        Assert.Equal(generation, trie.Generation);
        Assert.Equal(0, value.EqualityCalls);
    }

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

    private sealed class EqualityCountingValue
    {
        public int EqualityCalls { get; private set; }

        public override bool Equals(object? obj)
        {
            EqualityCalls++;
            return ReferenceEquals(this, obj);
        }

        public override int GetHashCode() => 0;
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

    /// <summary>
    /// Locks the existing snapshot-to-CHAMP boundary that a future frozen conversion must match.
    /// </summary>
    [Fact]
    public void SnapshotToPersistent_PreservesPolicyRepresentativesOrderAndGeneration()
    {
        var comparer = new NullableCollisionComparer();
        var firstKey = new string("Alpha".ToCharArray());
        var equivalentKey = new string("ALPHA".ToCharArray());
        var finalEquivalentKey = new string("alpha".ToCharArray());
        var initialValue = new RepresentativeValue("initial");
        var winningValue = new RepresentativeValue("winning");
        var equalWinningValue = new RepresentativeValue("winning");
        var nullValue = new RepresentativeValue("null");
        var laterValue = new RepresentativeValue("later");
        var trie = new ConcurrentHashTrie<string?, RepresentativeValue?>(comparer);
        trie.SetItem(firstKey, initialValue);
        trie.SetItem(equivalentKey, winningValue);
        trie.SetItem(finalEquivalentKey, equalWinningValue);
        trie.SetItem(null, nullValue);
        var snapshot = trie.Snapshot();
        var snapshotEntries = snapshot.ToArray();

        trie.SetItem("ALPHA", laterValue);
        Assert.True(trie.TryRemove(null, out _));
        trie.SetItem("post-snapshot", new RepresentativeValue("new"));

        var persistent = snapshot.ToPersistentHashMap();

        Assert.Same(comparer, persistent.Comparer);
        Assert.Equal(snapshotEntries.Length, persistent.Count);
        Assert.True(persistent.TryGetKey("aLpHa", out var actualKey));
        Assert.Same(firstKey, actualKey);
        Assert.True(persistent.TryGetValue("aLpHa", out var actualValue));
        Assert.Same(winningValue, actualValue);
        Assert.True(persistent.TryGetKey(null, out var actualNullKey));
        Assert.Null(actualNullKey);
        Assert.True(persistent.TryGetValue(null, out var actualNullValue));
        Assert.Same(nullValue, actualNullValue);

        var persistentEntries = persistent.ToArray();
        for (var index = 0; index < snapshotEntries.Length; index++)
        {
            Assert.Same(snapshotEntries[index].Key, persistentEntries[index].Key);
            Assert.Same(snapshotEntries[index].Value, persistentEntries[index].Value);
        }

        trie.Clear();
        Assert.Same(winningValue, snapshot["alpha"]);
        Assert.Same(nullValue, snapshot[null]);
        Assert.Same(winningValue, persistent["alpha"]);
        Assert.Same(nullValue, persistent[null]);
    }

    /// <summary>Verifies Ctrie snapshots expose the canonical CHAMP data-run-before-node-run order.</summary>
    [Fact]
    public void SnapshotEnumeration_MatchesCanonicalChampAcrossMixedBranchesAndTombs()
    {
        var trie = new ConcurrentHashTrie<int, string>();
        trie.SetItem(0, "zero");
        trie.SetItem(32, "thirty-two");
        trie.SetItem(1, "one");

        var mixed = trie.Snapshot();
        Assert.Equal(new[] { 1, 0, 32 }, mixed.Select(entry => entry.Key));
        Assert.Equal(new[] { 1, 0, 32 }, mixed.ToPersistentHashMap().Select(entry => entry.Key));

        ConcurrentHashTrie<int, string>.SnapshotView? tombSnapshot = null;
        trie.RemovalCommittedHookForTesting = () => tombSnapshot = trie.Snapshot();
        try
        {
            Assert.True(trie.TryRemove(32, out var removed));
            Assert.Equal("thirty-two", removed);
        }
        finally
        {
            trie.RemovalCommittedHookForTesting = null;
        }

        var captured = Assert.IsType<ConcurrentHashTrie<int, string>.SnapshotView>(tombSnapshot);
        Assert.Equal(new[] { 0, 1 }, captured.Select(entry => entry.Key));
        Assert.Equal(new[] { 0, 1 }, captured.ToPersistentHashMap().Select(entry => entry.Key));
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

    /// <summary>Exercises collision nodes and lazy generation renewal under parallel mutation.</summary>
    [Fact]
    public void CollidingKeys_AreLinearizableAcrossRepeatedSnapshots()
    {
        var trie = new ConcurrentHashTrie<int, int>(ConstantHashComparer.Instance);
        Parallel.For(0, 2_000, i => trie.SetItem(i, i));

        var retained = new List<ConcurrentHashTrie<int, int>.SnapshotView>();
        for (var round = 0; round < 8; round++)
        {
            retained.Add(trie.Snapshot());
            var offset = round * 100;
            Parallel.For(offset, offset + 100, i => Assert.True(trie.TryRemove(i, out _)));
        }

        Assert.Equal(2_000, retained[0].Count);
        Assert.Equal(1_300, retained[^1].Count);
        Assert.Equal(1_200, trie.Count);
        Assert.Equal(2_000, retained[0].ToPersistentHashMap().Count);
        Assert.Equal(1_999, retained[0][1_999]);
    }

    /// <summary>Reproduces the root-main race that requires RDCSS rather than a plain root CAS.</summary>
    [Fact]
    public async Task Snapshot_DoesNotLoseWriterCommittedAfterMainRead()
    {
        var trie = new ConcurrentHashTrie<int, int>();
        using var mainRead = new ManualResetEventSlim();
        using var releaseSnapshot = new ManualResetEventSlim();
        var firstInvocation = 1;
        trie.SnapshotMainReadHookForTesting = () =>
        {
            if (Interlocked.Exchange(ref firstInvocation, 0) == 1)
            {
                mainRead.Set();
                releaseSnapshot.Wait();
            }
        };

        var snapshotTask = Task.Run(trie.Snapshot);
        try
        {
            Assert.True(mainRead.Wait(TimeSpan.FromSeconds(30)));
            trie.SetItem(42, 420);
        }
        finally
        {
            releaseSnapshot.Set();
        }

        var snapshot = await snapshotTask.WaitAsync(TimeSpan.FromSeconds(30));
        trie.SnapshotMainReadHookForTesting = null;

        Assert.Equal(420, snapshot[42]);
        Assert.Equal(420, trie[42]);
        Assert.Equal(1, trie.ValidateStructureForTesting().EntryCount);
    }

    /// <summary>Verifies a reader completes an installed node-local GCAS descriptor.</summary>
    [Fact]
    public async Task Reader_HelpsInstalledGcasDescriptor()
    {
        var trie = new ConcurrentHashTrie<int, int>();
        using var installed = new ManualResetEventSlim();
        using var releaseWriter = new ManualResetEventSlim();
        trie.GcasInstalledHookForTesting = () =>
        {
            installed.Set();
            releaseWriter.Wait();
        };

        var writer = Task.Run(() => trie.SetItem(7, 70));
        try
        {
            Assert.True(installed.Wait(TimeSpan.FromSeconds(30)));
            Assert.True(trie.TryGetValue(7, out var value));
            Assert.Equal(70, value);
        }
        finally
        {
            releaseWriter.Set();
        }

        await writer.WaitAsync(TimeSpan.FromSeconds(30));
        trie.GcasInstalledHookForTesting = null;
        Assert.Equal(70, trie[7]);
    }

    /// <summary>Verifies an equal-hash L-node splits when a later hash diverges below it.</summary>
    [Fact]
    public void CollisionNode_SplitsForDifferentFullHash()
    {
        var trie = new ConcurrentHashTrie<int, string>(CollisionThenSplitComparer.Instance);
        trie.SetItem(0, "zero");
        trie.SetItem(1, "one");
        trie.SetItem(2, "two");

        Assert.Equal("zero", trie[0]);
        Assert.Equal("one", trie[1]);
        Assert.Equal("two", trie[2]);
        var statistics = trie.ValidateStructureForTesting();
        Assert.Equal(3, statistics.EntryCount);
        Assert.Equal(1, statistics.CollisionNodeCount);
        Assert.Equal(0, statistics.TombNodeCount);
    }

    /// <summary>Verifies value replacement retains the first equivalent key object.</summary>
    [Fact]
    public void Replacement_RetainsStoredKeyRepresentative()
    {
        var stored = new string(['A', 'l', 'p', 'h', 'a']);
        var equivalent = new string(['A', 'L', 'P', 'H', 'A']);
        var trie = new ConcurrentHashTrie<string, int>(StringComparer.OrdinalIgnoreCase);
        trie.SetItem(stored, 1);
        trie.SetItem(equivalent, 2);

        var actual = Assert.Single(trie.Snapshot()).Key;

        Assert.Same(stored, actual);
        Assert.Equal(2, trie[equivalent]);
    }

    /// <summary>Verifies deletion promotes deep survivors and restores the canonical empty root.</summary>
    [Fact]
    public void Removal_ContractsDeepPathsAndCanonicalizesEmptyRoot()
    {
        var trie = new ConcurrentHashTrie<int, int>();
        trie.SetItem(0, 0);
        trie.SetItem(1 << 30, 1);
        var snapshot = trie.Snapshot();
        Assert.True(trie.ValidateStructureForTesting().MaxDepth >= 6);

        Assert.True(trie.TryRemove(1 << 30, out var removed));
        Assert.Equal(1, removed);
        var singleton = trie.ValidateStructureForTesting();
        Assert.Equal(1, singleton.EntryCount);
        Assert.Equal(1, singleton.IndirectionNodeCount);
        Assert.Equal(0, singleton.TombNodeCount);

        Assert.True(trie.TryRemove(0, out _));
        var empty = trie.ValidateStructureForTesting();
        Assert.Equal(0, empty.EntryCount);
        Assert.Equal(1, empty.IndirectionNodeCount);
        Assert.Equal(0, empty.TombNodeCount);
        var revision = trie.Generation;
        trie.Clear();
        Assert.Equal(revision, trie.Generation);

        Assert.Equal(2, snapshot.Count);
        Assert.Equal(0, snapshot[0]);
        Assert.Equal(1, snapshot[1 << 30]);
    }

    /// <summary>Stresses collision mutation, tomb cleanup, and snapshots with a known final model.</summary>
    [Fact]
    public async Task ParallelRemoveReaddAndSnapshots_PreserveEveryFinalEntry()
    {
        var trie = new ConcurrentHashTrie<int, int>(HashBandComparer.Instance);
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
                    if (value != key && value != -key)
                        errors.Enqueue($"Unexpected intermediate value {key}/{value}.");
                }
            }
        });

        Parallel.For(0, 4_000, key =>
        {
            Assert.True(trie.TryAdd(key, key));
            Assert.True(trie.TryRemove(key, out var removed));
            Assert.Equal(key, removed);
            trie.SetItem(key, -key);
        });

        stop.Cancel();
        await reader;

        Assert.Empty(errors);
        Assert.Equal(4_000, trie.Count);
        for (var key = 0; key < 4_000; key++)
            Assert.Equal(-key, trie[key]);
        var statistics = trie.ValidateStructureForTesting();
        Assert.Equal(4_000, statistics.EntryCount);
        Assert.Equal(0, statistics.TombNodeCount);
    }

    private sealed class ConstantHashComparer : IEqualityComparer<int>
    {
        internal static readonly ConstantHashComparer Instance = new();
        public bool Equals(int x, int y) => x == y;
        public int GetHashCode(int obj) => 0;
    }

    private sealed class NullableCollisionComparer : IEqualityComparer<string?>
    {
        public bool Equals(string? left, string? right) =>
            StringComparer.OrdinalIgnoreCase.Equals(left, right);

        public int GetHashCode(string? value) => 0;
    }

    private sealed record RepresentativeValue(string Text);

    private sealed class CollisionThenSplitComparer : IEqualityComparer<int>
    {
        internal static readonly CollisionThenSplitComparer Instance = new();
        public bool Equals(int x, int y) => x == y;
        public int GetHashCode(int obj) => obj < 2 ? 0 : 32;
    }

    private sealed class HashBandComparer : IEqualityComparer<int>
    {
        internal static readonly HashBandComparer Instance = new();
        public bool Equals(int x, int y) => x == y;
        public int GetHashCode(int obj) => obj & 255;
    }
}
