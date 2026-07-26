// Tests for the persistent ordered multimap.

using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>Verifies grouped ordering, retained policies, representatives, and persistence.</summary>
public sealed class PersistentOrderedMultimapTests
{
    /// <summary>Verifies empty factories retain independent key and value policies.</summary>
    [Fact]
    public void Create_RetainsIndependentComparers()
    {
        Assert.Same(PersistentOrderedMultimap<string, string>.Empty,
            PersistentOrderedMultimap<string, string>.Create());

        var keys = StringComparer.OrdinalIgnoreCase;
        var values = StringComparer.InvariantCultureIgnoreCase;
        var map = PersistentOrderedMultimap<string, string>.Create(keys, values);

        Assert.True(map.IsEmpty);
        Assert.Same(keys, map.KeyComparer);
        Assert.Same(values, map.ValueComparer);
        Assert.Same(map, map.Clear());
        Assert.Same(values, map.GetValues("missing").Comparer);
        map.ValidateInvariants();
    }

    /// <summary>Verifies enumeration is grouped by first key and per-group value insertion order.</summary>
    [Fact]
    public void Add_EnumeratesInGroupedInsertionOrder()
    {
        var map = PersistentOrderedMultimap<string, int>.Empty
            .Add("b", 2)
            .Add("a", 9)
            .Add("b", 1)
            .Add("c", 7)
            .Add("a", 8);

        Assert.Equal(["b", "a", "c"], map.Keys);
        Assert.Equal(
            [
                KeyValuePair.Create("b", 2),
                KeyValuePair.Create("b", 1),
                KeyValuePair.Create("a", 9),
                KeyValuePair.Create("a", 8),
                KeyValuePair.Create("c", 7),
            ],
            map.ToArray());
        Assert.Equal(3, map.KeyCount);
        Assert.Equal(5, map.PairCount);
        map.ValidateInvariants();
    }

    /// <summary>Verifies duplicate pairs preserve identity and strict try-add reporting.</summary>
    [Fact]
    public void Add_EquivalentPairIsIdentityNoOp()
    {
        var map = PersistentOrderedMultimap<string, string>
            .Create(StringComparer.OrdinalIgnoreCase, StringComparer.OrdinalIgnoreCase)
            .Add("Key", "Value");

        Assert.Same(map, map.Add("KEY", "VALUE"));
        Assert.False(map.TryAdd("key", "value", out var unchanged));
        Assert.Same(map, unchanged);
    }

    /// <summary>Verifies first key and first per-group value representatives are retained.</summary>
    [Fact]
    public void Add_RetainsFirstRepresentatives()
    {
        var firstKey = new Token("key", "stored-key");
        var firstValue = new Token("value", "stored-value");
        var map = PersistentOrderedMultimap<Token, Token>
            .Create(TokenComparer.Instance, TokenComparer.Instance)
            .Add(firstKey, firstValue)
            .Add(new("KEY", "caller-key"), new("other", "other-value"));

        Assert.True(map.TryGetKey(new("key", "probe"), out var actualKey));
        Assert.Same(firstKey, actualKey);
        Assert.True(map.TryGetValue(new("KEY", "probe"), new("VALUE", "probe"), out var actualValue));
        Assert.Same(firstValue, actualValue);
    }

    /// <summary>Verifies removing the final pair contracts a group and re-adding appends it.</summary>
    [Fact]
    public void Remove_ContractsAndReappendsKeyGroup()
    {
        var source = PersistentOrderedMultimap<string, int>.Empty
            .Add("a", 1)
            .Add("b", 2)
            .Add("a", 3);
        var withoutA = source.Remove("a", 1).Remove("a", 3);

        Assert.Equal(["b"], withoutA.Keys);
        Assert.Equal(1, withoutA.PairCount);
        var readded = withoutA.Add("a", 4);
        Assert.Equal(["b", "a"], readded.Keys);
        Assert.Equal(3, source.PairCount);
        Assert.True(source.Contains("a", 1));
    }

    /// <summary>Verifies whole-group removal updates pair count and absent removals preserve identity.</summary>
    [Fact]
    public void RemoveKey_UpdatesPairCountAndPreservesAbsentIdentity()
    {
        var map = PersistentOrderedMultimap<int, int>.Empty
            .Add(1, 10)
            .Add(1, 11)
            .Add(2, 20);

        Assert.True(map.TryRemoveKey(1, out var reduced));
        Assert.Equal(1, reduced.KeyCount);
        Assert.Equal(1, reduced.PairCount);
        Assert.False(reduced.TryRemoveKey(1, out var unchanged));
        Assert.Same(reduced, unchanged);
        Assert.Same(reduced, reduced.Remove(2, 999));
    }

    /// <summary>Verifies range construction deduplicates and retained snapshots branch independently.</summary>
    [Fact]
    public void CreateRangeAndBranches_AreIndependent()
    {
        var root = PersistentOrderedMultimap<int, string>.CreateRange(
            [
                KeyValuePair.Create(1, "a"),
                KeyValuePair.Create(1, "a"),
                KeyValuePair.Create(2, "b"),
            ]);
        var left = root.Add(1, "c");
        var right = root.RemoveKey(1).Add(3, "d");

        Assert.Equal(2, root.PairCount);
        Assert.Equal(3, left.PairCount);
        Assert.Equal(2, right.PairCount);
        Assert.False(root.Contains(1, "c"));
        Assert.True(left.Contains(1, "c"));
        Assert.True(right.Contains(3, "d"));
        root.ValidateInvariants();
        left.ValidateInvariants();
        right.ValidateInvariants();
    }

    private sealed record Token(string Class, string Representation);

    private sealed class TokenComparer : IEqualityComparer<Token>
    {
        internal static TokenComparer Instance { get; } = new();

        public bool Equals(Token? x, Token? y) =>
            StringComparer.OrdinalIgnoreCase.Equals(x?.Class, y?.Class);

        public int GetHashCode(Token obj) => StringComparer.OrdinalIgnoreCase.GetHashCode(obj.Class);
    }
}
