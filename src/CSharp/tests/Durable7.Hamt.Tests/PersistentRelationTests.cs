using Durable7.Hamt;
using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Verifies the persistent many-to-many relation and its inverse indexes.</summary>
public sealed class PersistentRelationTests
{
    /// <summary>Verifies factories retain independent policies and shared default emptiness.</summary>
    [Fact]
    public void Create_PreservesIndependentComparers()
    {
        Assert.Same(PersistentRelation<string, string>.Empty, PersistentRelation<string, string>.Create());
        var left = StringComparer.OrdinalIgnoreCase;
        var right = StringComparer.InvariantCultureIgnoreCase;
        var relation = PersistentRelation<string, string>.Create(left, right);

        Assert.Same(left, relation.LeftComparer);
        Assert.Same(right, relation.RightComparer);
        Assert.True(relation.IsEmpty);
        relation.ValidateInvariants();
    }

    /// <summary>Verifies many-to-many adjacency and pair counting in both directions.</summary>
    [Fact]
    public void Add_FormsManyToManyRelation()
    {
        var relation = PersistentRelation<string, int>.Empty
            .Add("a", 1)
            .Add("a", 2)
            .Add("b", 1);

        Assert.Equal(2, relation.LeftCount);
        Assert.Equal(2, relation.RightCount);
        Assert.Equal(3, relation.PairCount);
        Assert.Equal(new[] { 1, 2 }, relation.GetRights("a").Order());
        Assert.Equal(new[] { "a", "b" }, relation.GetLefts(1).Order());
        Assert.Equal(2, relation.CountRights("a"));
        Assert.Equal(2, relation.CountLefts(1));
        Assert.True(relation.TryGetRights("a", out var rights));
        Assert.Equal(new[] { 1, 2 }, rights.Order());
        Assert.True(relation.TryGetLefts(1, out var lefts));
        Assert.Equal(new[] { "a", "b" }, lefts.Order());
        Assert.True(relation.Contains("b", 1));
        Assert.False(relation.Contains("b", 2));
        relation.ValidateInvariants();
    }

    /// <summary>Verifies duplicate pairs are identity-preserving no-ops.</summary>
    [Fact]
    public void Add_EquivalentPairReturnsReceiver()
    {
        var relation = PersistentRelation<string, string>
            .Create(StringComparer.OrdinalIgnoreCase, StringComparer.OrdinalIgnoreCase)
            .Add("Left", "Right");

        Assert.Same(relation, relation.Add("LEFT", "RIGHT"));
        Assert.False(relation.TryAdd("left", "right", out var unchanged));
        Assert.Same(relation, unchanged);
    }

    /// <summary>Verifies first representatives are global across all adjacency groups.</summary>
    [Fact]
    public void Add_ReusesGlobalRepresentativesAcrossGroups()
    {
        var relation = PersistentRelation<Token, Token>
            .Create(TokenComparer.Instance, TokenComparer.Instance)
            .Add(new("left-a", "stored-left-a"), new("right", "stored-right"))
            .Add(new("left-b", "stored-left-b"), new("RIGHT", "caller-right"))
            .Add(new("LEFT-A", "caller-left-a"), new("other", "stored-other"));

        Assert.True(relation.TryGetRight(new("right", "probe"), out var right));
        Assert.Equal("stored-right", right.Representation);
        Assert.True(relation.TryGetLeft(new("left-a", "probe"), out var left));
        Assert.Equal("stored-left-a", left.Representation);
        Assert.All(relation.GetLefts(new("right", "probe")), item =>
            Assert.Contains(item.Representation, new[] { "stored-left-a", "stored-left-b" }));
        Assert.Contains(relation.GetRights(new("left-b", "probe")), item =>
            item.Representation == "stored-right");
        relation.ValidateInvariants();
    }

    /// <summary>Verifies the cached inverse swaps indexes and has involutive identity.</summary>
    [Fact]
    public void Inverse_IsCachedConstantTimeView()
    {
        var relation = PersistentRelation<string, int>.Empty.Add("a", 1).Add("b", 1);
        var inverse = relation.Inverse;

        Assert.Same(inverse, relation.Inverse);
        Assert.Same(relation, inverse.Inverse);
        Assert.True(inverse.Contains(1, "a"));
        Assert.Equal(relation.PairCount, inverse.PairCount);
        Assert.Same(relation.LeftComparer, inverse.RightComparer);
        Assert.Same(relation.RightComparer, inverse.LeftComparer);
        inverse.ValidateInvariants();
    }

    /// <summary>Verifies pair removal contracts empty groups in both indexes.</summary>
    [Fact]
    public void RemovePair_UpdatesBothDomainsAndPreservesSource()
    {
        var source = PersistentRelation<string, int>.Empty.Add("a", 1).Add("a", 2).Add("b", 1);

        Assert.True(source.TryRemove("b", 1, out var withoutPair));
        Assert.False(withoutPair.ContainsLeft("b"));
        Assert.True(withoutPair.ContainsRight(1));
        Assert.Equal(2, withoutPair.PairCount);
        Assert.False(withoutPair.TryRemove("b", 1, out var unchanged));
        Assert.Same(withoutPair, unchanged);
        Assert.Equal(3, source.PairCount);
        withoutPair.ValidateInvariants();
    }

    /// <summary>Verifies whole-domain removals return their immutable adjacency sets.</summary>
    [Fact]
    public void RemoveWholeSide_IsSymmetric()
    {
        var source = PersistentRelation<string, int>.Empty.Add("a", 1).Add("a", 2).Add("b", 1);

        Assert.True(source.TryRemoveLeft("a", out var noA, out var rights));
        Assert.Equal(new[] { 1, 2 }, rights.Order());
        Assert.Single(noA);
        Assert.False(noA.ContainsRight(2));
        Assert.True(source.TryRemoveRight(1, out var noOne, out var lefts));
        Assert.Equal(new[] { "a", "b" }, lefts.Order());
        Assert.Single(noOne);
        Assert.True(noOne.Contains("a", 2));
        noA.ValidateInvariants();
        noOne.ValidateInvariants();
    }

    /// <summary>Verifies absent removal and clear preserve identity and policy.</summary>
    [Fact]
    public void AbsentRemovalAndClear_PreserveIdentityAndPolicy()
    {
        var left = StringComparer.OrdinalIgnoreCase;
        var relation = PersistentRelation<string, int>.Create(left).Add("a", 1);

        Assert.Same(relation, relation.Remove("missing", 1));
        Assert.Same(relation, relation.RemoveLeft("missing"));
        Assert.Same(relation, relation.RemoveRight(9));
        var empty = relation.Clear();
        Assert.Same(left, empty.LeftComparer);
        Assert.Same(relation.RightComparer, empty.RightComparer);
        Assert.Same(empty, empty.Clear());
    }

    /// <summary>Verifies range construction and retained branching histories.</summary>
    [Fact]
    public void CreateRangeAndBranches_RemainIndependent()
    {
        var root = PersistentRelation<int, string>.CreateRange(
            [KeyValuePair.Create(1, "a"), KeyValuePair.Create(1, "a"), KeyValuePair.Create(2, "b")]);
        var left = root.Add(1, "b");
        var right = root.Remove(1, "a").Add(3, "c");

        Assert.Equal(2, root.PairCount);
        Assert.Equal(3, left.PairCount);
        Assert.Equal(2, right.PairCount);
        Assert.False(root.Contains(1, "b"));
        Assert.True(left.Contains(1, "b"));
        Assert.True(right.Contains(3, "c"));
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
