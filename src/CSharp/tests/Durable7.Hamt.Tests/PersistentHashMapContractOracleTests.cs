// Tests for the persistent hash map contract oracle.

using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>
/// Pins the persistent-map semantics that lifecycle and frozen representations must preserve.
/// </summary>
public sealed class PersistentHashMapContractOracleTests
{
    /// <summary>Verifies comparer object identity survives every persistent transition.</summary>
    [Fact]
    public void ComparerIdentity_IsPreservedByEveryVersion()
    {
        var comparer = new ConstantHashOrdinalIgnoreCaseComparer();
        var empty = PersistentHashMap<string, int>.Create(comparer);
        var added = empty.SetItem("alpha", 1);
        var replaced = added.SetItem("ALPHA", 2);
        var removed = replaced.Remove("Alpha");
        var cleared = replaced.Clear();

        Assert.Same(comparer, empty.Comparer);
        Assert.Same(comparer, added.Comparer);
        Assert.Same(comparer, replaced.Comparer);
        Assert.Same(comparer, removed.Comparer);
        Assert.Same(comparer, cleared.Comparer);
    }

    /// <summary>Verifies equivalent updates retain the first key and equal value objects.</summary>
    [Fact]
    public void EquivalentReplacement_RetainsFirstStoredRepresentatives()
    {
        var storedKey = NewString("Alpha");
        var equivalentKey = NewString("ALPHA");
        var storedValue = NewString("value");
        var equalValue = NewString("value");
        var map = PersistentHashMap<string, string>.Create(StringComparer.OrdinalIgnoreCase)
            .SetItem(storedKey, storedValue);

        var noOp = map.SetItem(equivalentKey, equalValue);

        Assert.Same(map, noOp);
        Assert.True(noOp.TryGetKey(equivalentKey, out var actualKey));
        Assert.Same(storedKey, actualKey);
        Assert.Same(storedValue, noOp[equivalentKey]);

        var changed = noOp.SetItem(equivalentKey, NewString("changed"));
        Assert.True(changed.TryGetKey(equivalentKey, out actualKey));
        Assert.Same(storedKey, actualKey);
        Assert.Equal("changed", changed[equivalentKey]);
    }

    /// <summary>Verifies CRUD and representative semantics inside an equal-hash collision bucket.</summary>
    [Fact]
    public void EqualHashCollisionBucket_PreservesAllDistinctKeys()
    {
        var comparer = new ConstantHashOrdinalIgnoreCaseComparer();
        var firstAlpha = NewString("Alpha");
        var secondAlpha = NewString("ALPHA");
        var beta = NewString("Beta");
        var gamma = NewString("Gamma");
        var original = PersistentHashMap<string, int>.Create(comparer)
            .SetItem(firstAlpha, 1)
            .SetItem(beta, 2)
            .SetItem(gamma, 3);

        var replaced = original.SetItem(secondAlpha, 10);
        var removed = replaced.Remove(beta);

        Assert.Equal(3, original.Count);
        Assert.Equal(1, original[secondAlpha]);
        Assert.Equal(10, replaced[firstAlpha]);
        Assert.True(replaced.TryGetKey(secondAlpha, out var actualAlpha));
        Assert.Same(firstAlpha, actualAlpha);
        Assert.Equal(2, replaced[beta]);
        Assert.Equal(3, replaced[gamma]);
        Assert.False(removed.ContainsKey(beta));
        Assert.Equal(10, removed[firstAlpha]);
        Assert.Equal(3, removed[gamma]);
    }

    /// <summary>Verifies null keys and values participate in ordinary persistent operations.</summary>
    [Fact]
    public void NullKeyAndValue_AreStoredReplacedAndRemoved()
    {
        var empty = PersistentHashMap<string?, string?>.Empty;
        var withNull = empty.SetItem(null, null);
        var withBoth = withNull.SetItem("present", null);
        var replaced = withBoth.SetItem(null, "replacement");
        var removed = replaced.Remove(null);

        Assert.False(empty.ContainsKey(null));
        Assert.True(withNull.TryGetValue(null, out var nullValue));
        Assert.Null(nullValue);
        Assert.True(withBoth.ContainsKey(null));
        Assert.True(withBoth.ContainsKey("present"));
        Assert.Equal("replacement", replaced[null]);
        Assert.False(removed.ContainsKey(null));
        Assert.True(removed.TryGetValue("present", out var presentValue));
        Assert.Null(presentValue);
    }

    /// <summary>Verifies trie and collision-bucket traversal remains stable for retained versions.</summary>
    [Fact]
    public void Enumeration_IsStableForAnUnchangedVersion()
    {
        var comparer = new ScriptedHashComparer();
        var keys = new[]
        {
            new ScriptedHashKey("zero", 0),
            new ScriptedHashKey("deep-a", 1),
            new ScriptedHashKey("deep-b", 33),
            new ScriptedHashKey("collision-a", 7),
            new ScriptedHashKey("collision-b", 7),
            new ScriptedHashKey("high", unchecked((int)0x80000000)),
        };
        var map = PersistentHashMap<ScriptedHashKey, int>.Create(comparer);
        for (var index = 0; index < keys.Length; index++)
            map = map.SetItem(keys[index], index);

        var expected = map.ToArray();
        var changed = map.SetItem(new ScriptedHashKey("additional", 65), 100);

        Assert.Equal(expected, map.ToArray());
        Assert.Equal(expected, map.ToArray());
        Assert.Equal(changed.ToArray(), changed.ToArray());
        Assert.Equal(expected, map.ToArray());
    }

    /// <summary>Verifies logically ineffective operations preserve map reference identity.</summary>
    [Fact]
    public void LogicalNoOps_ReturnTheSameMapInstance()
    {
        var storedValue = NewString("value");
        var map = PersistentHashMap<string, string>.Create(StringComparer.OrdinalIgnoreCase)
            .SetItem("Alpha", storedValue);

        Assert.Same(map, map.SetItem("ALPHA", NewString("value")));
        Assert.Same(map, map.SetItems([KeyValuePair.Create("alpha", NewString("value"))]));
        Assert.Same(map, map.Remove("missing"));
        Assert.False(map.TryAdd("ALPHA", "other", out var duplicate));
        Assert.Same(map, duplicate);
        Assert.False(map.TryRemove("missing", out var notRemoved, out _));
        Assert.Same(map, notRemoved);

        var empty = map.Clear();
        Assert.Same(empty, empty.Clear());
        Assert.Same(empty, empty.Remove("missing"));
    }

    /// <summary>Verifies later edits cannot change any retained older map version.</summary>
    [Fact]
    public void RetainedVersions_RemainIndependentSnapshots()
    {
        var empty = PersistentHashMap<int, string>.Empty;
        var one = empty.SetItem(1, "one");
        var two = one.SetItem(2, "two");
        var replaced = two.SetItem(1, "uno");
        var removed = replaced.Remove(2);

        Assert.Empty(empty);
        Assert.Equal([KeyValuePair.Create(1, "one")], one.ToArray());
        Assert.Equal(
            [KeyValuePair.Create(1, "one"), KeyValuePair.Create(2, "two")],
            two.OrderBy(pair => pair.Key).ToArray());
        Assert.Equal("uno", replaced[1]);
        Assert.Equal("two", replaced[2]);
        Assert.Equal([KeyValuePair.Create(1, "uno")], removed.ToArray());
        Assert.Equal("one", one[1]);
        Assert.Equal("two", two[2]);
    }

    /// <summary>Verifies comparer callback failures propagate without affecting the source version.</summary>
    [Fact]
    public void ComparerCallbackExceptions_LeaveTheSourceVersionUnchanged()
    {
        var comparer = new SwitchableThrowingComparer();
        var map = PersistentHashMap<string, int>.Create(comparer)
            .SetItem("alpha", 1)
            .SetItem("beta", 2);
        var expected = map.ToArray();
        var expectedRoot = map.RootForTesting;

        comparer.ThrowFromEquals = true;
        var equalsFailure = Assert.Throws<ComparerCallbackException>(() => map.SetItem("gamma", 3));
        Assert.Same(comparer.Failure, equalsFailure);

        comparer.ThrowFromEquals = false;
        Assert.Same(expectedRoot, map.RootForTesting);
        Assert.Equal(expected, map.ToArray());

        comparer.ThrowFromGetHashCode = true;
        var hashFailure = Assert.Throws<ComparerCallbackException>(() => map.Remove("alpha"));
        Assert.Same(comparer.Failure, hashFailure);

        comparer.ThrowFromGetHashCode = false;
        Assert.Same(expectedRoot, map.RootForTesting);
        Assert.Equal(expected, map.ToArray());
        Assert.Equal(1, map["alpha"]);
        Assert.Equal(2, map["beta"]);
    }

    /// <summary>Verifies value-equality callback failures cannot publish a partial replacement.</summary>
    [Fact]
    public void ValueEqualityCallbackException_LeavesTheSourceVersionUnchanged()
    {
        var storedValue = new SwitchableThrowingValue("value");
        var incomingValue = new SwitchableThrowingValue("value");
        var map = PersistentHashMap<int, SwitchableThrowingValue>.Empty.SetItem(1, storedValue);
        var expectedRoot = map.RootForTesting;

        storedValue.ThrowFromEquals = true;
        var failure = Assert.Throws<ValueCallbackException>(() => map.SetItem(1, incomingValue));
        Assert.Same(storedValue.Failure, failure);

        storedValue.ThrowFromEquals = false;
        Assert.Same(expectedRoot, map.RootForTesting);
        Assert.Single(map);
        Assert.Same(storedValue, map[1]);
    }

    private static string NewString(string value) => new(value.ToCharArray());

    private sealed class ConstantHashOrdinalIgnoreCaseComparer : IEqualityComparer<string>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(string? x, string? y) => StringComparer.OrdinalIgnoreCase.Equals(x, y);

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(string obj) => 0;
    }

    private sealed record ScriptedHashKey(string Name, int Hash);

    private sealed class ScriptedHashComparer : IEqualityComparer<ScriptedHashKey>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(ScriptedHashKey? x, ScriptedHashKey? y) => x?.Name == y?.Name;

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(ScriptedHashKey obj) => obj.Hash;
    }

    private sealed class SwitchableThrowingComparer : IEqualityComparer<string>
    {
        /// <summary>Gets the failure this result carries.</summary>
        internal ComparerCallbackException Failure { get; } = new();

        /// <summary>
        /// Throws from the equality callback on demand, so a test can check that a failing comparison leaves the
        /// collection unchanged.
        /// </summary>
        internal bool ThrowFromEquals { get; set; }

        /// <summary>
        /// Throws from the hashing callback on demand, so a test can check that a failing hash leaves the collection
        /// unchanged.
        /// </summary>
        internal bool ThrowFromGetHashCode { get; set; }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(string? x, string? y)
        {
            if (ThrowFromEquals)
                throw Failure;

            return StringComparer.Ordinal.Equals(x, y);
        }

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(string obj)
        {
            if (ThrowFromGetHashCode)
                throw Failure;

            return 0;
        }
    }

    private sealed class SwitchableThrowingValue(string value) : IEquatable<SwitchableThrowingValue>
    {
        /// <summary>Gets the failure this result carries.</summary>
        internal ValueCallbackException Failure { get; } = new();

        /// <summary>
        /// Throws from the equality callback on demand, so a test can check that a failing comparison leaves the
        /// collection unchanged.
        /// </summary>
        internal bool ThrowFromEquals { get; set; }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(SwitchableThrowingValue? other)
        {
            if (ThrowFromEquals)
                throw Failure;

            return other is not null && value == other.Value;
        }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public override bool Equals(object? obj) => obj is SwitchableThrowingValue other && Equals(other);

        /// <summary>Returns a hash consistent with <see cref="Equals(SwitchableThrowingValue)"/>.</summary>
        public override int GetHashCode() => value.GetHashCode(StringComparison.Ordinal);

        private string Value => value;
    }

    private sealed class ComparerCallbackException : Exception
    {
    }

    private sealed class ValueCallbackException : Exception
    {
    }
}
