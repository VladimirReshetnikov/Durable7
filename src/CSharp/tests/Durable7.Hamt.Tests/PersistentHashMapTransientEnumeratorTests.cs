// Tests for the persistent hash map transient enumerator.

using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Enumerator and version-bound view tests for CHAMP map transients.</summary>
public sealed class PersistentHashMapTransientEnumeratorTests
{
    /// <summary>Verifies concrete, interface, key, and value enumeration share persistent trie order.</summary>
    [Fact]
    public void Enumeration_UsesPersistentTrieOrder()
    {
        var source = PersistentHashMap<int, string>.Empty;
        for (var key = 0; key < 80; key++)
            source = source.SetItem(key * 33, $"v{key}");

        var transient = source.ToTransient();
        transient.Remove(33 * 4);
        transient.SetItem(33 * 90, "v90");
        var expected = source.Remove(33 * 4).SetItem(33 * 90, "v90");

        Assert.Equal(expected.ToArray(), transient.ToArray());
        Assert.Equal(expected.Keys, transient.Keys);
        Assert.Equal(expected.Values, transient.Values);
        IReadOnlyDictionary<int, string> dictionary = transient;
        Assert.Equal(expected.ToArray(), dictionary.ToArray());
    }

    /// <summary>Verifies copied struct enumerators advance independently.</summary>
    [Fact]
    public void CopiedEnumerator_AdvancesIndependently()
    {
        var transient = PersistentHashMap<int, string>.Empty
            .SetItem(0, "zero")
            .SetItem(1, "one")
            .SetItem(33, "thirty-three")
            .ToTransient();
        var expected = transient.ToArray();
        var original = transient.GetEnumerator();
        Assert.True(original.MoveNext());
        var copy = original;

        var originalItems = new List<KeyValuePair<int, string>> { original.Current };
        while (original.MoveNext())
            originalItems.Add(original.Current);
        var copiedItems = new List<KeyValuePair<int, string>> { copy.Current };
        while (copy.MoveNext())
            copiedItems.Add(copy.Current);

        Assert.Equal(expected, originalItems);
        Assert.Equal(expected, copiedItems);
    }

    /// <summary>Verifies successful changes invalidate every captured enumeration alias.</summary>
    [Fact]
    public void ChangedMutation_InvalidatesEnumeratorsAndViews()
    {
        var transient = PersistentHashMap<int, string>.Empty.SetItem(1, "one").ToTransient();
        var pairs = transient.GetEnumerator();
        var keys = transient.Keys;
        var values = transient.Values;
        var keyEnumerator = keys.GetEnumerator();
        var valueEnumerator = values.GetEnumerator();

        transient.SetItem(2, "two");

        Assert.Throws<InvalidOperationException>(() => pairs.MoveNext());
        Assert.Throws<InvalidOperationException>(() => _ = pairs.Current);
        Assert.Throws<InvalidOperationException>(() => keys.GetEnumerator());
        Assert.Throws<InvalidOperationException>(() => values.GetEnumerator());
        Assert.Throws<InvalidOperationException>(() => keyEnumerator.MoveNext());
        Assert.Throws<InvalidOperationException>(() => _ = valueEnumerator.Current);
        Assert.Throws<InvalidOperationException>(() => ((System.Collections.IEnumerator)pairs).Reset());
    }

    /// <summary>Verifies every logical no-op leaves existing enumeration aliases valid.</summary>
    [Fact]
    public void LogicalNoOps_DoNotInvalidateEnumeration()
    {
        var stored = new string(['o', 'n', 'e']);
        var transient = PersistentHashMap<int, string>.Empty.SetItem(1, stored).ToTransient();
        var pairs = transient.GetEnumerator();
        var keys = transient.Keys;
        var values = transient.Values;

        transient.SetItem(1, new string(['o', 'n', 'e']));
        Assert.False(transient.TryAdd(1, "different"));
        Assert.Throws<ArgumentException>(() => transient.Add(1, "different"));
        Assert.False(transient.Remove(99));

        Assert.True(pairs.MoveNext());
        Assert.Same(stored, pairs.Current.Value);
        Assert.Equal(new[] { 1 }, keys);
        Assert.Single(values);

        var empty = PersistentHashMap<int, string>.CreateTransient();
        var emptyEnumerator = empty.GetEnumerator();
        empty.Clear();
        Assert.False(emptyEnumerator.MoveNext());
    }

    /// <summary>Verifies a default enumerator behaves as an exhausted value.</summary>
    [Fact]
    public void DefaultEnumerator_IsExhausted()
    {
        var enumerator = default(PersistentHashMap<int, string>.Transient.Enumerator);

        Assert.Equal(default, enumerator.Current);
        Assert.False(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        Assert.Throws<NotSupportedException>(() => ((System.Collections.IEnumerator)enumerator).Reset());
    }

    /// <summary>Verifies collision and full-depth branch nodes are traversed completely.</summary>
    [Fact]
    public void CollisionAndDeepBranches_AreEnumeratedCompletely()
    {
        var collision = PersistentHashMap<int, int>.CreateTransient(new ConstantHashComparer());
        for (var key = 0; key < 32; key++)
            collision.SetItem(key, key * 2);
        Assert.Equal(Enumerable.Range(0, 32), collision.Select(pair => pair.Key).OrderBy(key => key));

        var deep = PersistentHashMap<int, int>.CreateTransient(new IdentityHashComparer());
        var keys = new[] { 0, 1 << 30, unchecked((int)0x80000000), unchecked((int)0xC0000000) };
        foreach (var key in keys)
            deep.SetItem(key, key);
        Assert.Equal(keys.OrderBy(key => key), deep.Select(pair => pair.Key).OrderBy(key => key));
    }

    private sealed class ConstantHashComparer : IEqualityComparer<int>
    {
        public bool Equals(int x, int y) => x == y;

        public int GetHashCode(int obj) => 0;
    }

    private sealed class IdentityHashComparer : IEqualityComparer<int>
    {
        public bool Equals(int x, int y) => x == y;

        public int GetHashCode(int obj) => obj;
    }
}
