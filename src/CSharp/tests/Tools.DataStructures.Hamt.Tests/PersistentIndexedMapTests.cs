using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Verifies primary-map semantics and automatically maintained secondary memberships.</summary>
public sealed class PersistentIndexedMapTests
{
    /// <summary>Verifies creation retains the selector and all independent policies.</summary>
    [Fact]
    public void Create_RetainsSelectorAndComparers()
    {
        Func<string, string, char> selector = static (_, value) => value[0];
        var keys = StringComparer.OrdinalIgnoreCase;
        var values = StringComparer.InvariantCultureIgnoreCase;
        var indexes = EqualityComparer<char>.Default;
        var map = PersistentIndexedMap<string, string, char>.Create(selector, keys, values, indexes);

        Assert.True(map.IsEmpty);
        Assert.Same(selector, map.IndexSelector);
        Assert.Same(keys, map.KeyComparer);
        Assert.Same(values, map.ValueComparer);
        Assert.Same(indexes, map.IndexComparer);
        Assert.Same(map, map.Clear());
    }

    /// <summary>Verifies added rows populate nonunique secondary groups.</summary>
    [Fact]
    public void Add_PopulatesSecondaryGroups()
    {
        var map = PersistentIndexedMap<int, string, int>
            .Create(static (_, value) => value.Length)
            .Add(1, "a")
            .Add(2, "bb")
            .Add(3, "cc");

        Assert.Equal(3, map.Count);
        Assert.Equal(2, map.IndexKeyCount);
        Assert.Equal([1], map.GetKeysByIndex(1).Order());
        Assert.Equal([2, 3], map.GetKeysByIndex(2).Order());
        Assert.Equal(2, map.CountByIndex(2));
        Assert.Equal("bb", map[2]);
    }

    /// <summary>Verifies strict addition and try-add do not invoke the selector for duplicates.</summary>
    [Fact]
    public void DuplicateAdd_IsStrictAndDoesNotInvokeSelector()
    {
        var calls = 0;
        Func<int, string, int> selector = (_, value) =>
        {
            calls++;
            return value.Length;
        };
        var map = PersistentIndexedMap<int, string, int>.Create(selector).Add(1, "one");

        Assert.Throws<ArgumentException>(() => map.Add(1, "replacement"));
        Assert.False(map.TryAdd(1, "replacement", out var unchanged));
        Assert.Same(map, unchanged);
        Assert.Equal(1, calls);
    }

    /// <summary>Verifies value no-ops skip selection while changed values move memberships.</summary>
    [Fact]
    public void SetItem_UsesValueComparerAndMovesIndexMembership()
    {
        var calls = 0;
        Func<string, string, int> selector = (_, value) =>
        {
            calls++;
            return value.Length;
        };
        var source = PersistentIndexedMap<string, string, int>
            .Create(selector, valueComparer: StringComparer.OrdinalIgnoreCase)
            .Add("key", "one");

        Assert.Same(source, source.SetItem("key", "ONE"));
        Assert.Equal(1, calls);
        var changed = source.SetItem("key", "longer");

        Assert.Equal(2, calls);
        Assert.False(changed.ContainsIndexKey(3));
        Assert.Equal(["key"], changed.GetKeysByIndex(6));
        Assert.Equal("longer", changed["key"]);
        Assert.Equal("one", source["key"]);
        Assert.True(source.ContainsIndexKey(3));
    }

    /// <summary>Verifies selector failure cannot publish a partially updated facade.</summary>
    [Fact]
    public void SelectorFailure_LeavesSourceSnapshotIntact()
    {
        Func<int, string, int> selector = (_, value) =>
            value == "boom" ? throw new InvalidOperationException("selector failed") : value.Length;
        var source = PersistentIndexedMap<int, string, int>.Create(selector).Add(1, "one");

        Assert.Throws<InvalidOperationException>(() => source.SetItem(1, "boom"));
        Assert.Throws<InvalidOperationException>(() => source.Add(2, "boom"));
        Assert.Equal("one", source[1]);
        Assert.Equal([1], source.GetKeysByIndex(3));
        Assert.Single(source);
    }

    /// <summary>Verifies removal uses the retained secondary key without re-running selection.</summary>
    [Fact]
    public void Remove_DoesNotInvokeSelectorAndContractsGroups()
    {
        var calls = 0;
        Func<int, string, int> selector = (_, value) =>
        {
            calls++;
            return value.Length;
        };
        var source = PersistentIndexedMap<int, string, int>.Create(selector)
            .Add(1, "a")
            .Add(2, "b");
        var reduced = source.Remove(1).Remove(2);

        Assert.Equal(2, calls);
        Assert.True(reduced.IsEmpty);
        Assert.Equal(0, reduced.IndexKeyCount);
        Assert.Same(reduced, reduced.Remove(99));
        var cleared = source.Clear();
        Assert.Same(selector, cleared.IndexSelector);
        Assert.Same(source.KeyComparer, cleared.KeyComparer);
    }

    /// <summary>Verifies primary and secondary indexes retain their first representatives.</summary>
    [Fact]
    public void Representatives_AreRetainedAcrossRowsAndUpdates()
    {
        var key = new Key("key", "stored-key");
        var firstCategory = new Category("group", "stored-group");
        var secondCategory = new Category("GROUP", "caller-group");
        var map = PersistentIndexedMap<Key, Row, Category>
            .Create(
                static (_, row) => row.Category,
                KeyComparer.Instance,
                indexComparer: CategoryComparer.Instance)
            .Add(key, new(firstCategory, "one"))
            .Add(new("other", "other-key"), new(secondCategory, "two"));

        Assert.True(map.TryGetKey(new("KEY", "probe"), out var actualKey));
        Assert.Same(key, actualKey);
        Assert.True(map.TryGetIndexKey(new("other", "probe"), out var actualCategory));
        Assert.Same(firstCategory, actualCategory);
        Assert.Equal(2, map.CountByIndex(new("group", "probe")));
        map.ValidateInvariants();
    }

    /// <summary>Verifies range construction and immutable branches remain independent.</summary>
    [Fact]
    public void CreateRangeAndBranches_AreIndependent()
    {
        var root = PersistentIndexedMap<int, string, char>.CreateRange(
            [KeyValuePair.Create(1, "apple"), KeyValuePair.Create(2, "banana")],
            static (_, value) => value[0]);
        var left = root.SetItem(1, "apricot").Add(3, "avocado");
        var right = root.Remove(1).SetItem(2, "cherry");

        Assert.Equal(2, root.Count);
        Assert.Equal(3, left.Count);
        Assert.Single(right);
        Assert.Equal([1, 3], left.GetKeysByIndex('a').Order());
        Assert.Equal([2], right.GetKeysByIndex('c'));
        Assert.True(root.ContainsIndexKey('a'));
        Assert.True(root.ContainsIndexKey('b'));
        root.ValidateInvariants();
        left.ValidateInvariants();
        right.ValidateInvariants();
    }

    private sealed record Key(string Class, string Representation);

    private sealed record Category(string Class, string Representation);

    private sealed record Row(Category Category, string Payload);

    private sealed class KeyComparer : IEqualityComparer<Key>
    {
        internal static KeyComparer Instance { get; } = new();

        public bool Equals(Key? x, Key? y) => StringComparer.OrdinalIgnoreCase.Equals(x?.Class, y?.Class);

        public int GetHashCode(Key obj) => StringComparer.OrdinalIgnoreCase.GetHashCode(obj.Class);
    }

    private sealed class CategoryComparer : IEqualityComparer<Category>
    {
        internal static CategoryComparer Instance { get; } = new();

        public bool Equals(Category? x, Category? y) =>
            StringComparer.OrdinalIgnoreCase.Equals(x?.Class, y?.Class);

        public int GetHashCode(Category obj) => StringComparer.OrdinalIgnoreCase.GetHashCode(obj.Class);
    }
}
