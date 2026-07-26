// Tests for the persistent hash map single pass update.

using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>
/// Contract, callback-count, transition, failure, and invariant tests for the persistent map's
/// single-pass factory updates.
/// </summary>
public sealed class PersistentHashMapSinglePassUpdateTests
{
    /// <summary>Verifies delegate validation precedes hashing and branch selection.</summary>
    [Fact]
    public void Delegates_AreValidatedEagerlyBeforeHashing()
    {
        var comparer = new CountingStringComparer();
        var empty = PersistentHashMap<string, int>.Create(comparer);
        var present = empty.SetItem("stored", 1);
        comparer.Reset();

        Assert.Throws<ArgumentNullException>(() => empty.GetOrAdd("missing", null!, out _));
        Assert.Equal(0, comparer.HashCalls);

        Assert.Throws<ArgumentNullException>(() => present.GetOrAdd("stored", null!, out _));
        Assert.Equal(0, comparer.HashCalls);

        Assert.Throws<ArgumentNullException>(() => present.AddOrUpdate("stored", null!, (_, value) => value, out _));
        Assert.Equal(0, comparer.HashCalls);

        Assert.Throws<ArgumentNullException>(() => present.AddOrUpdate("stored", _ => 2, null!, out _));
        Assert.Equal(0, comparer.HashCalls);
    }

    /// <summary>Verifies a GetOrAdd hit returns the stored value without invoking its factory.</summary>
    [Fact]
    public void GetOrAdd_HitReturnsStoredRepresentativeWithoutFactoryInvocation()
    {
        var comparer = new CountingStringComparer();
        var storedKey = new string(['A', 'l', 'p', 'h', 'a']);
        var storedValue = new EquatableValue(1, "stored");
        var map = PersistentHashMap<string, EquatableValue>.Create(comparer)
            .SetItem(storedKey, storedValue);
        var lookupKey = new string(['a', 'L', 'P', 'H', 'A']);
        var factoryCalls = 0;
        comparer.Reset();

        var result = map.GetOrAdd(
            lookupKey,
            _ =>
            {
                factoryCalls++;
                return new EquatableValue(2, "unexpected");
            },
            out var value);

        Assert.Same(map, result);
        Assert.Same(storedValue, value);
        Assert.Equal(0, factoryCalls);
        Assert.Equal(1, comparer.HashCalls);
        Assert.Equal(1, comparer.EqualityCalls);
        Assert.True(result.TryGetKey(lookupKey, out var actualKey));
        Assert.Same(storedKey, actualKey);
    }

    /// <summary>Verifies a GetOrAdd miss invokes its factory once and preserves the source.</summary>
    [Fact]
    public void GetOrAdd_MissAddsCallerKeyInOneHashedDescent()
    {
        var comparer = new CountingStringComparer();
        var source = PersistentHashMap<string, EquatableValue>.Create(comparer)
            .SetItem("existing", new EquatableValue(1, "existing"));
        var callerKey = new string(['n', 'e', 'w']);
        var selected = new EquatableValue(2, "selected");
        string? factoryKey = null;
        var factoryCalls = 0;
        comparer.Reset();

        var result = source.GetOrAdd(
            callerKey,
            key =>
            {
                factoryCalls++;
                factoryKey = key;
                return selected;
            },
            out var value);

        Assert.Equal(1, factoryCalls);
        Assert.Same(callerKey, factoryKey);
        Assert.Same(selected, value);
        Assert.Equal(1, comparer.HashCalls);
        Assert.False(source.ContainsKey(callerKey));
        Assert.Same(selected, result[callerKey]);
        Assert.True(result.TryGetKey(callerKey, out var actualKey));
        Assert.Same(callerKey, actualKey);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies AddOrUpdate selects only the update branch on a hit.</summary>
    [Fact]
    public void AddOrUpdate_HitInvokesUpdateOnceWithCallerKeyAndStoredValue()
    {
        var comparer = new CountingStringComparer();
        var storedKey = new string(['A', 'l', 'p', 'h', 'a']);
        var storedValue = new EquatableValue(1, "old");
        var replacement = new EquatableValue(2, "new");
        var source = PersistentHashMap<string, EquatableValue>.Create(comparer)
            .SetItem(storedKey, storedValue);
        var callerKey = new string(['a', 'l', 'p', 'h', 'a']);
        var addCalls = 0;
        var updateCalls = 0;
        string? observedKey = null;
        EquatableValue? observedValue = null;
        comparer.Reset();

        var result = source.AddOrUpdate(
            callerKey,
            _ =>
            {
                addCalls++;
                return new EquatableValue(3, "unexpected");
            },
            (key, value) =>
            {
                updateCalls++;
                observedKey = key;
                observedValue = value;
                return replacement;
            },
            out var selected);
        var hashCalls = comparer.HashCalls;
        var equalityCalls = comparer.EqualityCalls;

        Assert.Equal(0, addCalls);
        Assert.Equal(1, updateCalls);
        Assert.Same(callerKey, observedKey);
        Assert.Same(storedValue, observedValue);
        Assert.Same(replacement, selected);
        Assert.Same(replacement, result[callerKey]);
        Assert.Same(storedValue, source[callerKey]);
        Assert.Equal(1, hashCalls);
        Assert.Equal(1, equalityCalls);
        Assert.True(result.TryGetKey(callerKey, out var actualKey));
        Assert.Same(storedKey, actualKey);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies AddOrUpdate selects only the add branch on a miss.</summary>
    [Fact]
    public void AddOrUpdate_MissInvokesAddOnce()
    {
        var comparer = new CountingStringComparer();
        var source = PersistentHashMap<string, int>.Create(comparer).SetItem("old", 1);
        var addCalls = 0;
        var updateCalls = 0;
        comparer.Reset();

        var result = source.AddOrUpdate(
            "new",
            key =>
            {
                addCalls++;
                Assert.Equal("new", key);
                return 2;
            },
            (_, value) =>
            {
                updateCalls++;
                return value + 1;
            },
            out var selected);
        var hashCalls = comparer.HashCalls;

        Assert.Equal(1, addCalls);
        Assert.Equal(0, updateCalls);
        Assert.Equal(2, selected);
        Assert.Equal(2, result["new"]);
        Assert.False(source.ContainsKey("new"));
        Assert.Equal(1, hashCalls);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies equal replacement values retain both the map and stored value object.</summary>
    [Fact]
    public void AddOrUpdate_EqualReplacementRetainsStoredValueRepresentative()
    {
        var stored = new EquatableValue(7, "stored");
        var equalButDistinct = new EquatableValue(7, "replacement");
        var map = PersistentHashMap<int, EquatableValue>.Empty.SetItem(1, stored);
        var updateCalls = 0;

        var result = map.AddOrUpdate(
            1,
            _ => throw new InvalidOperationException("The add branch must not run."),
            (_, _) =>
            {
                updateCalls++;
                return equalButDistinct;
            },
            out var value);

        Assert.Equal(1, updateCalls);
        Assert.Same(map, result);
        Assert.Same(stored, value);
        Assert.Same(stored, result[1]);
        Assert.Same(map.RootForTesting, result.RootForTesting);
    }

    /// <summary>Verifies present-null values are not confused with absence.</summary>
    [Fact]
    public void PresentNullValue_SelectsHitBranches()
    {
        var map = PersistentHashMap<string, string?>.Empty.SetItem("key", null);
        var addCalls = 0;
        var updateCalls = 0;

        var getResult = map.GetOrAdd(
            "key",
            _ =>
            {
                addCalls++;
                return "added";
            },
            out var presentNull);
        var updateResult = map.AddOrUpdate(
            "key",
            _ =>
            {
                addCalls++;
                return "added";
            },
            (_, current) =>
            {
                updateCalls++;
                Assert.Null(current);
                return "updated";
            },
            out var updated);

        Assert.Same(map, getResult);
        Assert.Null(presentNull);
        Assert.Equal(0, addCalls);
        Assert.Equal(1, updateCalls);
        Assert.Equal("updated", updated);
        Assert.Equal("updated", updateResult["key"]);
        Assert.Null(map["key"]);
    }

    /// <summary>Verifies leaf, collision, and bitmap paths all preserve one-factory semantics.</summary>
    [Fact]
    public void NodeShapes_HandleHitsMissesAndTransitions()
    {
        var comparer = new ExplicitHashComparer();
        var leafKey = new ExplicitHashKey(1, 0x10);
        var collisionKey = new ExplicitHashKey(2, 0x10);
        var siblingKey = new ExplicitHashKey(3, 0x30);
        var deepKey = new ExplicitHashKey(4, 0x410);

        var leaf = PersistentHashMap<ExplicitHashKey, int>.Create(comparer)
            .GetOrAdd(leafKey, _ => 10, out var leafValue);
        var collision = leaf.GetOrAdd(collisionKey, _ => 20, out var collisionValue);
        var branch = collision.GetOrAdd(siblingKey, _ => 30, out var siblingValue);
        var deepBranch = branch.GetOrAdd(deepKey, _ => 40, out var deepValue);
        var updated = deepBranch.AddOrUpdate(
            collisionKey,
            _ => throw new InvalidOperationException("The add branch must not run."),
            (_, value) => value + 1,
            out var updatedValue);

        Assert.Equal(10, leafValue);
        Assert.Equal(20, collisionValue);
        Assert.Equal(30, siblingValue);
        Assert.Equal(40, deepValue);
        Assert.Equal(21, updatedValue);
        Assert.Equal(10, updated[leafKey]);
        Assert.Equal(21, updated[collisionKey]);
        Assert.Equal(30, updated[siblingKey]);
        Assert.Equal(40, updated[deepKey]);
        Assert.Equal(20, deepBranch[collisionKey]);
        leaf.ValidateCanonicalityForDiagnostics();
        collision.ValidateCanonicalityForDiagnostics();
        branch.ValidateCanonicalityForDiagnostics();
        deepBranch.ValidateCanonicalityForDiagnostics();
        updated.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies a bitmap-inline conflict promotes the existing entry into a child.</summary>
    [Fact]
    public void BitmapInlineConflict_PromotesEntryAndAddsInOneDescent()
    {
        var comparer = new ExplicitHashComparer();
        var inline = new ExplicitHashKey(1, 0x000);
        var sibling = new ExplicitHashKey(2, 0x001);
        var conflict = new ExplicitHashKey(3, 0x020);
        var source = PersistentHashMap<ExplicitHashKey, int>.Create(comparer)
            .SetItem(inline, 10)
            .SetItem(sibling, 20);
        var calls = 0;

        var result = source.GetOrAdd(
            conflict,
            key =>
            {
                calls++;
                Assert.Equal(conflict, key);
                return 30;
            },
            out var selected);

        Assert.Equal(1, calls);
        Assert.Equal(30, selected);
        Assert.Equal(10, result[inline]);
        Assert.Equal(20, result[sibling]);
        Assert.Equal(30, result[conflict]);
        Assert.False(source.ContainsKey(conflict));
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies collision hits and equal updates retain identity and value representatives.</summary>
    [Fact]
    public void CollisionNoOps_RetainMapAndStoredValueRepresentative()
    {
        var comparer = new CountingConstantHashIntComparer();
        var stored = new EquatableValue(1, "stored");
        var source = PersistentHashMap<int, EquatableValue>.Create(comparer)
            .SetItem(0, new EquatableValue(0, "zero"))
            .SetItem(1, stored)
            .SetItem(2, new EquatableValue(2, "two"));

        var getResult = source.GetOrAdd(1, _ => new EquatableValue(9, "unexpected"), out var getValue);
        var updateResult = source.AddOrUpdate(
            1,
            _ => new EquatableValue(9, "unexpected"),
            (_, _) => new EquatableValue(1, "equal"),
            out var updateValue);

        Assert.Same(source, getResult);
        Assert.Same(source, updateResult);
        Assert.Same(stored, getValue);
        Assert.Same(stored, updateValue);
        Assert.Same(stored, source[1]);
    }

    /// <summary>Verifies equal-hash collision scans have bounded comparer callbacks.</summary>
    [Fact]
    public void CollisionBucket_UsesOneHashAndOneLinearEqualityScan()
    {
        var comparer = new CountingConstantHashIntComparer();
        var map = PersistentHashMap<int, int>.Create(comparer);
        for (var key = 0; key < 32; key++)
            map = map.SetItem(key, key);

        comparer.Reset();
        var hit = map.AddOrUpdate(31, _ => -1, (_, value) => value + 1, out var hitValue);
        Assert.Equal(32, hitValue);
        Assert.Equal(1, comparer.HashCalls);
        Assert.Equal(32, comparer.EqualityCalls);

        comparer.Reset();
        var miss = map.GetOrAdd(32, _ => 32, out var missValue);
        Assert.Equal(32, missValue);
        Assert.Equal(1, comparer.HashCalls);
        Assert.Equal(32, comparer.EqualityCalls);
        Assert.Equal(32, map.Count);
        Assert.Equal(33, miss.Count);
        hit.ValidateCanonicalityForDiagnostics();
        miss.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies bitmap and deep-child paths keep exact hash and equality callback counts.</summary>
    [Fact]
    public void BitmapPaths_UseOneHashAndAtMostOneMatchingEqualityProbe()
    {
        var comparer = new CountingExplicitHashComparer();
        var direct = new ExplicitHashKey(1, 0x01);
        var sibling = new ExplicitHashKey(2, 0x02);
        var absentSlot = new ExplicitHashKey(3, 0x03);
        var directMap = PersistentHashMap<ExplicitHashKey, int>.Create(comparer)
            .SetItem(direct, 1)
            .SetItem(sibling, 2);

        comparer.Reset();
        var same = directMap.GetOrAdd(direct, _ => -1, out var directValue);
        Assert.Same(directMap, same);
        Assert.Equal(1, directValue);
        Assert.Equal(1, comparer.HashCalls);
        Assert.Equal(1, comparer.EqualityCalls);

        comparer.Reset();
        var added = directMap.GetOrAdd(absentSlot, _ => 3, out var addedValue);
        Assert.Equal(3, addedValue);
        Assert.Equal(1, comparer.HashCalls);
        Assert.Equal(0, comparer.EqualityCalls);
        Assert.Equal(3, added.Count);

        var deepLeft = new ExplicitHashKey(10, 0x000);
        var deepRight = new ExplicitHashKey(11, 0x020);
        var deepMap = PersistentHashMap<ExplicitHashKey, int>.Create(comparer)
            .SetItem(deepLeft, 10)
            .SetItem(deepRight, 11);
        comparer.Reset();

        var deepResult = deepMap.AddOrUpdate(
            deepRight,
            _ => -1,
            (_, value) => value + 1,
            out var deepValue);

        Assert.Equal(12, deepValue);
        Assert.Equal(1, comparer.HashCalls);
        Assert.Equal(1, comparer.EqualityCalls);
        deepResult.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies leaf hits and equal-value updates allocate no temporary entry arrays.</summary>
    [Fact]
    public void LeafNoOps_AllocateNothingAfterWarmup()
    {
        var map = PersistentHashMap<int, int>.Empty.SetItem(1, 10);
        Func<int, int> addFactory = static _ => -1;
        Func<int, int, int> updateFactory = static (_, value) => value;

        _ = map.GetOrAdd(1, addFactory, out _);
        _ = map.AddOrUpdate(1, addFactory, updateFactory, out _);

        var beforeGet = GC.GetAllocatedBytesForCurrentThread();
        var getResult = map.GetOrAdd(1, addFactory, out var getValue);
        var afterGet = GC.GetAllocatedBytesForCurrentThread();

        var beforeUpdate = GC.GetAllocatedBytesForCurrentThread();
        var updateResult = map.AddOrUpdate(1, addFactory, updateFactory, out var updateValue);
        var afterUpdate = GC.GetAllocatedBytesForCurrentThread();

        Assert.Same(map, getResult);
        Assert.Same(map, updateResult);
        Assert.Equal(10, getValue);
        Assert.Equal(10, updateValue);
        Assert.Equal(0, afterGet - beforeGet);
        Assert.Equal(0, afterUpdate - beforeUpdate);
    }

    /// <summary>Verifies persistent operations handle roots published by the separate transient kernel.</summary>
    [Fact]
    public void PublishedTransientNodes_AreAcceptedBySinglePassUpdates()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(key => KeyValuePair.Create(key, key)));
        var transient = source.ToTransient();
        transient.SetItem(513, -1);
        transient.SetItem(513, -2);
        var published = transient.Persist();

        Assert.IsType<PersistentHashMap<int, int>.SeparateTransientBranchNode>(published.RootForTesting);

        var sameHit = published.GetOrAdd(513, _ => 100, out var sameHitValue);
        var sameUpdate = published.AddOrUpdate(513, _ => 100, (_, value) => value, out var sameUpdateValue);
        Assert.Same(published, sameHit);
        Assert.Same(published, sameUpdate);
        Assert.Equal(-2, sameHitValue);
        Assert.Equal(-2, sameUpdateValue);

        var hit = published.AddOrUpdate(513, _ => -1, (_, value) => value - 1, out var hitValue);
        var miss = published.GetOrAdd(2_000, _ => 2_000, out var missValue);

        Assert.Equal(-3, hitValue);
        Assert.Equal(2_000, missValue);
        Assert.Equal(-2, published[513]);
        Assert.Equal(-3, hit[513]);
        Assert.Equal(2_000, miss[2_000]);
        published.ValidateCanonicalityForDiagnostics();
        hit.ValidateCanonicalityForDiagnostics();
        miss.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies a collision root published by a transient accepts persistent updates.</summary>
    [Fact]
    public void PublishedTransientCollision_AcceptsSinglePassUpdates()
    {
        var comparer = new CountingConstantHashIntComparer();
        var source = PersistentHashMap<int, int>.Create(comparer)
            .SetItem(0, 0)
            .SetItem(1, 1)
            .SetItem(2, 2);
        var transient = source.ToTransient();
        transient.SetItem(1, -1);
        transient.SetItem(1, -2);
        var published = transient.Persist();

        Assert.IsType<PersistentHashMap<int, int>.SeparateTransientCollisionNode>(published.RootForTesting);

        var sameHit = published.GetOrAdd(1, _ => 100, out var sameHitValue);
        var sameUpdate = published.AddOrUpdate(1, _ => 100, (_, value) => value, out var sameUpdateValue);
        Assert.Same(published, sameHit);
        Assert.Same(published, sameUpdate);
        Assert.Equal(-2, sameHitValue);
        Assert.Equal(-2, sameUpdateValue);

        var hit = published.AddOrUpdate(1, _ => 100, (_, value) => value - 1, out var hitValue);
        var miss = published.GetOrAdd(3, _ => 3, out var missValue);

        Assert.Equal(-3, hitValue);
        Assert.Equal(3, missValue);
        Assert.Equal(-2, published[1]);
        Assert.Equal(-3, hit[1]);
        Assert.Equal(3, miss[3]);
        published.ValidateCanonicalityForDiagnostics();
        hit.ValidateCanonicalityForDiagnostics();
        miss.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies factories cannot publish partial results from any persistent node shape.</summary>
    [Fact]
    public void FactoryFailures_AcrossNodeShapesPreserveSourceRoots()
    {
        var collisionComparer = new CountingConstantHashIntComparer();
        var collision = PersistentHashMap<int, int>.Create(collisionComparer)
            .SetItem(0, 0)
            .SetItem(1, 1)
            .SetItem(2, 2);
        AssertFactoryFailuresPreserveRoot(collision, hitKey: 1, missKey: 3);

        var bitmap = PersistentHashMap<int, int>.Empty
            .SetItem(1, 1)
            .SetItem(2, 2)
            .SetItem(65, 65);
        AssertFactoryFailuresPreserveRoot(bitmap, hitKey: 65, missKey: 99);

        var branchTransient = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(key => KeyValuePair.Create(key, key)))
            .ToTransient();
        branchTransient.SetItem(513, -1);
        branchTransient.SetItem(513, -2);
        var separateBranch = branchTransient.Persist();
        Assert.IsType<PersistentHashMap<int, int>.SeparateTransientBranchNode>(separateBranch.RootForTesting);
        AssertFactoryFailuresPreserveRoot(separateBranch, hitKey: 513, missKey: 2_000);

        var collisionTransient = collision.ToTransient();
        collisionTransient.SetItem(1, -1);
        collisionTransient.SetItem(1, -2);
        var separateCollision = collisionTransient.Persist();
        Assert.IsType<PersistentHashMap<int, int>.SeparateTransientCollisionNode>(
            separateCollision.RootForTesting);
        AssertFactoryFailuresPreserveRoot(separateCollision, hitKey: 1, missKey: 3);
    }

    /// <summary>Verifies comparer and value-equality failures across collision and bitmap nodes.</summary>
    [Fact]
    public void EqualityFailures_AcrossNodeShapesPreserveSourceRoots()
    {
        var collisionComparer = new ConfigurableKeyComparer(constantHash: true);
        var collision = PersistentHashMap<ExplicitHashKey, ThrowingValue>.Create(collisionComparer)
            .SetItem(new ExplicitHashKey(1, 0), new ThrowingValue(1))
            .SetItem(new ExplicitHashKey(2, 0), new ThrowingValue(2));
        var collisionRoot = collision.RootForTesting;
        collisionComparer.ThrowOnEquality = true;
        Assert.Throws<TestException>(() => collision.GetOrAdd(
            new ExplicitHashKey(2, 0),
            _ => new ThrowingValue(20),
            out _));
        collisionComparer.ThrowOnEquality = false;
        Assert.Same(collisionRoot, collision.RootForTesting);

        var collisionValue = collision[new ExplicitHashKey(2, 0)];
        collisionValue.ThrowOnEquality = true;
        Assert.Throws<TestException>(() => collision.AddOrUpdate(
            new ExplicitHashKey(2, 0),
            _ => new ThrowingValue(20),
            (_, _) => new ThrowingValue(20),
            out _));
        collisionValue.ThrowOnEquality = false;
        Assert.Same(collisionRoot, collision.RootForTesting);

        var bitmapComparer = new ConfigurableKeyComparer(constantHash: false);
        var bitmapKey = new ExplicitHashKey(10, 0x10);
        var bitmap = PersistentHashMap<ExplicitHashKey, ThrowingValue>.Create(bitmapComparer)
            .SetItem(bitmapKey, new ThrowingValue(10))
            .SetItem(new ExplicitHashKey(11, 0x11), new ThrowingValue(11));
        var bitmapRoot = bitmap.RootForTesting;
        bitmapComparer.ThrowOnEquality = true;
        Assert.Throws<TestException>(() => bitmap.GetOrAdd(
            bitmapKey,
            _ => new ThrowingValue(100),
            out _));
        bitmapComparer.ThrowOnEquality = false;
        Assert.Same(bitmapRoot, bitmap.RootForTesting);

        var bitmapValue = bitmap[bitmapKey];
        bitmapValue.ThrowOnEquality = true;
        Assert.Throws<TestException>(() => bitmap.AddOrUpdate(
            bitmapKey,
            _ => new ThrowingValue(100),
            (_, _) => new ThrowingValue(100),
            out _));
        bitmapValue.ThrowOnEquality = false;
        Assert.Same(bitmapRoot, bitmap.RootForTesting);
    }

    /// <summary>Verifies factory, hash, equality, and value-equality failures publish no successor.</summary>
    [Fact]
    public void CallbackFailures_LeaveSourceRootAndContentsUnchanged()
    {
        var comparer = new ThrowingStringComparer();
        var source = PersistentHashMap<string, ThrowingValue>.Create(comparer)
            .SetItem("stored", new ThrowingValue(1));
        var root = source.RootForTesting;

        Assert.Throws<TestException>(() => source.GetOrAdd(
            "missing",
            _ => throw new TestException(),
            out _));
        Assert.Same(root, source.RootForTesting);

        Assert.Throws<TestException>(() => source.AddOrUpdate(
            "stored",
            _ => new ThrowingValue(2),
            (_, _) => throw new TestException(),
            out _));
        Assert.Same(root, source.RootForTesting);

        comparer.ThrowOnHash = true;
        Assert.Throws<TestException>(() => source.GetOrAdd("missing", _ => new ThrowingValue(2), out _));
        comparer.ThrowOnHash = false;
        Assert.Same(root, source.RootForTesting);

        comparer.ThrowOnEquality = true;
        Assert.Throws<TestException>(() => source.GetOrAdd("stored", _ => new ThrowingValue(2), out _));
        comparer.ThrowOnEquality = false;
        Assert.Same(root, source.RootForTesting);

        var stored = source["stored"];
        stored.ThrowOnEquality = true;
        Assert.Throws<TestException>(() => source.AddOrUpdate(
            "stored",
            _ => new ThrowingValue(2),
            (_, _) => new ThrowingValue(2),
            out _));
        stored.ThrowOnEquality = false;

        Assert.Same(root, source.RootForTesting);
        Assert.Same(stored, source["stored"]);
        Assert.Single(source);
        source.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies generated histories against a mutable model and retained snapshots.</summary>
    [Fact]
    public void GeneratedHistories_MatchDictionaryAndPreserveRetainedVersions()
    {
        const int operationCount = 2_000;
        var random = new Random(0x51A6_1E);
        var map = PersistentHashMap<int, int>.Empty;
        var model = new Dictionary<int, int>();
        var retained = new List<(PersistentHashMap<int, int> Map, Dictionary<int, int> Model)>();

        for (var operation = 0; operation < operationCount; operation++)
        {
            if (operation % 97 == 0)
                retained.Add((map, new Dictionary<int, int>(model)));

            var key = random.Next(-64, 65);
            if ((random.Next() & 1) == 0)
            {
                var addValue = random.Next();
                var factoryCalls = 0;
                var result = map.GetOrAdd(
                    key,
                    _ =>
                    {
                        factoryCalls++;
                        return addValue;
                    },
                    out var selected);

                if (model.TryGetValue(key, out var existing))
                {
                    Assert.Same(map, result);
                    Assert.Equal(existing, selected);
                    Assert.Equal(0, factoryCalls);
                }
                else
                {
                    model.Add(key, addValue);
                    Assert.Equal(addValue, selected);
                    Assert.Equal(1, factoryCalls);
                }

                map = result;
            }
            else
            {
                var addValue = random.Next();
                var delta = random.Next(-10, 11);
                var addCalls = 0;
                var updateCalls = 0;
                var result = map.AddOrUpdate(
                    key,
                    _ =>
                    {
                        addCalls++;
                        return addValue;
                    },
                    (_, value) =>
                    {
                        updateCalls++;
                        return unchecked(value + delta);
                    },
                    out var selected);

                if (model.TryGetValue(key, out var existing))
                {
                    var expected = unchecked(existing + delta);
                    model[key] = expected;
                    Assert.Equal(expected, selected);
                    Assert.Equal(0, addCalls);
                    Assert.Equal(1, updateCalls);
                }
                else
                {
                    model.Add(key, addValue);
                    Assert.Equal(addValue, selected);
                    Assert.Equal(1, addCalls);
                    Assert.Equal(0, updateCalls);
                }

                map = result;
            }

            AssertMatches(model, map);
            map.ValidateCanonicalityForDiagnostics();
        }

        foreach (var snapshot in retained)
        {
            AssertMatches(snapshot.Model, snapshot.Map);
            snapshot.Map.ValidateCanonicalityForDiagnostics();
        }
    }

    private static void AssertMatches(
        IReadOnlyDictionary<int, int> expected,
        PersistentHashMap<int, int> actual)
    {
        Assert.Equal(expected.Count, actual.Count);
        foreach (var (key, value) in expected)
            Assert.Equal(value, actual[key]);
        Assert.Equal(
            expected.OrderBy(pair => pair.Key),
            actual.OrderBy(pair => pair.Key));
    }

    private static void AssertFactoryFailuresPreserveRoot(
        PersistentHashMap<int, int> source,
        int hitKey,
        int missKey)
    {
        var root = source.RootForTesting;
        var expected = source.OrderBy(pair => pair.Key).ToArray();

        Assert.Throws<TestException>(() => source.GetOrAdd(
            missKey,
            _ => throw new TestException(),
            out _));
        Assert.Same(root, source.RootForTesting);

        Assert.Throws<TestException>(() => source.AddOrUpdate(
            hitKey,
            _ => -1,
            (_, _) => throw new TestException(),
            out _));
        Assert.Same(root, source.RootForTesting);
        Assert.Equal(expected, source.OrderBy(pair => pair.Key));
        source.ValidateCanonicalityForDiagnostics();
    }

    private sealed class CountingStringComparer : IEqualityComparer<string>
    {
        public int HashCalls { get; private set; }
        public int EqualityCalls { get; private set; }

        public bool Equals(string? x, string? y)
        {
            EqualityCalls++;
            return StringComparer.OrdinalIgnoreCase.Equals(x, y);
        }

        public int GetHashCode(string obj)
        {
            HashCalls++;
            return StringComparer.OrdinalIgnoreCase.GetHashCode(obj);
        }

        public void Reset()
        {
            HashCalls = 0;
            EqualityCalls = 0;
        }
    }

    private sealed class CountingConstantHashIntComparer : IEqualityComparer<int>
    {
        public int HashCalls { get; private set; }
        public int EqualityCalls { get; private set; }

        public bool Equals(int x, int y)
        {
            EqualityCalls++;
            return x == y;
        }

        public int GetHashCode(int obj)
        {
            HashCalls++;
            return 0;
        }

        public void Reset()
        {
            HashCalls = 0;
            EqualityCalls = 0;
        }
    }

    private sealed class CountingExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        public int HashCalls { get; private set; }
        public int EqualityCalls { get; private set; }

        public bool Equals(ExplicitHashKey x, ExplicitHashKey y)
        {
            EqualityCalls++;
            return x.Id == y.Id;
        }

        public int GetHashCode(ExplicitHashKey obj)
        {
            HashCalls++;
            return obj.Hash;
        }

        public void Reset()
        {
            HashCalls = 0;
            EqualityCalls = 0;
        }
    }

    private sealed class ConfigurableKeyComparer(bool constantHash) : IEqualityComparer<ExplicitHashKey>
    {
        public bool ThrowOnEquality { get; set; }

        public bool Equals(ExplicitHashKey x, ExplicitHashKey y)
        {
            if (ThrowOnEquality)
                throw new TestException();
            return x.Id == y.Id;
        }

        public int GetHashCode(ExplicitHashKey obj) => constantHash ? 0 : obj.Hash;
    }

    private sealed class ThrowingStringComparer : IEqualityComparer<string>
    {
        public bool ThrowOnHash { get; set; }
        public bool ThrowOnEquality { get; set; }

        public bool Equals(string? x, string? y)
        {
            if (ThrowOnEquality)
                throw new TestException();
            return StringComparer.Ordinal.Equals(x, y);
        }

        public int GetHashCode(string obj)
        {
            if (ThrowOnHash)
                throw new TestException();
            return StringComparer.Ordinal.GetHashCode(obj);
        }
    }

    private sealed class ThrowingValue(int id) : IEquatable<ThrowingValue>
    {
        private int Id { get; } = id;

        public bool ThrowOnEquality { get; set; }

        public bool Equals(ThrowingValue? other)
        {
            if (ThrowOnEquality)
                throw new TestException();
            return other is not null && Id == other.Id;
        }

        public override bool Equals(object? obj) => obj is ThrowingValue other && Equals(other);

        public override int GetHashCode() => Id;
    }

    private sealed record EquatableValue(int Id, string Label)
    {
        public bool Equals(EquatableValue? other) => other is not null && Id == other.Id;

        public override int GetHashCode() => Id;
    }

    private readonly record struct ExplicitHashKey(int Id, int Hash);

    private sealed class ExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        public bool Equals(ExplicitHashKey x, ExplicitHashKey y) => x.Id == y.Id;

        public int GetHashCode(ExplicitHashKey obj) => obj.Hash;
    }

    private sealed class TestException : Exception;
}
