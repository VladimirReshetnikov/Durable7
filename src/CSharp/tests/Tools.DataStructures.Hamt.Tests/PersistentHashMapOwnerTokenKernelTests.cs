using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Exercises the private Axis 2 T1 owner-token kernel before any public API is exposed.</summary>
public sealed class PersistentHashMapOwnerTokenKernelTests
{
    /// <summary>Verifies that clean adoption and publication visit no nodes and consume the session.</summary>
    [Fact]
    public void CleanAdoptionAndPublication_AreConstantTimeAndConsumeTheSession()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 512).Select(index => KeyValuePair.Create(index, index * 3)));
        var kernel = source.CreateOwnerTokenTransientKernel();

        Assert.Equal(source.Count, kernel.Count);
        Assert.Same(source.Comparer, kernel.Comparer);
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

    /// <summary>Verifies copy-on-first-write followed by in-place writes along an owned path.</summary>
    [Fact]
    public void RepeatedPathEdits_CopySharedStorageOnceThenWriteInPlace()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(index => KeyValuePair.Create(index, index)));
        var sourceRoot = source.RootForTesting;
        var kernel = source.CreateOwnerTokenTransientKernel();

        for (var value = 0; value < 128; value++)
            kernel.SetItem(513, -value - 1);

        var result = kernel.Persist();
        var counters = kernel.GetCountersForDiagnostics();
        var canonicality = result.ValidateCanonicalityForDiagnostics();

        Assert.Equal(1_024, canonicality.RecursiveEntryCount);
        Assert.Equal(-128, result[513]);
        Assert.Equal(513, source[513]);
        Assert.Same(sourceRoot, source.RootForTesting);
        Assert.True(counters.CopiedNodeCount > 0);
        Assert.True(counters.CopiedArrayCount > 0);
        Assert.True(counters.InPlaceArrayWriteCount >= 127);
        Assert.True(counters.InPlaceNodeMutationCount >= 127);
        Assert.Equal(1, counters.PersistentWrapperAllocationCount);

        var secondKernel = result.CreateOwnerTokenTransientKernel();
        secondKernel.SetItem(513, 42);
        var second = secondKernel.Persist();
        Assert.Equal(-128, result[513]);
        Assert.Equal(42, second[513]);
        second.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies comparer, stored-representative, collision, and no-op parity.</summary>
    [Fact]
    public void ComparerRepresentativesCollisionsAndNoOps_MatchPersistentMap()
    {
        var comparer = new ConstantHashOrdinalIgnoreCaseComparer();
        var storedKey = NewString("Alpha");
        var equivalentKey = NewString("ALPHA");
        var source = PersistentHashMap<string, string>.Create(comparer)
            .SetItem(storedKey, NewString("one"))
            .SetItem("Beta", "two")
            .SetItem("Gamma", "three");
        var kernel = source.CreateOwnerTokenTransientKernel();

        var version = kernel.VersionForDiagnostics;
        kernel.SetItem(equivalentKey, NewString("one"));
        Assert.Equal(version, kernel.VersionForDiagnostics);
        Assert.False(kernel.TryAdd(equivalentKey, "duplicate"));
        Assert.Equal(version, kernel.VersionForDiagnostics);

        kernel.SetItem(equivalentKey, "changed");
        Assert.True(kernel.Remove("BETA"));
        Assert.False(kernel.Remove("missing"));
        Assert.True(kernel.TryGetKey(equivalentKey, out var actualKey));
        Assert.Same(storedKey, actualKey);

        var result = kernel.Persist();
        Assert.Same(comparer, result.Comparer);
        Assert.Same(storedKey, result.First(pair => comparer.Equals(pair.Key, storedKey)).Key);
        Assert.Equal("changed", result[equivalentKey]);
        Assert.False(result.ContainsKey("Beta"));
        Assert.Equal("three", result["GAMMA"]);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Compares a mixed equal-hash history with the persistent-map oracle.</summary>
    [Fact]
    public void MixedCollisionHistory_MatchesPersistentOracleAndCanonicalCounts()
    {
        var comparer = new ConstantHashIntComparer();
        var source = PersistentHashMap<int, int>.Create(comparer);
        for (var index = 0; index < 64; index++)
            source = source.SetItem(index, index);

        var oracle = source;
        var kernel = source.CreateOwnerTokenTransientKernel();
        var random = new Random(0x51a2);
        for (var operation = 0; operation < 600; operation++)
        {
            var key = random.Next(96);
            if (random.Next(4) == 0)
            {
                Assert.Equal(oracle.ContainsKey(key), kernel.Remove(key));
                oracle = oracle.Remove(key);
            }
            else
            {
                var value = random.Next();
                kernel.SetItem(key, value);
                oracle = oracle.SetItem(key, value);
            }
        }

        var result = kernel.Persist();
        Assert.Equal(oracle.ToArray(), result.ToArray());
        var diagnostics = result.ValidateCanonicalityForDiagnostics();
        Assert.Equal(result.Count, diagnostics.RecursiveEntryCount);
    }

    /// <summary>Verifies callback failures preserve both the base map and active session.</summary>
    [Fact]
    public void CallbackFailures_LeaveSessionAndBaseUsable()
    {
        var comparer = new SwitchableThrowingComparer();
        var source = PersistentHashMap<string, ThrowingValue>.Create(comparer)
            .SetItem("alpha", new ThrowingValue("one"))
            .SetItem("beta", new ThrowingValue("two"));
        var expectedSource = source.ToArray();
        var kernel = source.CreateOwnerTokenTransientKernel();
        var expectedKernel = kernel.ToArrayForDiagnostics();
        var root = kernel.RootIdentityForDiagnostics;
        var version = kernel.VersionForDiagnostics;

        comparer.ThrowHash = true;
        Assert.Throws<TestFailureException>(() => kernel.SetItem("alpha", new ThrowingValue("replacement")));
        comparer.ThrowHash = false;
        Assert.Same(root, kernel.RootIdentityForDiagnostics);
        Assert.Equal(version, kernel.VersionForDiagnostics);
        Assert.Equal(expectedKernel, kernel.ToArrayForDiagnostics());

        comparer.ThrowEquals = true;
        Assert.Throws<TestFailureException>(() => kernel.Remove("alpha"));
        comparer.ThrowEquals = false;
        Assert.Same(root, kernel.RootIdentityForDiagnostics);
        Assert.Equal(expectedKernel, kernel.ToArrayForDiagnostics());

        var stored = kernel.ToArrayForDiagnostics().Single(pair => pair.Key == "alpha").Value;
        stored.ThrowEquals = true;
        Assert.Throws<TestFailureException>(() => kernel.SetItem("alpha", new ThrowingValue("one")));
        stored.ThrowEquals = false;
        Assert.Same(root, kernel.RootIdentityForDiagnostics);
        Assert.Equal(expectedKernel, kernel.ToArrayForDiagnostics());
        Assert.Equal(expectedSource, source.ToArray());

        kernel.SetItem("alpha", new ThrowingValue("replacement"));
        Assert.Equal("replacement", kernel.TryGetValue("alpha", out var replacement) ? replacement.Text : null);
    }

    /// <summary>Verifies each preparation failure boundary is operation-atomic.</summary>
    [Fact]
    public void EveryPreparationFailpoint_LeavesRootContentVersionAndCountersUnchanged()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 256).Select(index => KeyValuePair.Create(index, index)));
        var observedFailures = 0;

        for (var failAt = 0; failAt < 64; failAt++)
        {
            var kernel = source.CreateOwnerTokenTransientKernel();
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

        Assert.True(observedFailures >= 3);
    }

    /// <summary>Verifies a failed prepared edit does not mutate an already-owned array.</summary>
    [Fact]
    public void FailedPreparedInPlaceWrite_DoesNotChangeOwnedArray()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 512).Select(index => KeyValuePair.Create(index, index)));
        var kernel = source.CreateOwnerTokenTransientKernel();
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
        Assert.True(kernel.TryGetValue(257, out var value));
        Assert.Equal(-3, value);
    }

    /// <summary>Verifies publication failures leave a dirty session active and reusable.</summary>
    [Fact]
    public void PublicationFailures_LeaveThePreparedSessionActive()
    {
        var source = PersistentHashMap<int, int>.Empty.SetItem(1, 1);
        var kernel = source.CreateOwnerTokenTransientKernel();
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
        }

        kernel.FailureInjector = null;
        var result = kernel.Persist();
        Assert.Equal(2, result[1]);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies retained-size diagnostics charge node ownership metadata and tokens.</summary>
    [Fact]
    public void LayoutDiagnostics_ChargeOrdinaryNodeFieldsAndPublishedTokens()
    {
        var ordinary = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(index => KeyValuePair.Create(index, index)));
        var ordinaryDiagnostics = ordinary.GetStructureDiagnostics();

        Assert.Equal(0, ordinaryDiagnostics.OwnerTaggedNodeCount);
        Assert.Equal(0, ordinaryDiagnostics.OwnerTaggedArrayCount);
        Assert.Equal(0, ordinaryDiagnostics.OwnerTokenCount);
        Assert.Equal(
            (long)(ordinaryDiagnostics.BranchNodeCount + ordinaryDiagnostics.CollisionNodeCount) * IntPtr.Size,
            ordinaryDiagnostics.EstimatedOwnerMetadataBytes);

        var kernel = ordinary.CreateOwnerTokenTransientKernel();
        for (var index = 0; index < 64; index++)
            kernel.SetItem(index, -index - 1);
        var published = kernel.Persist();
        var publishedDiagnostics = published.GetStructureDiagnostics();

        Assert.True(publishedDiagnostics.OwnerTaggedNodeCount > 0);
        Assert.True(publishedDiagnostics.OwnerTaggedArrayCount > 0);
        Assert.Equal(1, publishedDiagnostics.OwnerTokenCount);
        Assert.True(publishedDiagnostics.EstimatedOwnerTokenBytes > 0);
        Assert.True(publishedDiagnostics.EstimatedRetainedBytes > ordinaryDiagnostics.EstimatedRetainedBytes);
    }

    private static string NewString(string value) => new(value.ToCharArray());

    private sealed class ConstantHashOrdinalIgnoreCaseComparer : IEqualityComparer<string>
    {
        public bool Equals(string? left, string? right) =>
            StringComparer.OrdinalIgnoreCase.Equals(left, right);

        public int GetHashCode(string value) => 0;
    }

    private sealed class ConstantHashIntComparer : IEqualityComparer<int>
    {
        public bool Equals(int left, int right) => left == right;

        public int GetHashCode(int value) => 0;
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
