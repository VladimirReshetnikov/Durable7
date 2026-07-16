using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Contract, representative, persistence, and invariant tests for the hash multimap.</summary>
public sealed class PersistentHashMultimapTests
{
    /// <summary>Verifies empty factories retain both comparer objects independently.</summary>
    [Fact]
    public void EmptyFactories_RetainIndependentComparers()
    {
        Assert.Same(PersistentHashMultimap<string, string>.Empty, PersistentHashMultimap<string, string>.Create());
        var keys = StringComparer.OrdinalIgnoreCase;
        var values = StringComparer.OrdinalIgnoreCase;
        var map = PersistentHashMultimap<string, string>.Create(keys, values);
        Assert.True(map.IsEmpty);
        Assert.Equal(0, map.KeyCount);
        Assert.Equal(0, map.PairCount);
        Assert.Same(keys, map.KeyComparer);
        Assert.Same(values, map.ValueComparer);
        Assert.NotSame(PersistentHashMultimap<string, string>.Empty, map);
        Assert.Same(map, map.Clear());
        map.ValidateInvariants();
    }

    /// <summary>Verifies pair insertion retains first representatives in both domains.</summary>
    [Fact]
    public void Add_RetainsFirstRepresentativesAndDuplicateIdentity()
    {
        var key = new Token(1, "key-first");
        var equalKey = new Token(1, "key-later");
        var value = new Token(10, "value-first");
        var equalValue = new Token(10, "value-later");
        var map = PersistentHashMultimap<Token, Token>
            .Create(TokenComparer.Instance, TokenComparer.Instance)
            .Add(key, value);

        Assert.Same(map, map.Add(equalKey, equalValue));
        Assert.False(map.TryAdd(equalKey, equalValue, out var unchanged));
        Assert.Same(map, unchanged);
        Assert.True(map.TryGetKey(equalKey, out var storedKey));
        Assert.Same(key, storedKey);
        Assert.True(map.TryGetValue(equalKey, equalValue, out var storedValue));
        Assert.Same(value, storedValue);
        Assert.Equal(1, map.KeyCount);
        Assert.Equal(1, map.PairCount);
        map.ValidateInvariants();
    }

    /// <summary>Verifies one key can own several values and pair counts remain distinct-pair counts.</summary>
    [Fact]
    public void MultipleValuesAndKeys_MaintainCountsAndGroups()
    {
        var map = PersistentHashMultimap<string, int>.Empty
            .Add("a", 1)
            .Add("a", 2)
            .Add("b", 2)
            .Add("b", 3)
            .Add("b", 3);

        Assert.Equal(2, map.KeyCount);
        Assert.Equal(4, map.PairCount);
        Assert.Equal(2, map.CountValues("a"));
        Assert.Equal(2, map.CountValues("b"));
        Assert.Equal(0, map.CountValues("missing"));
        Assert.True(map.Contains("a", 1));
        Assert.False(map.Contains("a", 3));
        Assert.Equal([1, 2], map.GetValues("a").Order());
        Assert.Equal(4, map.ToArray().Length);
        map.ValidateInvariants();
    }

    /// <summary>Verifies removing the last pair contracts the outer group and preserves policies.</summary>
    [Fact]
    public void Remove_ContractsEmptyGroupsAndReturnsIdentityOnMiss()
    {
        var map = PersistentHashMultimap<string, int>
            .Create(StringComparer.OrdinalIgnoreCase)
            .Add("Alpha", 1)
            .Add("Alpha", 2)
            .Add("Beta", 3);

        Assert.Same(map, map.Remove("alpha", 99));
        Assert.False(map.TryRemove("missing", 1, out var unchanged));
        Assert.Same(map, unchanged);

        var oneRemoved = map.Remove("ALPHA", 1);
        Assert.True(oneRemoved.ContainsKey("alpha"));
        Assert.Equal(1, oneRemoved.CountValues("alpha"));
        var groupRemoved = oneRemoved.Remove("alpha", 2);
        Assert.False(groupRemoved.ContainsKey("alpha"));
        Assert.Equal(1, groupRemoved.KeyCount);
        Assert.Equal(1, groupRemoved.PairCount);
        Assert.Same(map.KeyComparer, groupRemoved.KeyComparer);
        groupRemoved.ValidateInvariants();
    }

    /// <summary>Verifies whole-key removal returns the immutable value set and exact pair delta.</summary>
    [Fact]
    public void RemoveKey_ReturnsRemovedPersistentGroup()
    {
        var map = PersistentHashMultimap<int, string>.Empty
            .Add(1, "a")
            .Add(1, "b")
            .Add(2, "c");

        Assert.True(map.TryRemoveKey(1, out var result, out var values));
        Assert.Equal(["a", "b"], values.Order());
        Assert.Equal(1, result.KeyCount);
        Assert.Equal(1, result.PairCount);
        Assert.False(result.ContainsKey(1));
        Assert.True(map.ContainsKey(1));
        Assert.Same(result, result.RemoveKey(1));
        result.ValidateInvariants();
    }

    /// <summary>Verifies absent group reads return a value-policy-compatible empty set.</summary>
    [Fact]
    public void AbsentGroup_ReturnsComparerPreservingEmptySet()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var map = PersistentHashMultimap<int, string>.Create(valueComparer: comparer);
        Assert.False(map.TryGetValues(1, out var values));
        Assert.True(values.IsEmpty);
        Assert.Same(comparer, values.Comparer);
        Assert.Same(comparer, map.GetValues(1).Comparer);
    }

    /// <summary>Checks deterministic branching histories against a simple pair-set model.</summary>
    [Fact]
    public void BranchingHistory_MatchesPairSetModel()
    {
        var source = PersistentHashMultimap<int, int>.Empty;
        for (var key = 0; key < 40; key++)
            for (var value = 0; value < 6; value++)
                source = source.Add(key, value);

        var left = source;
        var right = source;
        for (var key = 0; key < 20; key++)
            left = left.Remove(key, key % 6).Add(key, 100 + key);
        for (var key = 20; key < 40; key++)
            right = right.RemoveKey(key).Add(key, 200 + key);

        Assert.Equal(240, source.PairCount);
        Assert.All(Enumerable.Range(0, 40), key => Assert.Equal(6, source.CountValues(key)));
        Assert.Equal(240, left.PairCount);
        Assert.Equal(140, right.PairCount);
        source.ValidateInvariants();
        left.ValidateInvariants();
        right.ValidateInvariants();
    }

    private sealed record Token(int Class, string Label);

    private sealed class TokenComparer : IEqualityComparer<Token>
    {
        internal static readonly TokenComparer Instance = new();

        public bool Equals(Token? x, Token? y) => x?.Class == y?.Class;

        public int GetHashCode(Token obj) => obj.Class;
    }
}
