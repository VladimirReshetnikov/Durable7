// Tests for the sorted dictionary.

using Xunit;
using FtSortedDictionary = Durable7.FingerTree.SortedDictionary<int, string>;
using BclSortedDictionary = System.Collections.Generic.SortedDictionary<int, string>;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies <see cref="Durable7.FingerTree.SortedDictionary{TKey, TValue}"/> against the BCL
/// <see cref="System.Collections.Generic.SortedDictionary{TKey, TValue}"/>: key order, lookup, set/add/remove
/// semantics, navigable-map queries, order-statistic access, and range extraction.
/// </summary>
public sealed class SortedDictionaryTests
{
    /// <summary>Verifies construction orders by key and applies last-wins on duplicate keys.</summary>
    [Fact]
    public void CreateRange_OrdersByKeyAndLastWins()
    {
        var map = FtSortedDictionary.CreateRange(new[]
        {
            new KeyValuePair<int, string>(3, "c"),
            new KeyValuePair<int, string>(1, "a"),
            new KeyValuePair<int, string>(2, "b"),
            new KeyValuePair<int, string>(1, "A"),   // duplicate key, later -> wins
        });

        Assert.Equal(3, map.Count);
        Assert.Equal(new[] { 1, 2, 3 }, map.Keys.ToArray());
        Assert.Equal("A", map[1]);
        Assert.Equal(new[] { "A", "b", "c" }, map.Values.ToArray());
    }

    /// <summary>Verifies set/add/remove semantics and lookup against the BCL sorted dictionary.</summary>
    [Fact]
    public void SetAddRemoveAndLookup_MatchBcl()
    {
        var map = FtSortedDictionary.Empty;
        var bcl = new BclSortedDictionary();

        foreach (var k in new[] { 5, 1, 3, 5, 2 })
        {
            map = map.SetItem(k, $"v{k}");
            bcl[k] = $"v{k}";
        }

        Assert.Equal(bcl.Count, map.Count);
        Assert.Equal(bcl.ToArray(), map.ToArray());
        foreach (var k in new[] { 0, 1, 2, 3, 4, 5, 6 })
        {
            Assert.Equal(bcl.ContainsKey(k), map.ContainsKey(k));
            Assert.Equal(bcl.TryGetValue(k, out var bv), map.TryGetValue(k, out var mv));
            Assert.Equal(bv, mv);
        }

        // Add throws on an existing key; TryAdd reports false; both leave the value intact.
        Assert.Throws<ArgumentException>(() => map.Add(3, "dup"));
        Assert.False(map.TryAdd(3, "dup", out var same));
        Assert.Equal(map.ToArray(), same.ToArray());
        Assert.True(map.TryAdd(9, "v9", out var added));
        Assert.Equal("v9", added[9]);

        // Remove.
        Assert.True(map.TryRemove(3, out var removed));
        Assert.False(removed.ContainsKey(3));
        Assert.Equal(map.ToArray(), map.Remove(99).ToArray());   // absent -> unchanged

        Assert.Throws<KeyNotFoundException>(() => map[42]);
    }

    /// <summary>Verifies order-statistic access (EntryAt/IndexOfKey) and endpoints.</summary>
    [Fact]
    public void OrderStatisticsAndEndpoints_AreCorrect()
    {
        var keys = new[] { 10, 20, 30, 40, 50 };
        var map = FtSortedDictionary.CreateRange(keys.Select(k => new KeyValuePair<int, string>(k, $"v{k}")));

        for (var i = 0; i < keys.Length; i++)
        {
            Assert.Equal(keys[i], map.EntryAt(i).Key);
            Assert.Equal(i, map.IndexOfKey(keys[i]));
        }

        Assert.Equal(-1, map.IndexOfKey(25));
        Assert.Throws<ArgumentOutOfRangeException>(() => map.EntryAt(keys.Length));
        Assert.Equal(10, map.MinEntry.Key);
        Assert.Equal(50, map.MaxEntry.Key);
    }

    /// <summary>Verifies the navigable-map neighbor queries against a key-array model.</summary>
    [Fact]
    public void NavigableQueries_MatchModel()
    {
        var keys = new[] { 2, 4, 6, 8 };
        var map = FtSortedDictionary.CreateRange(keys.Select(k => new KeyValuePair<int, string>(k, $"v{k}")));

        foreach (var probe in new[] { 1, 2, 5, 6, 9 })
        {
            Check(map.TryFloorEntry(probe, out var f), f, keys.Where(k => k <= probe).Cast<int?>().LastOrDefault());
            Check(map.TryCeilingEntry(probe, out var c), c, keys.Where(k => k >= probe).Cast<int?>().FirstOrDefault());
            Check(map.TryLowerEntry(probe, out var lo), lo, keys.Where(k => k < probe).Cast<int?>().LastOrDefault());
            Check(map.TryHigherEntry(probe, out var hi), hi, keys.Where(k => k > probe).Cast<int?>().FirstOrDefault());
        }

        static void Check(bool found, KeyValuePair<int, string> entry, int? expectedKey)
        {
            Assert.Equal(expectedKey.HasValue, found);
            if (expectedKey.HasValue)
            {
                Assert.Equal(expectedKey.Value, entry.Key);
                Assert.Equal($"v{expectedKey.Value}", entry.Value);
            }
        }
    }

    /// <summary>Verifies inclusive key-range extraction.</summary>
    [Fact]
    public void GetRange_ReturnsInclusiveKeyBand()
    {
        var map = FtSortedDictionary.CreateRange(
            Enumerable.Range(0, 20).Select(k => new KeyValuePair<int, string>(k * 2, $"v{k * 2}")));

        Assert.Equal(new[] { 6, 8, 10 }, map.GetRange(5, 11).Keys.ToArray());
        Assert.Empty(map.GetRange(100, 200).Keys.ToArray());
        Assert.Empty(map.GetRange(11, 5).Keys.ToArray());
    }

    /// <summary>Verifies a custom (reverse) key comparer is honored and preserved.</summary>
    [Fact]
    public void CustomKeyComparer_OrdersDescending()
    {
        var descending = Comparer<int>.Create((x, y) => y.CompareTo(x));
        var map = FtSortedDictionary.CreateRange(
            new[] { 1, 5, 3 }.Select(k => new KeyValuePair<int, string>(k, $"v{k}")),
            descending);

        Assert.Equal(new[] { 5, 3, 1 }, map.Keys.ToArray());
        Assert.Equal(5, map.MinEntry.Key);   // least key under the reversed order is the numeric maximum
        Assert.True(map.TryCeilingEntry(4, out var c));
        Assert.Equal(3, c.Key);              // least key (reversed order) that is >= 4 in that order is 3
    }

    /// <summary>Runs a deterministic mixed history against the BCL sorted dictionary.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    public void MixedHistory_MatchesBcl(int seed)
    {
        var random = new Random(seed);
        var bcl = new BclSortedDictionary();
        var map = FtSortedDictionary.Empty;

        for (var step = 0; step < 400; step++)
        {
            var key = random.Next(40);
            if (random.Next(3) != 0)
            {
                var value = $"s{step}";
                bcl[key] = value;
                map = map.SetItem(key, value);
            }
            else
            {
                bcl.Remove(key);
                map = map.Remove(key);
            }

            Assert.Equal(bcl.Count, map.Count);
            Assert.Equal(bcl.ToArray(), map.ToArray());
        }
    }

    /// <summary>Verifies it behaves as an <see cref="IReadOnlyDictionary{TKey, TValue}"/>.</summary>
    [Fact]
    public void ImplementsReadOnlyDictionary()
    {
        IReadOnlyDictionary<int, string> map = FtSortedDictionary.Empty.SetItem(1, "a").SetItem(2, "b");

        Assert.Equal(2, map.Count);
        Assert.True(map.ContainsKey(1));
        Assert.Equal("b", map[2]);
        Assert.Equal(new[] { 1, 2 }, map.Keys.ToArray());
        Assert.Equal(new[] { "a", "b" }, map.Values.ToArray());
        Assert.Equal(
            new[] { new KeyValuePair<int, string>(1, "a"), new KeyValuePair<int, string>(2, "b") },
            map.ToArray());
    }

    /// <summary>Verifies emptying a default-comparer dictionary collapses to the shared <c>Empty</c> singleton.</summary>
    [Fact]
    public void EmptyingWithDefaultComparer_CollapsesToSingleton()
    {
        var map = FtSortedDictionary.Empty.SetItem(7, "v").Remove(7);
        Assert.True(map.IsEmpty);
        Assert.Same(FtSortedDictionary.Empty, map);
    }

    /// <summary>Verifies emptying a custom-comparer dictionary keeps its own comparer (no singleton collapse).</summary>
    [Fact]
    public void EmptyingWithCustomComparer_PreservesComparer()
    {
        var descending = Comparer<int>.Create((x, y) => y.CompareTo(x));
        var map = FtSortedDictionary.Create(descending).SetItem(7, "v").Remove(7);
        Assert.True(map.IsEmpty);
        Assert.NotSame(FtSortedDictionary.Empty, map);
        Assert.Same(descending, map.Comparer);
    }

    /// <summary>Verifies empty-dictionary degenerate behavior.</summary>
    [Fact]
    public void Empty_HasNoEntries()
    {
        var empty = FtSortedDictionary.Empty;
        Assert.True(empty.IsEmpty);
        Assert.Empty(empty);
        Assert.False(empty.ContainsKey(1));
        Assert.False(empty.TryGetValue(1, out _));
        Assert.Equal(-1, empty.IndexOfKey(1));
        Assert.False(empty.TryCeilingEntry(1, out _));
        Assert.Throws<InvalidOperationException>(() => empty.MinEntry);
    }
}
