using System.Collections.Concurrent;
using Xunit;
using Map = Tools.DataStructures.Hamt.PersistentHashMap<int, string>;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>
/// Deterministic API contract tests for <see cref="PersistentHashMap{TKey, TValue}"/>.
/// </summary>
public sealed class PersistentHashMapTests
{
    /// <summary>Verifies empty-map behavior and no-op identity preservation.</summary>
    [Fact]
    public void Empty_HasNoEntries()
    {
        var empty = Map.Empty;

        Assert.True(empty.IsEmpty);
        Assert.Empty(empty);
        Assert.False(empty.ContainsKey(1));
        Assert.False(empty.TryGetValue(1, out _));
        Assert.Same(empty, empty.Remove(1));
        Assert.Same(empty, empty.Clear());
        Assert.Throws<KeyNotFoundException>(() => empty[1]);
    }

    /// <summary>Verifies add, replace, lookup, and persistence of older versions.</summary>
    [Fact]
    public void SetItem_AddsReplacesAndPreservesOldVersions()
    {
        var empty = Map.Empty;
        var one = empty.SetItem(1, "one");
        var two = one.SetItem(2, "two");
        var replaced = two.SetItem(1, "uno");

        Assert.Empty(empty);
        Assert.Equal(new[] { new KeyValuePair<int, string>(1, "one") }, one.ToArray());
        Assert.Equal("one", two[1]);
        Assert.Equal("two", two[2]);
        Assert.Equal("uno", replaced[1]);
        Assert.Equal("two", replaced[2]);
        Assert.Same(replaced, replaced.SetItem(1, "uno"));
    }

    /// <summary>Verifies duplicate-rejecting add APIs.</summary>
    [Fact]
    public void AddAndTryAdd_RejectDuplicates()
    {
        var map = Map.Empty.Add(1, "one");

        Assert.Throws<ArgumentException>(() => map.Add(1, "duplicate"));
        Assert.False(map.TryAdd(1, "duplicate", out var same));
        Assert.Same(map, same);

        Assert.True(map.TryAdd(2, "two", out var added));
        Assert.Equal("two", added[2]);
        Assert.False(map.ContainsKey(2));
    }

    /// <summary>Verifies remove APIs and removed-value reporting.</summary>
    [Fact]
    public void RemoveAndTryRemove_DeletePresentKeys()
    {
        var map = Map.Empty.SetItem(1, "one").SetItem(2, "two");

        Assert.True(map.TryRemove(1, out var removed, out var value));
        Assert.Equal("one", value);
        Assert.False(removed.ContainsKey(1));
        Assert.True(map.ContainsKey(1));

        Assert.False(removed.TryRemove(9, out var same, out var missing));
        Assert.Same(removed, same);
        Assert.Null(missing);
        Assert.Same(removed, removed.Remove(9));
    }

    /// <summary>Verifies range creation, last-wins values, and first equivalent key retention.</summary>
    [Fact]
    public void CreateRange_LastWinsAndRetainsFirstEquivalentKey()
    {
        var map = PersistentHashMap<string, int>.CreateRange(
            new[]
            {
                new KeyValuePair<string, int>("Alpha", 1),
                new KeyValuePair<string, int>("beta", 2),
                new KeyValuePair<string, int>("ALPHA", 3),
            },
            StringComparer.OrdinalIgnoreCase);

        Assert.Equal(2, map.Count);
        Assert.Equal(3, map["alpha"]);
        Assert.Equal("Alpha", map.Single(kv => kv.Value == 3).Key);
        Assert.True(map.ContainsKey("BETA"));
    }

    /// <summary>Verifies null reference keys follow the configured comparer semantics.</summary>
    [Fact]
    public void NullReferenceKey_IsSupportedWhenComparerSupportsIt()
    {
        var map = PersistentHashMap<string?, int>.Empty
            .SetItem(null, 1)
            .SetItem("value", 2);

        Assert.True(map.ContainsKey(null));
        Assert.Equal(1, map[null]);
        Assert.True(map.TryRemove(null, out var removed, out var value));
        Assert.Equal(1, value);
        Assert.False(removed.ContainsKey(null));
        Assert.Equal(2, removed["value"]);
    }

    /// <summary>Verifies that custom-comparer empty maps preserve their comparer.</summary>
    [Fact]
    public void Clear_PreservesCustomComparer()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var map = PersistentHashMap<string, int>.Create(comparer).SetItem("a", 1);
        var cleared = map.Clear();

        Assert.True(cleared.IsEmpty);
        Assert.Same(comparer, cleared.Comparer);
        Assert.NotSame(PersistentHashMap<string, int>.Empty, cleared);
        Assert.Same(cleared, cleared.Clear());
        Assert.Same(PersistentHashMap<string, int>.Empty, PersistentHashMap<string, int>.Empty.SetItem("x", 1).Remove("x"));
        Assert.Same(Map.Empty, Map.Create());
    }

    /// <summary>Verifies that stored key objects are recoverable through <c>TryGetKey</c>.</summary>
    [Fact]
    public void TryGetKey_ReturnsStoredKeyObject()
    {
        var storedKey = new string(['A', 'l', 'p', 'h', 'a']);
        var map = PersistentHashMap<string, int>.Create(StringComparer.OrdinalIgnoreCase)
            .SetItem(storedKey, 1);

        Assert.True(map.TryGetKey("ALPHA", out var actualKey));
        Assert.Same(storedKey, actualKey);

        var missing = "gamma";
        Assert.False(map.TryGetKey(missing, out var fallback));
        Assert.Same(missing, fallback);
    }

    /// <summary>Verifies every sequence-accepting member rejects null arguments.</summary>
    [Fact]
    public void SequenceArguments_RejectNull()
    {
        Assert.Throws<ArgumentNullException>(() => PersistentHashMap<int, string>.CreateRange(null!));
        Assert.Throws<ArgumentNullException>(() => Map.Empty.SetItems(null!));
    }

    /// <summary>Verifies the Keys and Values views align with pair enumeration in trie order.</summary>
    [Fact]
    public void KeysAndValues_AlignWithPairEnumeration()
    {
        var map = Map.Empty.SetItem(5, "five").SetItem(2, "two").SetItem(7, "seven");

        Assert.Equal(map.Select(kv => kv.Key), map.Keys);
        Assert.Equal(map.Select(kv => kv.Value), map.Values);
        Assert.Equal(map, map.Keys.Zip(map.Values, KeyValuePair.Create));
    }

    /// <summary>Verifies <see cref="IReadOnlyDictionary{TKey, TValue}"/> behavior.</summary>
    [Fact]
    public void ImplementsReadOnlyDictionary()
    {
        IReadOnlyDictionary<int, string> map = Map.Empty.SetItem(2, "two").SetItem(1, "one");

        Assert.Equal(2, map.Count);
        Assert.True(map.ContainsKey(1));
        Assert.Equal("two", map[2]);
        Assert.Equal(new[] { 1, 2 }, map.Keys.OrderBy(x => x).ToArray());
        Assert.Equal(new[] { "one", "two" }, map.OrderBy(kv => kv.Key).Select(kv => kv.Value).ToArray());
    }

    /// <summary>Verifies adding or replacing a sequence through <c>SetItems</c>.</summary>
    [Fact]
    public void SetItems_AppliesAllEntriesInOrder()
    {
        var map = Map.Empty
            .SetItem(1, "old")
            .SetItems(new[]
            {
                new KeyValuePair<int, string>(1, "new"),
                new KeyValuePair<int, string>(2, "two"),
            });

        Assert.Equal(new[]
        {
            new KeyValuePair<int, string>(1, "new"),
            new KeyValuePair<int, string>(2, "two"),
        }, map.OrderBy(kv => kv.Key).ToArray());
    }

    /// <summary>Verifies retained immutable snapshots are safe for concurrent readers.</summary>
    [Fact]
    public void ConcurrentReaders_ObserveConsistentRetainedSnapshot()
    {
        var map = Map.Empty;
        for (var key = 0; key < 512; key++)
            map = map.SetItem(key, $"v{key}");

        Parallel.For(0, Environment.ProcessorCount * 4, _ =>
        {
            for (var pass = 0; pass < 64; pass++)
            {
                Assert.Equal(512, map.Count);
                for (var key = 0; key < 512; key += 17)
                {
                    Assert.True(map.TryGetValue(key, out var value));
                    Assert.Equal($"v{key}", value);
                }

                var enumerated = 0;
                foreach (var (key, value) in map)
                {
                    Assert.Equal($"v{key}", value);
                    enumerated++;
                }

                Assert.Equal(512, enumerated);
            }
        });
    }

    /// <summary>Verifies lock-free publication of immutable versions exposes only valid snapshots.</summary>
    [Fact]
    public async Task ConcurrentPublication_ReadersSeeValidSnapshots()
    {
        var published = Map.Empty;
        var done = 0;
        var failures = new ConcurrentQueue<Exception>();

        var readers = Enumerable.Range(0, Environment.ProcessorCount * 2)
            .Select(_ => Task.Run(() =>
            {
                try
                {
                    while (Volatile.Read(ref done) == 0)
                        AssertContiguousSnapshot(Volatile.Read(ref published));

                    AssertContiguousSnapshot(Volatile.Read(ref published));
                }
                catch (Exception ex)
                {
                    failures.Enqueue(ex);
                }
            }))
            .ToArray();

        var writer = Task.Run(() =>
        {
            var map = Map.Empty;
            for (var key = 0; key < 256; key++)
            {
                map = map.SetItem(key, $"v{key}");
                Volatile.Write(ref published, map);
            }

            Volatile.Write(ref done, 1);
        });

        await Task.WhenAll(readers.Append(writer));
        Assert.Empty(failures);
    }

    private static void AssertContiguousSnapshot(Map map)
    {
        Assert.InRange(map.Count, 0, 256);
        for (var key = 0; key < map.Count; key++)
            Assert.Equal($"v{key}", map[key]);

        foreach (var (key, value) in map)
        {
            Assert.InRange(key, 0, map.Count - 1);
            Assert.Equal($"v{key}", value);
        }
    }
}
