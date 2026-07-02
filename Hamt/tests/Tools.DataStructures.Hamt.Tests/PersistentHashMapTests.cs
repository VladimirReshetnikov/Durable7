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
        Assert.Same(PersistentHashMap<string, int>.Empty, PersistentHashMap<string, int>.Empty.SetItem("x", 1).Remove("x"));
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
}
