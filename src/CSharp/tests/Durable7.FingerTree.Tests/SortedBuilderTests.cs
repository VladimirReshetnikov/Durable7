// Tests for the sorted builder.

using Xunit;
using FtSortedSet = Durable7.FingerTree.SortedSet<int>;
using FtSortedDictionary = Durable7.FingerTree.SortedDictionary<int, string>;

namespace Durable7.FingerTree.Tests;

/// <summary>Validates mutable builders for the sorted set and sorted dictionary facades.</summary>
public sealed class SortedBuilderTests
{
    /// <summary>Verifies sorted-set builder mutations, no-op invalidation, caching, and snapshot isolation.</summary>
    [Fact]
    public void SortedSetBuilder_MutationsFreezeAndCacheCorrectly()
    {
        var builder = FtSortedSet.CreateBuilder();
        var empty = builder.ToImmutable();

        Assert.Same(FtSortedSet.Empty, empty);
        Assert.Same(empty, builder.ToImmutable());
        Assert.True(builder.Add(3));
        Assert.True(builder.Add(1));
        Assert.True(builder.Add(5));
        Assert.False(builder.Add(3));

        var first = builder.ToImmutable();
        first.ValidateInvariants();
        Assert.Equal(new[] { 1, 3, 5 }, first.ToArray());
        Assert.Same(first, builder.ToImmutable());

        Assert.False(builder.Remove(42));
        Assert.Same(first, builder.ToImmutable());

        builder.UnionWith(new[] { 2, 4, 5 });
        var second = builder.ToImmutable();
        second.ValidateInvariants();
        Assert.Equal(new[] { 1, 2, 3, 4, 5 }, second.ToArray());
        Assert.Equal(new[] { 1, 3, 5 }, first.ToArray());

        builder.ExceptWith(new[] { 1, 5, 9 });
        Assert.Equal(new[] { 2, 3, 4 }, builder.ToImmutable().ToArray());
    }

    /// <summary>Verifies sorted-set builder comparer preservation and comparer-equal representative behavior.</summary>
    [Fact]
    public void SortedSetBuilder_PreservesComparerAndRepresentatives()
    {
        var byLength = Comparer<string>.Create((left, right) => left.Length.CompareTo(right.Length));
        var set = Durable7.FingerTree.SortedSet<string>.CreateRange(new[] { "aa" }, byLength);
        var builder = set.ToBuilder();

        Assert.Same(set, builder.ToImmutable());
        Assert.Same(byLength, builder.Comparer);
        Assert.False(builder.Add("bb"));
        Assert.Equal("aa", Assert.Single(builder));

        Assert.True(builder.Remove("cc"));
        Assert.True(builder.Add("bb"));
        var frozen = builder.ToImmutable();
        frozen.ValidateInvariants();

        Assert.Same(byLength, frozen.Comparer);
        Assert.Equal("bb", Assert.Single(frozen));
        Assert.Equal("aa", Assert.Single(set));
    }

    /// <summary>Verifies default and custom empty-freeze identity for sorted-set builders.</summary>
    [Fact]
    public void SortedSetBuilder_EmptyFreezeFollowsComparerIdentityRule()
    {
        Assert.Same(FtSortedSet.Empty, FtSortedSet.CreateBuilder().ToImmutable());

        var descending = Comparer<int>.Create((left, right) => right.CompareTo(left));
        var builder = FtSortedSet.CreateBuilder(descending);
        var frozen = builder.ToImmutable();

        Assert.True(frozen.IsEmpty);
        Assert.NotSame(FtSortedSet.Empty, frozen);
        Assert.Same(descending, frozen.Comparer);
    }

    /// <summary>Verifies sorted-set builder enumeration is fail-fast only for structural changes.</summary>
    [Fact]
    public void SortedSetBuilder_EnumerationIsFailFast()
    {
        var builder = FtSortedSet.CreateBuilder();
        builder.UnionWith(new[] { 1, 3 });

        using var enumerator = builder.GetEnumerator();
        Assert.False(builder.Add(1));
        Assert.True(enumerator.MoveNext());
        Assert.Equal(1, enumerator.Current);

        Assert.True(builder.Add(2));
        Assert.Throws<InvalidOperationException>(() => enumerator.MoveNext());
    }

    /// <summary>Verifies sorted-dictionary builder mutations, duplicate handling, caching, and snapshot isolation.</summary>
    [Fact]
    public void SortedDictionaryBuilder_MutationsFreezeAndCacheCorrectly()
    {
        var builder = FtSortedDictionary.CreateBuilder();
        var empty = builder.ToImmutable();

        Assert.Same(FtSortedDictionary.Empty, empty);
        builder.Add(2, "two");
        Assert.True(builder.TryAdd(1, "one"));
        Assert.False(builder.TryAdd(1, "uno"));
        Assert.Throws<ArgumentException>(() => builder.Add(2, "deux"));

        var first = builder.ToImmutable();
        first.ValidateInvariants();
        Assert.Equal(new[] { 1, 2 }, first.Keys.ToArray());
        Assert.Equal("one", first[1]);
        Assert.Same(first, builder.ToImmutable());

        Assert.False(builder.Remove(99));
        Assert.Same(first, builder.ToImmutable());

        builder.SetItem(2, "two");
        var sameContents = builder.ToImmutable();
        sameContents.ValidateInvariants();
        Assert.NotSame(first, sameContents);
        Assert.Equal(first.ToArray(), sameContents.ToArray());

        builder.SetItem(3, "three");
        var second = builder.ToImmutable();
        second.ValidateInvariants();
        Assert.Equal(new[] { 1, 2, 3 }, second.Keys.ToArray());
        Assert.Equal(new[] { 1, 2 }, first.Keys.ToArray());
    }

    /// <summary>Verifies sorted-dictionary builder AddRange semantics and comparer preservation.</summary>
    [Fact]
    public void SortedDictionaryBuilder_AddRangeUsesAddSemanticsAndPreservesComparer()
    {
        var descending = Comparer<int>.Create((left, right) => right.CompareTo(left));
        var builder = FtSortedDictionary.CreateBuilder(descending);

        builder.AddRange(new[]
        {
            new KeyValuePair<int, string>(1, "one"),
            new KeyValuePair<int, string>(3, "three"),
        });
        Assert.Throws<ArgumentException>(() => builder.AddRange(new[] { new KeyValuePair<int, string>(3, "tres") }));

        var frozen = builder.ToImmutable();
        frozen.ValidateInvariants();

        Assert.Same(descending, frozen.Comparer);
        Assert.Equal(new[] { 3, 1 }, frozen.Keys.ToArray());
        Assert.Equal("three", frozen[3]);
    }

    /// <summary>Verifies sorted-dictionary builders preserve the immutable dictionary's nullable-key behavior.</summary>
    [Fact]
    public void SortedDictionaryBuilder_AllowsNullKeysWhenComparerAllowsThem()
    {
        var builder = Durable7.FingerTree.SortedDictionary<string?, int>.CreateBuilder(Comparer<string?>.Default);

        builder.Add(null, 0);
        builder.SetItem("beta", 4);

        Assert.True(builder.ContainsKey(null));
        Assert.True(builder.TryGetValue(null, out var staged));
        Assert.Equal(0, staged);

        var frozen = builder.ToImmutable();
        frozen.ValidateInvariants();

        Assert.Equal(new string?[] { null, "beta" }, frozen.Keys.ToArray());
        Assert.Equal(0, frozen[null]);
    }

    /// <summary>Verifies default and custom empty-freeze identity for sorted-dictionary builders.</summary>
    [Fact]
    public void SortedDictionaryBuilder_EmptyFreezeFollowsComparerIdentityRule()
    {
        Assert.Same(FtSortedDictionary.Empty, FtSortedDictionary.CreateBuilder().ToImmutable());

        var descending = Comparer<int>.Create((left, right) => right.CompareTo(left));
        var builder = FtSortedDictionary.CreateBuilder(descending);
        var frozen = builder.ToImmutable();

        Assert.True(frozen.IsEmpty);
        Assert.NotSame(FtSortedDictionary.Empty, frozen);
        Assert.Same(descending, frozen.Comparer);
    }

    /// <summary>Verifies sorted-dictionary builder enumeration is fail-fast only for structural writes.</summary>
    [Fact]
    public void SortedDictionaryBuilder_EnumerationIsFailFast()
    {
        var builder = FtSortedDictionary.CreateBuilder();
        builder.Add(1, "one");
        builder.Add(3, "three");

        using var enumerator = builder.GetEnumerator();
        Assert.False(builder.TryAdd(1, "uno"));
        Assert.True(enumerator.MoveNext());
        Assert.Equal(1, enumerator.Current.Key);

        builder.SetItem(3, "three");
        Assert.Throws<InvalidOperationException>(() => enumerator.MoveNext());
    }

    /// <summary>Verifies key and value views capture fail-fast state when enumeration starts, not when the view is obtained.</summary>
    [Fact]
    public void SortedDictionaryBuilder_KeyAndValueViewsCaptureVersionAtEnumerationStart()
    {
        var builder = FtSortedDictionary.CreateBuilder();
        builder.Add(1, "one");

        var keys = builder.Keys;
        var values = builder.Values;
        builder.Add(2, "two");

        Assert.Equal(new[] { 1, 2 }, keys.ToArray());
        Assert.Equal(new[] { "one", "two" }, values.ToArray());

        using var keyEnumerator = keys.GetEnumerator();
        Assert.True(keyEnumerator.MoveNext());
        builder.Add(3, "three");
        Assert.Throws<InvalidOperationException>(() => keyEnumerator.MoveNext());
    }
}
