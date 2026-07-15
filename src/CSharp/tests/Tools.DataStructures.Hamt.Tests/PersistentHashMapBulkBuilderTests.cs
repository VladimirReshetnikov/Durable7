using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

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
            _ =>
            {
                calls++;
                return equalCandidate;
            });
        var hashesAfterMiss = comparer.HashCalls;
        var updated = builder.AddOrUpdate(
            "ALPHA",
            new EquatableReference("unused"),
            value =>
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

        Assert.Equal(1, builder.AddOrUpdate(collision1, 1, static value => checked(value + 1)));
        Assert.Equal(1, builder.AddOrUpdate(collision2, 1, static value => checked(value + 1)));
        Assert.Equal(1, builder.AddOrUpdate(deep, 1, static value => checked(value + 1)));
        Assert.Equal(2, builder.AddOrUpdate(new ExplicitHashKey(1, 0), 99, static value => checked(value + 1)));
        Assert.Equal(2, builder.AddOrUpdate(new ExplicitHashKey(3, 1 << 30), 99, static value => checked(value + 1)));

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
        builder.AddOrUpdate("alpha", stored, static value => value);
        var before = builder.ToImmutable();
        var hashesBeforeValidation = comparer.HashCalls;

        Assert.Throws<ArgumentNullException>(() => builder.AddOrUpdate("alpha", stored, null!));
        Assert.Equal(hashesBeforeValidation, comparer.HashCalls);

        Assert.Throws<InvalidOperationException>(() =>
            builder.AddOrUpdate("ALPHA", stored, static _ => throw new InvalidOperationException("factory")));
        Assert.Same(stored, builder.ToImmutable()["alpha"]);

        ThrowingEquatableReference.ThrowOnEquals = true;
        try
        {
            Assert.Throws<InvalidOperationException>(() =>
                builder.AddOrUpdate("ALPHA", stored, static _ => new ThrowingEquatableReference("candidate")));
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
        builder.AddOrUpdate(1, 1, static value => checked(value + 1));
        var first = builder.ToImmutable();
        builder.AddOrUpdate(1, 99, static value => checked(value + 1));
        builder.AddOrUpdate(2, 5, static value => checked(value + 1));
        var second = builder.ToImmutable();

        Assert.Equal(1, first[1]);
        Assert.False(first.ContainsKey(2));
        Assert.Equal(2, second[1]);
        Assert.Equal(5, second[2]);
    }

    private readonly record struct ExplicitHashKey(int Id, int Hash);

    private sealed class ExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        public bool Equals(ExplicitHashKey x, ExplicitHashKey y) => x.Id == y.Id;

        public int GetHashCode(ExplicitHashKey obj) => obj.Hash;
    }

    private sealed class CountingStringComparer : IEqualityComparer<string>
    {
        public int HashCalls { get; private set; }

        public bool Equals(string? x, string? y) => StringComparer.OrdinalIgnoreCase.Equals(x, y);

        public int GetHashCode(string obj)
        {
            HashCalls++;
            return StringComparer.OrdinalIgnoreCase.GetHashCode(obj);
        }
    }

    private sealed class EquatableReference(string value)
    {
        public string Value { get; } = value;

        public override bool Equals(object? obj) => obj is EquatableReference other && Value == other.Value;

        public override int GetHashCode() => Value.GetHashCode(StringComparison.Ordinal);
    }

    private sealed class ThrowingEquatableReference(string value)
    {
        public static bool ThrowOnEquals { get; set; }

        public string Value { get; } = value;

        public override bool Equals(object? obj)
        {
            if (ThrowOnEquals)
                throw new InvalidOperationException("value equality");
            return obj is ThrowingEquatableReference other && Value == other.Value;
        }

        public override int GetHashCode() => Value.GetHashCode(StringComparison.Ordinal);
    }
}
