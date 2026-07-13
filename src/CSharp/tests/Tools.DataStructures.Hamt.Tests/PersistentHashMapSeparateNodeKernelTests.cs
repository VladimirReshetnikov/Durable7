using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Exercises the second private T1 layout with distinct transient-editable node classes.</summary>
public sealed class PersistentHashMapSeparateNodeKernelTests
{
    /// <summary>Verifies constant-time clean adoption/publication and one-way consumption.</summary>
    [Fact]
    public void CleanAdoptionAndPublication_AreConstantTimeAndConsumeTheSession()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 512).Select(index => KeyValuePair.Create(index, index * 7)));
        var kernel = source.CreateSeparateNodeTransientKernel();

        Assert.Equal(
            PersistentHashMap<int, int>.TransientOwnershipLayout.SeparateNodes,
            kernel.LayoutForDiagnostics);
        Assert.Equal(source.Count, kernel.Count);
        Assert.Same(source.Comparer, kernel.Comparer);
        kernel.SetItem(17, 17 * 7);
        Assert.False(kernel.TryAdd(17, -1));
        Assert.False(kernel.Remove(-1));

        var result = kernel.Persist();
        var counters = kernel.GetCountersForDiagnostics();

        Assert.Same(source, result);
        Assert.Equal(1, counters.AdoptionCount);
        Assert.Equal(0, counters.AdoptionNodeVisits);
        Assert.Equal(1, counters.PublicationCount);
        Assert.Equal(0, counters.PublicationNodeVisits);
        Assert.Equal(0, counters.PersistentWrapperAllocationCount);
        Assert.False(kernel.IsActiveForDiagnostics);
        Assert.False(kernel.TokenIsActiveForDiagnostics);
        Assert.Throws<ObjectDisposedException>(() => _ = kernel.Count);
        Assert.Throws<ObjectDisposedException>(() => kernel.SetItem(1, -1));
        Assert.Throws<ObjectDisposedException>(() => kernel.Persist());
    }

    /// <summary>Verifies the edited graph uses separate classes and then reuses its owned path.</summary>
    [Fact]
    public void RepeatedPathEdits_CreateSeparateNodesThenWriteInPlace()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(index => KeyValuePair.Create(index, index)));
        var sourceRoot = source.RootForTesting;
        var sourceStructure = source.GetStructureDiagnostics();
        var kernel = source.CreateSeparateNodeTransientKernel();

        for (var value = 0; value < 128; value++)
            kernel.SetItem(513, -value - 1);

        var result = kernel.Persist();
        var counters = kernel.GetCountersForDiagnostics();
        var structure = result.GetStructureDiagnostics();
        var ordinaryRoot = Assert.IsType<PersistentHashMap<int, int>.BitmapIndexedNode>(sourceRoot);
        var separateRoot = Assert.IsType<PersistentHashMap<int, int>.SeparateTransientBranchNode>(
            result.RootForTesting);

        Assert.Equal(-128, result[513]);
        Assert.Equal(513, source[513]);
        Assert.Same(sourceRoot, source.RootForTesting);
        Assert.True(counters.CopiedNodeCount > 0);
        Assert.True(counters.CopiedArrayCount > 0);
        Assert.True(counters.InPlaceNodeMutationCount >= 127);
        Assert.True(counters.InPlaceArrayWriteCount >= 127);
        Assert.Equal(1, counters.PersistentWrapperAllocationCount);
        Assert.Equal(0, sourceStructure.SeparateBranchNodeCount);
        Assert.Equal(0, sourceStructure.SeparateCollisionNodeCount);
        Assert.Equal(0, sourceStructure.EstimatedSeparateNodeMetadataBytes);
        Assert.True(structure.SeparateBranchNodeCount > 0);
        Assert.Equal(0, structure.SeparateCollisionNodeCount);
        Assert.True(structure.EstimatedSeparateNodeMetadataBytes > 0);
        Assert.Equal(1, structure.OwnerTokenCount);
        Assert.NotSame(ordinaryRoot.Data, separateRoot.Data);
        Assert.NotSame(ordinaryRoot.Children, separateRoot.Children);
        result.ValidateCanonicalityForDiagnostics();

        var secondKernel = result.CreateSeparateNodeTransientKernel();
        secondKernel.SetItem(513, 42);
        var second = secondKernel.Persist();
        Assert.Equal(-128, result[513]);
        Assert.Equal(42, second[513]);
        second.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies collisions, representatives, comparer identity, no-ops, and contraction.</summary>
    [Fact]
    public void CollisionHistory_PreservesChampSemanticsAndUsesSeparateCollisionNodes()
    {
        var comparer = new ConstantHashOrdinalIgnoreCaseComparer();
        var storedKey = NewString("Alpha");
        var equivalentKey = NewString("ALPHA");
        var source = PersistentHashMap<string, string>.Create(comparer)
            .SetItem(storedKey, NewString("one"))
            .SetItem("Beta", "two")
            .SetItem("Gamma", "three");
        var kernel = source.CreateSeparateNodeTransientKernel();

        var version = kernel.VersionForDiagnostics;
        kernel.SetItem(equivalentKey, NewString("one"));
        Assert.Equal(version, kernel.VersionForDiagnostics);
        Assert.False(kernel.TryAdd(equivalentKey, "duplicate"));
        Assert.Equal(version, kernel.VersionForDiagnostics);

        kernel.SetItem(equivalentKey, "changed");
        kernel.SetItem("Delta", "four");
        Assert.True(kernel.Remove("BETA"));
        Assert.False(kernel.Remove("missing"));
        Assert.True(kernel.TryGetKey(equivalentKey, out var actualKey));
        Assert.Same(storedKey, actualKey);

        var result = kernel.Persist();
        var structure = result.GetStructureDiagnostics();
        Assert.Same(comparer, result.Comparer);
        Assert.Same(storedKey, result.First(pair => comparer.Equals(pair.Key, storedKey)).Key);
        Assert.Equal("changed", result[equivalentKey]);
        Assert.Equal("four", result["DELTA"]);
        Assert.False(result.ContainsKey("Beta"));
        Assert.True(structure.SeparateCollisionNodeCount > 0);
        Assert.Equal(1, structure.OwnerTokenCount);
        result.ValidateCanonicalityForDiagnostics();

        var contraction = result.CreateSeparateNodeTransientKernel();
        Assert.True(contraction.Remove("Gamma"));
        Assert.True(contraction.Remove("Delta"));
        var singleton = contraction.Persist();
        Assert.Single(singleton);
        Assert.IsType<PersistentHashMap<string, string>.LeafNode>(singleton.RootForTesting);
        singleton.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Compares mixed edits and subsequent persistent operations with a persistent oracle.</summary>
    [Fact]
    public void MixedHistoryAndPostPublicationOperations_MatchPersistentOracle()
    {
        var comparer = new ExplicitHashComparer();
        var source = PersistentHashMap<ExplicitHashKey, int>.Create(comparer);
        for (var index = 0; index < 256; index++)
            source = source.SetItem(new ExplicitHashKey(index, ClusteredHash(index)), index);

        var oracle = source;
        var kernel = source.CreateSeparateNodeTransientKernel();
        var random = new Random(0x7261);
        for (var operation = 0; operation < 800; operation++)
        {
            var value = random.Next(384);
            var key = new ExplicitHashKey(value, ClusteredHash(value));
            if (random.Next(5) == 0)
            {
                Assert.Equal(oracle.ContainsKey(key), kernel.Remove(key));
                oracle = oracle.Remove(key);
            }
            else
            {
                var replacement = random.Next();
                kernel.SetItem(key, replacement);
                oracle = oracle.SetItem(key, replacement);
            }
        }

        var published = kernel.Persist();
        Assert.True(published.MapEquals(oracle));
        Assert.Equal(oracle.ToArray(), published.ToArray());
        published.ValidateCanonicalityForDiagnostics();

        var addedKey = new ExplicitHashKey(10_000, ClusteredHash(10_000));
        var persistentResult = published.SetItem(addedKey, 42).Remove(new ExplicitHashKey(7, ClusteredHash(7)));
        var persistentOracle = oracle.SetItem(addedKey, 42).Remove(new ExplicitHashKey(7, ClusteredHash(7)));
        Assert.True(persistentResult.MapEquals(persistentOracle));
        Assert.Equal(persistentOracle.ToArray(), persistentResult.ToArray());
        persistentResult.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies every deterministic preparation boundary leaves state untouched.</summary>
    [Fact]
    public void EveryPreparationFailpoint_LeavesRootContentVersionAndCountersUnchanged()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 256).Select(index => KeyValuePair.Create(index, index)));
        var observedFailures = 0;

        for (var failAt = 0; failAt < 64; failAt++)
        {
            var kernel = source.CreateSeparateNodeTransientKernel();
            var root = kernel.RootIdentityForDiagnostics;
            var expected = kernel.ToArrayForDiagnostics();
            var before = kernel.GetCountersForDiagnostics();
            var invocation = 0;
            kernel.FailureInjector = _ =>
            {
                if (invocation++ == failAt)
                    throw new TestFailureException();
            };

            try
            {
                kernel.SetItem(129, -1);
                Assert.True(failAt > 0);
                break;
            }
            catch (TestFailureException)
            {
                observedFailures++;
                kernel.FailureInjector = null;
                Assert.True(kernel.IsActiveForDiagnostics);
                Assert.True(kernel.TokenIsActiveForDiagnostics);
                Assert.Same(root, kernel.RootIdentityForDiagnostics);
                Assert.Equal(0, kernel.VersionForDiagnostics);
                Assert.Equal(before, kernel.GetCountersForDiagnostics());
                Assert.Equal(expected, kernel.ToArrayForDiagnostics());
                Assert.Equal(129, source[129]);
            }
        }

        // The separate hierarchy additionally prepares ownership copies for independently shared
        // data and child arrays, so it exposes at least the owner-field layout's failure boundaries.
        Assert.True(observedFailures >= 4);
    }

    /// <summary>Verifies a failed second edit cannot partly mutate an already-owned separate node.</summary>
    [Fact]
    public void FailedPreparedInPlaceWrite_DoesNotChangeOwnedStorage()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 512).Select(index => KeyValuePair.Create(index, index)));
        var kernel = source.CreateSeparateNodeTransientKernel();
        kernel.SetItem(257, -1);
        var root = kernel.RootIdentityForDiagnostics;
        var expected = kernel.ToArrayForDiagnostics();
        var version = kernel.VersionForDiagnostics;
        var counters = kernel.GetCountersForDiagnostics();
        kernel.FailureInjector = point =>
        {
            if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.MutationPrepared)
                throw new TestFailureException();
        };

        Assert.Throws<TestFailureException>(() => kernel.SetItem(257, -2));
        kernel.FailureInjector = null;

        Assert.Same(root, kernel.RootIdentityForDiagnostics);
        Assert.Equal(expected, kernel.ToArrayForDiagnostics());
        Assert.Equal(version, kernel.VersionForDiagnostics);
        Assert.Equal(counters, kernel.GetCountersForDiagnostics());
        kernel.SetItem(257, -3);
        Assert.Equal(-3, kernel.TryGetValue(257, out var value) ? value : 0);
    }

    /// <summary>Verifies throwing hash/equality/value callbacks cannot mutate owned nodes.</summary>
    [Fact]
    public void CallbackFailures_LeaveOwnedSeparateGraphAndBaseUsable()
    {
        var comparer = new SwitchableThrowingComparer();
        var source = PersistentHashMap<string, ThrowingValue>.Create(comparer)
            .SetItem("alpha", new ThrowingValue("one"))
            .SetItem("beta", new ThrowingValue("two"));
        var kernel = source.CreateSeparateNodeTransientKernel();
        kernel.SetItem("alpha", new ThrowingValue("changed"));
        var root = kernel.RootIdentityForDiagnostics;
        var expected = kernel.ToArrayForDiagnostics();
        var version = kernel.VersionForDiagnostics;
        var counters = kernel.GetCountersForDiagnostics();

        comparer.ThrowHash = true;
        Assert.Throws<TestFailureException>(() => kernel.SetItem("alpha", new ThrowingValue("hash")));
        comparer.ThrowHash = false;
        Assert.Same(root, kernel.RootIdentityForDiagnostics);

        comparer.ThrowEquals = true;
        Assert.Throws<TestFailureException>(() => kernel.Remove("alpha"));
        comparer.ThrowEquals = false;
        Assert.Same(root, kernel.RootIdentityForDiagnostics);

        var stored = kernel.ToArrayForDiagnostics().Single(pair => pair.Key == "alpha").Value;
        stored.ThrowEquals = true;
        Assert.Throws<TestFailureException>(() => kernel.SetItem("alpha", new ThrowingValue("changed")));
        stored.ThrowEquals = false;

        Assert.Same(root, kernel.RootIdentityForDiagnostics);
        Assert.Equal(expected, kernel.ToArrayForDiagnostics());
        Assert.Equal(version, kernel.VersionForDiagnostics);
        Assert.Equal(counters, kernel.GetCountersForDiagnostics());
        Assert.Equal("one", source["alpha"].Text);
    }

    /// <summary>Verifies failed publication leaves a dirty separate-node session active.</summary>
    [Fact]
    public void PublicationFailures_LeavePreparedSessionActiveAndUnpublished()
    {
        var source = PersistentHashMap<int, int>.Empty.SetItem(1, 1);
        var kernel = source.CreateSeparateNodeTransientKernel();
        kernel.SetItem(1, 2);
        var root = kernel.RootIdentityForDiagnostics;
        var version = kernel.VersionForDiagnostics;

        foreach (var target in new[]
                 {
                     PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforePublicationAllocation,
                     PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.PublicationPrepared,
                 })
        {
            kernel.FailureInjector = point =>
            {
                if (point == target)
                    throw new TestFailureException();
            };
            Assert.Throws<TestFailureException>(() => kernel.Persist());
            Assert.True(kernel.IsActiveForDiagnostics);
            Assert.True(kernel.TokenIsActiveForDiagnostics);
            Assert.Same(root, kernel.RootIdentityForDiagnostics);
            Assert.Equal(version, kernel.VersionForDiagnostics);
            Assert.Equal(2, kernel.TryGetValue(1, out var value) ? value : -1);
            Assert.Equal(1, source[1]);
        }

        kernel.FailureInjector = null;
        var result = kernel.Persist();
        Assert.Equal(2, result[1]);
        result.ValidateCanonicalityForDiagnostics();
    }

    private static string NewString(string value) => new(value.ToCharArray());

    private static int ClusteredHash(int value) => unchecked((value << 15) | 0x2a5b);

    private readonly record struct ExplicitHashKey(int Value, int Hash);

    private sealed class ExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        public bool Equals(ExplicitHashKey left, ExplicitHashKey right) => left.Value == right.Value;

        public int GetHashCode(ExplicitHashKey value) => value.Hash;
    }

    private sealed class ConstantHashOrdinalIgnoreCaseComparer : IEqualityComparer<string>
    {
        public bool Equals(string? left, string? right) =>
            StringComparer.OrdinalIgnoreCase.Equals(left, right);

        public int GetHashCode(string value) => 0;
    }

    private sealed class SwitchableThrowingComparer : IEqualityComparer<string>
    {
        internal bool ThrowHash { get; set; }

        internal bool ThrowEquals { get; set; }

        public bool Equals(string? left, string? right)
        {
            if (ThrowEquals)
                throw new TestFailureException();
            return StringComparer.Ordinal.Equals(left, right);
        }

        public int GetHashCode(string value)
        {
            if (ThrowHash)
                throw new TestFailureException();
            return 0;
        }
    }

    private sealed class ThrowingValue(string text) : IEquatable<ThrowingValue>
    {
        internal string Text { get; } = text;

        internal bool ThrowEquals { get; set; }

        public bool Equals(ThrowingValue? other)
        {
            if (ThrowEquals)
                throw new TestFailureException();
            return other is not null && Text == other.Text;
        }

        public override bool Equals(object? obj) => obj is ThrowingValue other && Equals(other);

        public override int GetHashCode() => Text.GetHashCode(StringComparison.Ordinal);
    }

    private sealed class TestFailureException : Exception
    {
    }
}
