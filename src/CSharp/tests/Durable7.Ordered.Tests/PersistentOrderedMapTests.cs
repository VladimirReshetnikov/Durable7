using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>Contract, persistence, ordering, relabel, and comparer tests for the ordered map.</summary>
public sealed class PersistentOrderedMapTests
{
    /// <summary>Verifies empty factories retain both policies and canonicalize only both defaults.</summary>
    [Fact]
    public void EmptyFactories_RetainIndependentPolicies()
    {
        Assert.Same(PersistentOrderedMap<string, string>.Empty, PersistentOrderedMap<string, string>.Create());

        var keys = StringComparer.OrdinalIgnoreCase;
        var values = StringComparer.OrdinalIgnoreCase;
        var map = PersistentOrderedMap<string, string>.Create(keys, values);
        Assert.True(map.IsEmpty);
        Assert.Same(keys, map.KeyComparer);
        Assert.Same(values, map.ValueComparer);
        Assert.NotSame(PersistentOrderedMap<string, string>.Empty, map);
        Assert.Same(map, map.Clear());
        map.ValidateInvariants();
    }

    /// <summary>Verifies construction keeps first key position/representative and the last value.</summary>
    [Fact]
    public void CreateRange_UsesFirstKeyAndPositionWithLastValue()
    {
        var first = new Key(1, "first");
        var second = new Key(2, "second");
        var laterFirst = new Key(1, "later");
        var map = PersistentOrderedMap<Key, string>.CreateRange(
            [KeyValuePair.Create(first, "one"), KeyValuePair.Create(second, "two"), KeyValuePair.Create(laterFirst, "ONE")],
            KeyComparer.Instance,
            StringComparer.Ordinal);

        Assert.Equal([first, second], map.Keys);
        Assert.Equal(["ONE", "two"], map.Values);
        Assert.True(map.TryGetKey(laterFirst, out var stored));
        Assert.Same(first, stored);
        Assert.Equal("ONE", map[laterFirst]);
        Assert.Equal(0, map.IndexOfKey(laterFirst));
        map.ValidateInvariants();
    }

    /// <summary>Verifies equal-value writes are identity no-ops while changed values keep position and key.</summary>
    [Fact]
    public void SetItem_UsesConfiguredValueComparerAndKeepsPosition()
    {
        var first = new Key(1, "first");
        var second = new Key(2, "second");
        var source = PersistentOrderedMap<Key, string>
            .Create(KeyComparer.Instance, StringComparer.OrdinalIgnoreCase)
            .Add(first, "one")
            .Add(second, "two");

        Assert.Same(source, source.SetItem(new Key(1, "lookup"), "ONE"));
        var changed = source.SetItem(new Key(1, "replacement"), "uno");
        Assert.Equal([first, second], changed.Keys);
        Assert.Equal(["uno", "two"], changed.Values);
        Assert.Same(first, changed.EntryAt(0).Key);
        Assert.Equal(["one", "two"], source.Values);
        changed.ValidateInvariants();
    }

    /// <summary>Verifies strict addition, try-add, insertion, and explicit movement remain separate.</summary>
    [Fact]
    public void AdditionAndMovement_PreserveExplicitOrderingContract()
    {
        var map = PersistentOrderedMap<int, string>.Empty
            .Add(1, "one")
            .Add(2, "two")
            .AddFirst(0, "zero")
            .Insert(2, 3, "three");
        AssertPairs([(0, "zero"), (1, "one"), (3, "three"), (2, "two")], map);

        Assert.Throws<ArgumentException>(() => map.Add(1, "duplicate"));
        Assert.False(map.TryAdd(1, "duplicate", out var unchanged));
        Assert.Same(map, unchanged);

        var moved = map.MoveToFirst(2).MoveTo(2, 0).MoveToLast(1);
        AssertPairs([(2, "two"), (0, "zero"), (3, "three"), (1, "one")], moved);
        AssertPairs([(0, "zero"), (1, "one"), (3, "three"), (2, "two")], map);
        moved.ValidateInvariants();
    }

    /// <summary>Verifies removal, ranges, reversal, and retained source snapshots.</summary>
    [Fact]
    public void RemovalRangesAndReverse_PreserveSnapshotsAndPolicies()
    {
        var map = PersistentOrderedMap<int, string>.CreateRange(
            Enumerable.Range(0, 8).Select(i => KeyValuePair.Create(i, $"v{i}")));

        Assert.Same(map, map.Remove(100));
        Assert.True(map.TryRemove(3, out var removed, out var value));
        Assert.Equal("v3", value);
        AssertPairs([(0, "v0"), (1, "v1"), (2, "v2"), (4, "v4"), (5, "v5"), (6, "v6"), (7, "v7")], removed);
        AssertPairs([(2, "v2"), (3, "v3"), (4, "v4"), (5, "v5")], map.GetRange(2, 4));
        AssertPairs([(0, "v0"), (1, "v1"), (2, "v2")], map.Take(3));
        AssertPairs([(6, "v6"), (7, "v7")], map.Drop(6));
        Assert.Equal(Enumerable.Range(0, 8).Reverse(), map.Reverse().Keys);
        Assert.Equal(Enumerable.Range(0, 8), map.Keys);
        map.ValidateInvariants();
    }

    /// <summary>Exercises repeated same-position insertion through several sparse-label relabels.</summary>
    [Fact]
    public void RepeatedMiddleInsertion_RelabelsWithoutLosingIndexAgreement()
    {
        var map = PersistentOrderedMap<int, int>.Empty.Add(-1, -1).Add(-2, -2);
        for (var value = 0; value < 180; value++)
        {
            var previous = map;
            map = map.Insert(1, value, value * 10);
            Assert.Equal([-1, .. Enumerable.Range(0, value + 1).Reverse(), -2], map.Keys);
            Assert.False(previous.ContainsKey(value));
            map.ValidateInvariants();
        }

        for (var value = 0; value < 180; value++)
        {
            Assert.Equal(value * 10, map[value]);
            Assert.Equal(180 - value, map.IndexOfKey(value));
        }
    }

    /// <summary>Verifies independent branches can edit and reorder without changing their source.</summary>
    [Fact]
    public void BranchingHistories_RemainIndependent()
    {
        var source = PersistentOrderedMap<int, string>.CreateRange(
            Enumerable.Range(0, 20).Select(i => KeyValuePair.Create(i, i.ToString())));
        var left = source.SetItem(5, "left").MoveToFirst(5).Remove(6);
        var right = source.SetItem(5, "right").MoveToLast(5).Add(20, "twenty");

        Assert.Equal("5", source[5]);
        Assert.Equal(0, source.First.Key);
        Assert.Equal(19, source.Last.Key);
        Assert.Equal("left", left[5]);
        Assert.Equal(5, left.First.Key);
        Assert.False(left.ContainsKey(6));
        Assert.Equal("right", right[5]);
        Assert.Equal(20, right.Last.Key);
        source.ValidateInvariants();
        left.ValidateInvariants();
        right.ValidateInvariants();
    }

    private static void AssertPairs(
        IEnumerable<(int Key, string Value)> expected,
        PersistentOrderedMap<int, string> actual)
    {
        Assert.Equal(expected.Select(pair => KeyValuePair.Create(pair.Key, pair.Value)), actual);
        Assert.Equal(actual.Select(pair => pair.Key), actual.Keys);
        Assert.Equal(actual.Select(pair => pair.Value), actual.Values);
        actual.ValidateInvariants();
    }

    private sealed record Key(int Class, string Label);

    private sealed class KeyComparer : IEqualityComparer<Key>
    {
        internal static readonly KeyComparer Instance = new();

        public bool Equals(Key? x, Key? y) => x?.Class == y?.Class;

        public int GetHashCode(Key obj) => obj.Class;
    }
}
