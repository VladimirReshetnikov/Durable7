// Tests for the persistent hash map bulk builder.

using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>White-box coverage for mutable unpublished HAMT bulk construction.</summary>
public sealed class PersistentHashMapBulkBuilderTests
{
    /// <summary>Verifies frozen maps are detached from every later mutation of the builder.</summary>
    [Fact]
    public void FrozenSnapshots_RemainImmutableAcrossBuilderMutations()
    {
        var comparer = new ExplicitHashComparer();
        var first = new ExplicitHashKey(1, 0x10);
        var collision = new ExplicitHashKey(2, 0x10);
        var branch = new ExplicitHashKey(3, 0x11);
        var builder = PersistentHashMap<ExplicitHashKey, string>.CreateBulkBuilder(comparer);

        builder.SetItem(first, "first");
        var leafSnapshot = builder.ToImmutable();
        builder.SetItem(collision, "collision");
        var collisionSnapshot = builder.ToImmutable();
        builder.SetItem(branch, "branch");
        builder.SetItem(first, "updated");
        var branchSnapshot = builder.ToImmutable();

        Assert.Equal("first", leafSnapshot[first]);
        Assert.False(leafSnapshot.ContainsKey(collision));
        Assert.Equal("first", collisionSnapshot[first]);
        Assert.Equal("collision", collisionSnapshot[collision]);
        Assert.False(collisionSnapshot.ContainsKey(branch));
        Assert.Equal("updated", branchSnapshot[first]);
        Assert.Equal("collision", branchSnapshot[collision]);
        Assert.Equal("branch", branchSnapshot[branch]);
    }

    /// <summary>Verifies last-wins values and first-equivalent key/value retention during a bulk build.</summary>
    [Fact]
    public void EquivalentKeys_RetainFirstKeyAndEqualValueInstances()
    {
        var builder = PersistentHashMap<string, string>.CreateBulkBuilder(StringComparer.OrdinalIgnoreCase);
        var storedKey = new string(['A', 'l', 'p', 'h', 'a']);
        var storedValue = new string(['v', 'a', 'l', 'u', 'e']);
        var equalValue = new string(['v', 'a', 'l', 'u', 'e']);

        builder.SetItem(storedKey, storedValue);
        builder.SetItem("ALPHA", equalValue);
        var equalSnapshot = builder.ToImmutable();
        builder.SetItem("alpha", "replacement");
        var replacedSnapshot = builder.ToImmutable();

        Assert.Equal(1, builder.Count);
        Assert.True(equalSnapshot.TryGetKey("alpha", out var actualKey));
        Assert.Same(storedKey, actualKey);
        Assert.Same(storedValue, equalSnapshot["alpha"]);
        Assert.Same(storedKey, replacedSnapshot.Single().Key);
        Assert.Equal("replacement", replacedSnapshot["ALPHA"]);
    }

    /// <summary>Verifies null keys and final-level hash branching use the ordinary map contracts.</summary>
    [Fact]
    public void NullAndDeepPrefixKeys_RoundTripThroughFrozenMap()
    {
        var nullable = PersistentHashMap<string?, int>.CreateBulkBuilder();
        nullable.SetItem(null, 1);
        nullable.SetItem("value", 2);
        var nullableMap = nullable.ToImmutable();
        Assert.Equal(1, nullableMap[null]);
        Assert.Equal(2, nullableMap["value"]);

        var comparer = new ExplicitHashComparer();
        var low = new ExplicitHashKey(1, 0);
        var high = new ExplicitHashKey(2, 1 << 30);
        var deep = PersistentHashMap<ExplicitHashKey, int>.CreateBulkBuilder(comparer);
        deep.SetItem(low, 10);
        deep.SetItem(high, 20);
        var deepMap = deep.ToImmutable();
        Assert.Equal(10, deepMap[low]);
        Assert.Equal(20, deepMap[high]);
    }

    /// <summary>Checks a large collision-heavy bulk build against ordinary persistent updates.</summary>
    [Fact]
    public void RandomizedBuild_MatchesPersistentUpdateShapeAndContents()
    {
        var comparer = new ExplicitHashComparer();
        var builder = PersistentHashMap<ExplicitHashKey, int>.CreateBulkBuilder(comparer);
        var persistent = PersistentHashMap<ExplicitHashKey, int>.Create(comparer);
        var random = new Random(20260710);

        for (var i = 0; i < 10_000; i++)
        {
            var id = random.Next(2_000);
            var hash = id % 4 == 0 ? id & 31 : unchecked(id * 0x01010101);
            var key = new ExplicitHashKey(id, hash);
            var value = random.Next();
            builder.SetItem(key, value);
            persistent = persistent.SetItem(key, value);
        }

        var built = builder.ToImmutable();
        Assert.Equal(persistent.Count, built.Count);
        Assert.Equal(persistent.ToArray(), built.ToArray());
    }

    /// <summary>Verifies the combining operation selects one path and retains representatives.</summary>
    [Fact]
    public void AddOrUpdate_SelectsExactlyOnePathAndRetainsRepresentatives()
    {
        var comparer = new CountingStringComparer();
        var storedKey = new string(['A', 'l', 'p', 'h', 'a']);
        var storedValue = new EquatableReference("value");
        var equalCandidate = new EquatableReference("value");
        var builder = PersistentHashMap<string, EquatableReference>.CreateBulkBuilder(comparer);
        var calls = 0;

        var added = builder.AddOrUpdate(
            storedKey,
            storedValue,
            (_, _) =>
            {
                calls++;
                return equalCandidate;
            });
        var hashesAfterMiss = comparer.HashCalls;
        var updated = builder.AddOrUpdate(
            "ALPHA",
            new EquatableReference("unused"),
            (value, _) =>
            {
                calls++;
                Assert.Same(storedValue, value);
                return equalCandidate;
            });

        var map = builder.ToImmutable();
        Assert.Same(storedValue, added);
        Assert.Same(storedValue, updated);
        Assert.Equal(1, calls);
        Assert.Equal(hashesAfterMiss + 1, comparer.HashCalls);
        Assert.True(map.TryGetKey("alpha", out var actualKey));
        Assert.Same(storedKey, actualKey);
        Assert.Same(storedValue, map["alpha"]);
    }

    /// <summary>Verifies combining scans only the matching full-hash bucket across branch shapes.</summary>
    [Fact]
    public void AddOrUpdate_AggregatesCollisionAndDeepPrefixEntries()
    {
        var comparer = new ExplicitHashComparer();
        var collision1 = new ExplicitHashKey(1, 0);
        var collision2 = new ExplicitHashKey(2, 0);
        var deep = new ExplicitHashKey(3, 1 << 30);
        var builder = PersistentHashMap<ExplicitHashKey, int>.CreateBulkBuilder(comparer);

        Assert.Equal(1, builder.AddOrUpdate(collision1, 1, static (value, _) => checked(value + 1)));
        Assert.Equal(1, builder.AddOrUpdate(collision2, 1, static (value, _) => checked(value + 1)));
        Assert.Equal(1, builder.AddOrUpdate(deep, 1, static (value, _) => checked(value + 1)));
        Assert.Equal(2, builder.AddOrUpdate(new ExplicitHashKey(1, 0), 99, static (value, _) => checked(value + 1)));
        Assert.Equal(2, builder.AddOrUpdate(new ExplicitHashKey(3, 1 << 30), 99, static (value, _) => checked(value + 1)));

        var map = builder.ToImmutable();
        Assert.Equal(3, map.Count);
        Assert.Equal(2, map[collision1]);
        Assert.Equal(1, map[collision2]);
        Assert.Equal(2, map[deep]);
        map.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies delegate validation and callback failures cannot partially update a builder.</summary>
    [Fact]
    public void AddOrUpdate_CallbackFailuresLeaveBuilderUnchanged()
    {
        var comparer = new CountingStringComparer();
        var builder = PersistentHashMap<string, ThrowingEquatableReference>.CreateBulkBuilder(comparer);
        var stored = new ThrowingEquatableReference("stored");
        builder.AddOrUpdate("alpha", stored, static (value, _) => value);
        var before = builder.ToImmutable();
        var hashesBeforeValidation = comparer.HashCalls;

        Assert.Throws<ArgumentNullException>(() => builder.AddOrUpdate("alpha", stored, null!));
        Assert.Equal(hashesBeforeValidation, comparer.HashCalls);

        Assert.Throws<InvalidOperationException>(() =>
            builder.AddOrUpdate("ALPHA", stored, static (_, _) => throw new InvalidOperationException("factory")));
        Assert.Same(stored, builder.ToImmutable()["alpha"]);

        ThrowingEquatableReference.ThrowOnEquals = true;
        try
        {
            Assert.Throws<InvalidOperationException>(() =>
                builder.AddOrUpdate("ALPHA", stored, static (_, _) => new ThrowingEquatableReference("candidate")));
        }
        finally
        {
            ThrowingEquatableReference.ThrowOnEquals = false;
        }

        var after = builder.ToImmutable();
        Assert.Single(after);
        Assert.Same(stored, after["alpha"]);
        Assert.Same(stored, before["alpha"]);
    }

    /// <summary>Verifies frozen maps stay detached after later combining updates.</summary>
    [Fact]
    public void AddOrUpdate_FrozenSnapshotsRemainDetached()
    {
        var builder = PersistentHashMap<int, int>.CreateBulkBuilder();
        builder.AddOrUpdate(1, 1, static (value, _) => checked(value + 1));
        var first = builder.ToImmutable();
        builder.AddOrUpdate(1, 99, static (value, _) => checked(value + 1));
        builder.AddOrUpdate(2, 5, static (value, _) => checked(value + 1));
        var second = builder.ToImmutable();

        Assert.Equal(1, first[1]);
        Assert.False(first.ContainsKey(2));
        Assert.Equal(2, second[1]);
        Assert.Equal(5, second[2]);
    }

    /// <summary>Verifies checked update overflow leaves the staged entry unchanged and reusable.</summary>
    [Fact]
    public void AddOrUpdate_CheckedOverflowLeavesBuilderUnchanged()
    {
        var builder = PersistentHashMap<string, int>.CreateBulkBuilder(StringComparer.OrdinalIgnoreCase);
        var storedKey = new string(['A', 'l', 'p', 'h', 'a']);
        builder.AddOrUpdate(storedKey, int.MaxValue, static (value, increment) => checked(value + increment));

        Assert.Throws<OverflowException>(() =>
            builder.AddOrUpdate("ALPHA", 1, static (value, increment) => checked(value + increment)));

        var map = builder.ToImmutable();
        Assert.Single(map);
        Assert.Equal(int.MaxValue, map["alpha"]);
        Assert.True(map.TryGetKey("alpha", out var actualKey));
        Assert.Same(storedKey, actualKey);
        map.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies hashing and key-equality failures leave staged state unchanged.</summary>
    [Fact]
    public void AddOrUpdate_ComparerFailuresLeaveBuilderUnchanged()
    {
        var comparer = new InstrumentedKeyComparer();
        var stored = new InstrumentedKey(1, 7);
        var builder = PersistentHashMap<InstrumentedKey, int>.CreateBulkBuilder(comparer);
        builder.AddOrUpdate(stored, 10, static (value, incoming) => value + incoming);
        var factoryCalls = 0;

        var hashFailure = new InvalidOperationException("hash");
        comparer.HashFailure = hashFailure;
        Assert.Same(
            hashFailure,
            Assert.Throws<InvalidOperationException>(() =>
                builder.AddOrUpdate(
                    new InstrumentedKey(2, 7),
                    20,
                    (value, incoming) =>
                    {
                        factoryCalls++;
                        return value + incoming;
                    })));
        comparer.HashFailure = null;

        var equalityFailure = new InvalidOperationException("equality");
        comparer.EqualityFailure = equalityFailure;
        Assert.Same(
            equalityFailure,
            Assert.Throws<InvalidOperationException>(() =>
                builder.AddOrUpdate(
                    new InstrumentedKey(2, 7),
                    20,
                    (value, incoming) =>
                    {
                        factoryCalls++;
                        return value + incoming;
                    })));
        comparer.EqualityFailure = null;

        Assert.Equal(0, factoryCalls);
        var map = builder.ToImmutable();
        Assert.Single(map);
        Assert.Equal(10, map[stored]);
        map.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies one hash and one full-hash bucket scan select the combining delegate.</summary>
    [Fact]
    public void AddOrUpdate_HashesOnceAndScansOneFullHashBucket()
    {
        var comparer = new InstrumentedKeyComparer();
        var builder = PersistentHashMap<InstrumentedKey, int>.CreateBulkBuilder(comparer);
        builder.AddOrUpdate(new InstrumentedKey(1, 7), 1, static (value, incoming) => value + incoming);
        builder.AddOrUpdate(new InstrumentedKey(2, 7), 2, static (value, incoming) => value + incoming);
        builder.AddOrUpdate(new InstrumentedKey(3, 7), 3, static (value, incoming) => value + incoming);
        builder.AddOrUpdate(new InstrumentedKey(4, 11), 4, static (value, incoming) => value + incoming);

        comparer.ResetCounts();
        var factoryCalls = 0;
        Assert.Equal(
            8,
            builder.AddOrUpdate(
                new InstrumentedKey(3, 7),
                5,
                (value, incoming) =>
                {
                    factoryCalls++;
                    return value + incoming;
                }));
        Assert.Equal(1, comparer.HashCalls);
        Assert.Equal(3, comparer.EqualityCalls);
        Assert.Equal(1, factoryCalls);

        comparer.ResetCounts();
        factoryCalls = 0;
        Assert.Equal(
            6,
            builder.AddOrUpdate(
                new InstrumentedKey(6, 7),
                6,
                (value, incoming) =>
                {
                    factoryCalls++;
                    return value + incoming;
                }));
        Assert.Equal(1, comparer.HashCalls);
        Assert.Equal(3, comparer.EqualityCalls);
        Assert.Equal(0, factoryCalls);

        comparer.ResetCounts();
        Assert.Equal(
            9,
            builder.AddOrUpdate(
                new InstrumentedKey(9, 99),
                9,
                (_, _) =>
                {
                    factoryCalls++;
                    return -1;
                }));
        Assert.Equal(1, comparer.HashCalls);
        Assert.Equal(0, comparer.EqualityCalls);
        Assert.Equal(0, factoryCalls);
    }

    /// <summary>Verifies a null key follows the ordinary comparer-defined map contract.</summary>
    [Fact]
    public void AddOrUpdate_NullKeyUsesOrdinaryMapContract()
    {
        var builder = PersistentHashMap<string?, int>.CreateBulkBuilder();

        Assert.Equal(1, builder.AddOrUpdate(null, 1, static (value, incoming) => checked(value + incoming)));
        Assert.Equal(2, builder.AddOrUpdate(null, 1, static (value, incoming) => checked(value + incoming)));

        var map = builder.ToImmutable();
        Assert.Single(map);
        Assert.Equal(2, map[null]);
        map.ValidateCanonicalityForDiagnostics();
    }

    private readonly record struct ExplicitHashKey(int Id, int Hash);

    private sealed class ExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(ExplicitHashKey x, ExplicitHashKey y) => x.Id == y.Id;

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(ExplicitHashKey obj) => obj.Hash;
    }

    private sealed class CountingStringComparer : IEqualityComparer<string>
    {
        /// <summary>
        /// Gets how many times the policy was asked to hash, so a test can assert an operation consulted it only as
        /// often as its bound allows.
        /// </summary>
        public int HashCalls { get; private set; }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(string? x, string? y) => StringComparer.OrdinalIgnoreCase.Equals(x, y);

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(string obj)
        {
            HashCalls++;
            return StringComparer.OrdinalIgnoreCase.GetHashCode(obj);
        }
    }

    private sealed class EquatableReference(string value)
    {
        /// <summary>Gets the stored value.</summary>
        public string Value { get; } = value;

        /// <summary>Determines whether both values hold the same elements.</summary>
        public override bool Equals(object? obj) => obj is EquatableReference other && Value == other.Value;

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public override int GetHashCode() => Value.GetHashCode(StringComparison.Ordinal);
    }

    private sealed class ThrowingEquatableReference(string value)
    {
        /// <summary>
        /// Throws from the equals callback on demand, so a test can check that a failing callback leaves the collection
        /// unchanged.
        /// </summary>
        public static bool ThrowOnEquals { get; set; }

        /// <summary>Gets the stored value.</summary>
        public string Value { get; } = value;

        /// <summary>Determines whether both values hold the same elements.</summary>
        public override bool Equals(object? obj)
        {
            if (ThrowOnEquals)
                throw new InvalidOperationException("value equality");
            return obj is ThrowingEquatableReference other && Value == other.Value;
        }

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public override int GetHashCode() => Value.GetHashCode(StringComparison.Ordinal);
    }

    private readonly record struct InstrumentedKey(int Id, int Hash);

    private sealed class InstrumentedKeyComparer : IEqualityComparer<InstrumentedKey>
    {
        /// <summary>
        /// Gets how many times the policy was asked to hash, so a test can assert an operation consulted it only as
        /// often as its bound allows.
        /// </summary>
        public int HashCalls { get; private set; }

        /// <summary>Gets how many times the policy was asked to compare.</summary>
        public int EqualityCalls { get; private set; }

        /// <summary>Gets the hash failure.</summary>
        public Exception? HashFailure { get; set; }

        /// <summary>Gets the equality failure.</summary>
        public Exception? EqualityFailure { get; set; }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(InstrumentedKey x, InstrumentedKey y)
        {
            EqualityCalls++;
            if (EqualityFailure is not null)
                throw EqualityFailure;
            return x.Id == y.Id;
        }

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(InstrumentedKey obj)
        {
            HashCalls++;
            if (HashFailure is not null)
                throw HashFailure;
            return obj.Hash;
        }

        /// <summary>Clears the recorded counts.</summary>
        public void ResetCounts()
        {
            HashCalls = 0;
            EqualityCalls = 0;
        }
    }
}
