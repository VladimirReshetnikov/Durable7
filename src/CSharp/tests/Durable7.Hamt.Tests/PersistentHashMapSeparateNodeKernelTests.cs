// Tests for the persistent hash map separate node kernel.

using Xunit;

namespace Durable7.Hamt.Tests;

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

    /// <summary>Verifies one changed edit stays on the ordinary path and needs only its map wrapper.</summary>
    [Fact]
    public void OneChangedEdit_DefersOwnershipAndPublishesTheOrdinaryResultDirectly()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(index => KeyValuePair.Create(index, index)));
        var expected = source.SetItem(513, -513);
        var kernel = source.CreateSeparateNodeTransientKernel();

        kernel.SetItem(513, -513);

        Assert.IsType<PersistentHashMap<int, int>.BitmapIndexedNode>(
            kernel.RootIdentityForDiagnostics);
        var deferred = Assert.IsType<PersistentHashMap<int, int>>(
            kernel.DeferredPersistentIdentityForDiagnostics);
        var result = kernel.Persist();
        var counters = kernel.GetCountersForDiagnostics();
        var structure = result.GetStructureDiagnostics();

        Assert.True(result.MapEquals(expected));
        Assert.Same(deferred, result);
        Assert.Equal(1, counters.DeferredPersistentMutationCount);
        Assert.Equal(0, counters.EditablePromotionCount);
        Assert.Equal(0, counters.CommitPlanAllocationCount);
        Assert.Equal(1, counters.PreparedMutationCount);
        Assert.Equal(0, counters.AllocatedNodeCount);
        Assert.Equal(0, counters.AllocatedArrayCount);
        Assert.Equal(1, counters.PersistentWrapperAllocationCount);
        Assert.Equal(1, counters.DeferredPersistentWrapperAllocationCount);
        Assert.Equal(1, counters.PublicationCount);
        Assert.False(kernel.TokenIsAllocatedForDiagnostics);
        Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);
        Assert.Equal(0, structure.SeparateBranchNodeCount);
        Assert.Equal(0, structure.SeparateCollisionNodeCount);
        Assert.Equal(0, structure.OwnerTokenCount);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies repeated edits to a leaf stay ordinary and do not report a promotion.</summary>
    [Fact]
    public void LeafOnlyReuse_RemainsOrdinaryAndDoesNotReportEditablePromotion()
    {
        var source = PersistentHashMap<int, int>.Empty.SetItem(1, 10);
        var kernel = source.CreateSeparateNodeTransientKernel();

        kernel.SetItem(1, 20);
        kernel.SetItem(1, 30);
        var result = kernel.Persist();
        var counters = kernel.GetCountersForDiagnostics();
        var structure = result.GetStructureDiagnostics();

        Assert.Equal(30, result[1]);
        Assert.Equal(10, source[1]);
        Assert.Equal(1, counters.DeferredPersistentMutationCount);
        Assert.Equal(0, counters.EditablePromotionCount);
        Assert.False(kernel.TokenIsAllocatedForDiagnostics);
        Assert.Equal(0, structure.SeparateBranchNodeCount);
        Assert.Equal(0, structure.SeparateCollisionNodeCount);
        Assert.Equal(0, structure.OwnerTokenCount);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies production mode and counter-enabled mode execute identical lifecycle semantics.</summary>
    [Fact]
    public void DiagnosticsDisabledHistory_MatchesCounterEnabledHistory()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 256).Select(index => KeyValuePair.Create(index, index)));
        var diagnostic = source.CreateSeparateNodeTransientKernel();
        var production = source.CreateSeparateNodeTransientKernel(enableDiagnostics: false);

        ApplyHistory(diagnostic);
        ApplyHistory(production);

        var diagnosticResult = diagnostic.Persist();
        var productionResult = production.Persist();
        Assert.True(diagnosticResult.MapEquals(productionResult));
        Assert.Equal(diagnosticResult.ToArray(), productionResult.ToArray());
        Assert.Equal(
            diagnosticResult.GetStructureDiagnostics(),
            productionResult.GetStructureDiagnostics());
        Assert.True(diagnostic.GetCountersForDiagnostics().PreparedMutationCount >= 4);
        diagnosticResult.ValidateCanonicalityForDiagnostics();
        productionResult.ValidateCanonicalityForDiagnostics();

        static void ApplyHistory(PersistentHashMap<int, int>.Transient kernel)
        {
            kernel.SetItem(17, -17);
            Assert.True(kernel.TryAdd(1_024, 1_024));
            Assert.True(kernel.Remove(3));
            kernel.SetItem(17, -18);
            Assert.False(kernel.TryAdd(17, 0));
            Assert.False(kernel.Remove(-1));
            kernel.Clear();
            Assert.True(kernel.TryAdd(2_048, 2_048));
        }
    }

    /// <summary>Pins every first-operation fast path and no-op under both diagnostic modes.</summary>
    [Fact]
    public void FirstOperationMatrix_DefersChangedEditsAndKeepsNoOpsPristine()
    {
        var comparer = new ConstantHashIntComparer();
        var source = PersistentHashMap<int, int>.Create(comparer)
            .SetItem(1, 10)
            .SetItem(2, 20)
            .SetItem(3, 30);
        var empty = PersistentHashMap<int, int>.Create(comparer);

        foreach (var diagnosticsEnabled in new[] { false, true })
        {
            AssertChanged(
                source,
                diagnosticsEnabled,
                kernel => kernel.SetItem(2, -20),
                map => map.SetItem(2, -20));
            AssertChanged(
                source,
                diagnosticsEnabled,
                kernel => Assert.True(kernel.TryAdd(4, 40)),
                map => map.SetItem(4, 40));
            AssertChanged(
                source,
                diagnosticsEnabled,
                kernel => Assert.True(kernel.Remove(2)),
                map => map.Remove(2));
            AssertChanged(
                source,
                diagnosticsEnabled,
                kernel => kernel.Clear(),
                map => map.Clear());

            AssertNoOp(source, diagnosticsEnabled, kernel => kernel.SetItem(2, 20));
            AssertNoOp(source, diagnosticsEnabled, kernel => Assert.False(kernel.TryAdd(2, -1)));
            AssertNoOp(source, diagnosticsEnabled, kernel => Assert.False(kernel.Remove(99)));
            AssertNoOp(empty, diagnosticsEnabled, kernel => kernel.Clear());
        }

        static void AssertChanged(
            PersistentHashMap<int, int> source,
            bool diagnosticsEnabled,
            Action<PersistentHashMap<int, int>.Transient> mutation,
            Func<PersistentHashMap<int, int>, PersistentHashMap<int, int>> persistentMutation)
        {
            var expected = persistentMutation(source);
            var kernel = source.CreateSeparateNodeTransientKernel(
                enableDiagnostics: diagnosticsEnabled);
            mutation(kernel);

            Assert.Equal(1L, kernel.VersionForDiagnostics);
            Assert.False(kernel.TokenIsAllocatedForDiagnostics);
            Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);
            var deferred = Assert.IsType<PersistentHashMap<int, int>>(
                kernel.DeferredPersistentIdentityForDiagnostics);
            var result = kernel.Persist();

            Assert.Same(deferred, result);
            Assert.True(result.MapEquals(expected));
            Assert.Same(source.Comparer, result.Comparer);
            var structure = result.GetStructureDiagnostics();
            Assert.Equal(0, structure.SeparateBranchNodeCount);
            Assert.Equal(0, structure.SeparateCollisionNodeCount);
            Assert.Equal(0, structure.OwnerTokenCount);
            if (diagnosticsEnabled)
            {
                var counters = kernel.GetCountersForDiagnostics();
                Assert.Equal(1, counters.DeferredPersistentMutationCount);
                Assert.Equal(1, counters.PreparedMutationCount);
                Assert.Equal(0, counters.EditablePromotionCount);
            }
            else
            {
                Assert.Throws<InvalidOperationException>(
                    () => _ = kernel.GetCountersForDiagnostics());
            }
        }

        static void AssertNoOp(
            PersistentHashMap<int, int> source,
            bool diagnosticsEnabled,
            Action<PersistentHashMap<int, int>.Transient> mutation)
        {
            var kernel = source.CreateSeparateNodeTransientKernel(
                enableDiagnostics: diagnosticsEnabled);
            var root = kernel.RootIdentityForDiagnostics;
            mutation(kernel);

            Assert.Equal(0L, kernel.VersionForDiagnostics);
            Assert.Null(kernel.DeferredPersistentIdentityForDiagnostics);
            Assert.Same(root, kernel.RootIdentityForDiagnostics);
            Assert.False(kernel.TokenIsAllocatedForDiagnostics);
            Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);
            Assert.Same(source, kernel.Persist());
        }
    }

    /// <summary>Verifies dirty empty publication preserves canonical/default and custom policies.</summary>
    [Fact]
    public void DirtyEmptyPublication_CanonicalizesDefaultAndPreservesCustomComparer()
    {
        var defaultAfterClear = PersistentHashMap<int, int>.Empty.SetItem(1, 1)
            .CreateSeparateNodeTransientKernel();
        defaultAfterClear.Clear();
        var clearedDefault = defaultAfterClear.Persist();
        Assert.Same(PersistentHashMap<int, int>.Empty, clearedDefault);
        Assert.Equal(0, defaultAfterClear.GetCountersForDiagnostics().PersistentWrapperAllocationCount);

        var defaultAfterRemove = PersistentHashMap<int, int>.Empty.SetItem(1, 1)
            .CreateSeparateNodeTransientKernel();
        Assert.True(defaultAfterRemove.Remove(1));
        Assert.Same(PersistentHashMap<int, int>.Empty, defaultAfterRemove.Persist());
        Assert.Equal(0, defaultAfterRemove.GetCountersForDiagnostics().PersistentWrapperAllocationCount);

        var comparer = new ConstantHashIntComparer();
        var customAfterClear = PersistentHashMap<int, int>.Create(comparer).SetItem(1, 1)
            .CreateSeparateNodeTransientKernel();
        customAfterClear.Clear();
        var clearedCustom = customAfterClear.Persist();
        Assert.Empty(clearedCustom);
        Assert.Same(comparer, clearedCustom.Comparer);
        Assert.NotSame(PersistentHashMap<int, int>.Empty, clearedCustom);
        Assert.Equal(1, customAfterClear.GetCountersForDiagnostics().PersistentWrapperAllocationCount);

        var customAfterRemove = PersistentHashMap<int, int>.Create(comparer).SetItem(1, 1)
            .CreateSeparateNodeTransientKernel();
        Assert.True(customAfterRemove.Remove(1));
        var removedCustom = customAfterRemove.Persist();
        Assert.Empty(removedCustom);
        Assert.Same(comparer, removedCustom.Comparer);
        Assert.NotSame(PersistentHashMap<int, int>.Empty, removedCustom);
        Assert.Equal(1, customAfterRemove.GetCountersForDiagnostics().PersistentWrapperAllocationCount);
    }

    /// <summary>Verifies canonical-empty publication has no synthetic allocation failpoint.</summary>
    [Fact]
    public void DefaultEmptyPublication_SkipsAllocationBoundaryButKeepsPreparedBoundaryAtomic()
    {
        var kernel = PersistentHashMap<int, int>.Empty.SetItem(1, 1)
            .CreateSeparateNodeTransientKernel();
        kernel.Clear();
        var sawAllocationBoundary = false;
        kernel.FailureInjector = point =>
        {
            if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforePublicationAllocation)
                sawAllocationBoundary = true;
            if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.PublicationPrepared)
                throw new TestFailureException();
        };

        Assert.Throws<TestFailureException>(kernel.Persist);
        Assert.False(sawAllocationBoundary);
        Assert.True(kernel.IsActiveForDiagnostics);
        Assert.True(kernel.TokenIsActiveForDiagnostics);
        Assert.Empty(kernel);

        kernel.FailureInjector = null;
        Assert.Same(PersistentHashMap<int, int>.Empty, kernel.Persist());
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
        Assert.Equal(1, counters.DeferredPersistentMutationCount);
        Assert.Equal(1, counters.EditablePromotionCount);
        Assert.Equal(1, counters.CommitPlanAllocationCount);
        Assert.True(counters.InPlaceNodeMutationCount >= 126);
        Assert.True(counters.InPlaceArrayWriteCount >= 126);
        Assert.Equal(1, counters.AdoptionCount);
        Assert.Equal(0, counters.AdoptionNodeVisits);
        Assert.Equal(1, counters.PublicationCount);
        Assert.Equal(0, counters.PublicationNodeVisits);
        Assert.Equal(2, counters.PersistentWrapperAllocationCount);
        Assert.Equal(1, counters.DeferredPersistentWrapperAllocationCount);
        Assert.True(kernel.TokenIsAllocatedForDiagnostics);
        Assert.True(kernel.CommitPlanIsAllocatedForDiagnostics);
        Assert.Equal(0, sourceStructure.SeparateBranchNodeCount);
        Assert.Equal(0, sourceStructure.SeparateCollisionNodeCount);
        Assert.Equal(0, sourceStructure.EstimatedOwnerMetadataBytes);
        Assert.Equal(0, sourceStructure.EstimatedSeparateNodeMetadataBytes);
        Assert.True(structure.SeparateBranchNodeCount > 0);
        Assert.Equal(0, structure.SeparateCollisionNodeCount);
        Assert.True(structure.EstimatedSeparateNodeMetadataBytes > 0);
        Assert.Equal(0, structure.EstimatedOwnerMetadataBytes);
        Assert.Equal(1, structure.OwnerTokenCount);
        Assert.Same(ordinaryRoot.Data, separateRoot.Data);
        Assert.NotSame(ordinaryRoot.Children, separateRoot.Children);
        Assert.False(separateRoot.DataOwned);
        Assert.True(separateRoot.ChildrenOwned);
        result.ValidateCanonicalityForDiagnostics();

        var secondKernel = result.CreateSeparateNodeTransientKernel();
        secondKernel.SetItem(513, 42);
        var second = secondKernel.Persist();
        Assert.Equal(-128, result[513]);
        Assert.Equal(42, second[513]);
        second.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Pins deferred promotion, lazy resources, rollback, and later owned-path reuse.</summary>
    [Fact]
    public void DeferredPromotion_AllocatesResourcesLazilyAndRollsThemBackOnFailure()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(index => KeyValuePair.Create(index, index)));
        var kernel = source.CreateSeparateNodeTransientKernel();

        kernel.SetItem(513, -1);
        var deferred = Assert.IsType<PersistentHashMap<int, int>>(
            kernel.DeferredPersistentIdentityForDiagnostics);
        Assert.IsType<PersistentHashMap<int, int>.BitmapIndexedNode>(
            kernel.RootIdentityForDiagnostics);
        Assert.False(kernel.TokenIsAllocatedForDiagnostics);
        Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);

        var deferredRoot = kernel.RootIdentityForDiagnostics;
        var deferredVersion = kernel.VersionForDiagnostics;
        var deferredCounters = kernel.GetCountersForDiagnostics();
        kernel.FailureInjector = point =>
        {
            if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeTokenAllocation)
                throw new TestFailureException();
        };

        Assert.Throws<TestFailureException>(() => kernel.SetItem(513, -2));
        Assert.Same(deferred, kernel.DeferredPersistentIdentityForDiagnostics);
        Assert.Same(deferredRoot, kernel.RootIdentityForDiagnostics);
        Assert.Equal(deferredVersion, kernel.VersionForDiagnostics);
        Assert.Equal(deferredCounters, kernel.GetCountersForDiagnostics());
        Assert.False(kernel.TokenIsAllocatedForDiagnostics);
        Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);
        Assert.Equal(-1, kernel.TryGetValue(513, out var afterTokenFailure) ? afterTokenFailure : 0);

        kernel.FailureInjector = null;
        kernel.SetItem(513, -2);
        Assert.Null(kernel.DeferredPersistentIdentityForDiagnostics);
        Assert.IsType<PersistentHashMap<int, int>.SeparateTransientBranchNode>(
            kernel.RootIdentityForDiagnostics);
        Assert.True(kernel.TokenIsAllocatedForDiagnostics);
        Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);
        Assert.Equal(1, kernel.GetCountersForDiagnostics().EditablePromotionCount);

        var editableRoot = kernel.RootIdentityForDiagnostics;
        var editableVersion = kernel.VersionForDiagnostics;
        var editableCounters = kernel.GetCountersForDiagnostics();
        kernel.FailureInjector = point =>
        {
            if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeCommitPlanAllocation)
                throw new TestFailureException();
        };

        Assert.Throws<TestFailureException>(() => kernel.SetItem(513, -3));
        Assert.Same(editableRoot, kernel.RootIdentityForDiagnostics);
        Assert.Equal(editableVersion, kernel.VersionForDiagnostics);
        Assert.Equal(editableCounters, kernel.GetCountersForDiagnostics());
        Assert.True(kernel.TokenIsAllocatedForDiagnostics);
        Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);
        Assert.Equal(-2, kernel.TryGetValue(513, out var afterPlanFailure) ? afterPlanFailure : 0);

        kernel.FailureInjector = null;
        kernel.SetItem(513, -3);
        Assert.True(kernel.CommitPlanIsAllocatedForDiagnostics);
        var counters = kernel.GetCountersForDiagnostics();
        Assert.Equal(1, counters.CommitPlanAllocationCount);
        Assert.True(counters.InPlaceNodeMutationCount > 0);

        var result = kernel.Persist();
        Assert.Equal(-3, result[513]);
        Assert.Equal(513, source[513]);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies a data edit owns only data while retaining an independently shared child array.</summary>
    [Fact]
    public void DataEdit_OwnsDataButRetainsSharedChildren()
    {
        var comparer = new ExplicitHashComparer();
        var first = new ExplicitHashKey(0, 0);
        var second = new ExplicitHashKey(1, 32);
        var direct = new ExplicitHashKey(2, 1);
        var source = PersistentHashMap<ExplicitHashKey, int>.Create(comparer)
            .SetItem(first, 10)
            .SetItem(second, 20)
            .SetItem(direct, 30);
        var ordinaryRoot = Assert.IsType<PersistentHashMap<ExplicitHashKey, int>.BitmapIndexedNode>(
            source.RootForTesting);
        var kernel = source.CreateSeparateNodeTransientKernel(deferOwnershipUntilReuse: false);

        kernel.SetItem(direct, -30);
        var result = kernel.Persist();
        var separateRoot = Assert.IsType<
            PersistentHashMap<ExplicitHashKey, int>.SeparateTransientBranchNode>(
                result.RootForTesting);

        Assert.Equal(-30, result[direct]);
        Assert.Equal(30, source[direct]);
        Assert.NotSame(ordinaryRoot.Data, separateRoot.Data);
        Assert.Same(ordinaryRoot.Children, separateRoot.Children);
        Assert.True(separateRoot.DataOwned);
        Assert.False(separateRoot.ChildrenOwned);
        result.ValidateCanonicalityForDiagnostics();
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
        var kernel = source.CreateSeparateNodeTransientKernel(deferOwnershipUntilReuse: false);

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
        Assert.Equal(0, structure.EstimatedOwnerMetadataBytes);
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

    /// <summary>Exercises every persistent consumer over a published separate collision root.</summary>
    [Fact]
    public void PublishedSeparateCollision_SupportsPersistentEditsDiffEqualityAndAlgebra()
    {
        var comparer = new ConstantHashIntComparer();
        var source = PersistentHashMap<int, int>.Create(comparer);
        for (var index = 0; index < 6; index++)
            source = source.SetItem(index, index * 10);

        var oracle = source.SetItem(1, -10).SetItem(6, 60).Remove(2);
        var kernel = source.CreateSeparateNodeTransientKernel();
        kernel.SetItem(1, -10);
        kernel.SetItem(6, 60);
        Assert.True(kernel.Remove(2));
        var mixed = kernel.Persist();

        Assert.IsType<PersistentHashMap<int, int>.SeparateTransientCollisionNode>(
            mixed.RootForTesting);
        Assert.True(mixed.MapEquals(oracle));
        Assert.True(oracle.MapEquals(mixed));
        Assert.Equal(oracle.Diff(source).ToArray(), mixed.Diff(source).ToArray());
        Assert.Equal(source.Diff(oracle).ToArray(), source.Diff(mixed).ToArray());

        var persistentlySet = mixed.SetItem(3, -30);
        var persistentlySetOracle = oracle.SetItem(3, -30);
        Assert.True(persistentlySet.MapEquals(persistentlySetOracle));
        Assert.True(persistentlySetOracle.MapEquals(persistentlySet));
        persistentlySet.ValidateCanonicalityForDiagnostics();

        var persistentlyRemoved = mixed.Remove(4);
        var persistentlyRemovedOracle = oracle.Remove(4);
        Assert.True(persistentlyRemoved.MapEquals(persistentlyRemovedOracle));
        Assert.True(persistentlyRemovedOracle.MapEquals(persistentlyRemoved));
        Assert.Equal(-10, mixed[1]);
        Assert.Equal(40, mixed[4]);
        persistentlyRemoved.ValidateCanonicalityForDiagnostics();

        var right = PersistentHashMap<int, int>.Create(comparer);
        for (var index = 4; index < 10; index++)
            right = right.SetItem(index, index * 100);

        var union = mixed.Union(right);
        var intersection = mixed.Intersect(right);
        var except = mixed.Except(right);
        var symmetric = mixed.SymmetricExcept(right);
        Assert.True(union.MapEquals(oracle.Union(right)));
        Assert.True(intersection.MapEquals(oracle.Intersect(right)));
        Assert.True(except.MapEquals(oracle.Except(right)));
        Assert.True(symmetric.MapEquals(oracle.SymmetricExcept(right)));
        Assert.True(right.Union(mixed).MapEquals(right.Union(oracle)));
        Assert.True(right.Intersect(mixed).MapEquals(right.Intersect(oracle)));
        Assert.True(right.Except(mixed).MapEquals(right.Except(oracle)));
        Assert.True(right.SymmetricExcept(mixed).MapEquals(right.SymmetricExcept(oracle)));
        Assert.Equal(right[4], union[4]);
        Assert.Equal(mixed[4], intersection[4]);
        Assert.Same(mixed, mixed.Union(mixed));
        Assert.Same(mixed, mixed.Intersect(mixed));
        Assert.Empty(mixed.Except(mixed));
        Assert.Empty(mixed.SymmetricExcept(mixed));

        union.ValidateCanonicalityForDiagnostics();
        intersection.ValidateCanonicalityForDiagnostics();
        except.ValidateCanonicalityForDiagnostics();
        symmetric.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies removal promotes a sole non-leaf hash child through its parent.</summary>
    [Fact]
    public void Remove_PromotesCollisionWhenParentWouldContainOnlyThatHashChild()
    {
        var comparer = new ExplicitHashComparer();
        var first = new ExplicitHashKey(0, 0);
        var second = new ExplicitHashKey(1, 0);
        var split = new ExplicitHashKey(2, 32);
        var source = PersistentHashMap<ExplicitHashKey, int>.Create(comparer)
            .SetItem(first, 10)
            .SetItem(second, 20)
            .SetItem(split, 30);
        var kernel = source.CreateSeparateNodeTransientKernel();

        Assert.True(kernel.Remove(split));
        var result = kernel.Persist();

        Assert.Equal(2, result.Count);
        Assert.Equal(10, result[first]);
        Assert.Equal(20, result[second]);
        Assert.False(result.ContainsKey(split));
        Assert.IsAssignableFrom<PersistentHashMap<ExplicitHashKey, int>.HashNode>(
            result.RootForTesting);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Compares a randomized equal-full-hash history with the persistent-map oracle.</summary>
    [Fact]
    public void MixedCollisionHistory_MatchesPersistentOracleAndCanonicalCounts()
    {
        var comparer = new ConstantHashIntComparer();
        var source = PersistentHashMap<int, int>.Create(comparer);
        for (var index = 0; index < 64; index++)
            source = source.SetItem(index, index);

        var oracle = source;
        var kernel = source.CreateSeparateNodeTransientKernel();
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
        Assert.True(result.GetStructureDiagnostics().SeparateCollisionNodeCount > 0);
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

    /// <summary>Exercises lookup across both possible ordinary/separate branch transitions.</summary>
    [Fact]
    public void MixedBranchLookup_AlternatesExactNodeKindsUntilTheTerminal()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(index => KeyValuePair.Create(index, index)));
        var kernel = source.CreateSeparateNodeTransientKernel(deferOwnershipUntilReuse: false);
        kernel.SetItem(513, -513);
        var separateRootMap = kernel.Persist();
        var separateRoot = Assert.IsType<PersistentHashMap<int, int>.SeparateTransientBranchNode>(
            separateRootMap.RootForTesting);

        Assert.Contains(
            separateRoot.Children,
            child => child is PersistentHashMap<int, int>.BitmapIndexedNode);
        Assert.Equal(100, separateRootMap[100]);
        Assert.True(separateRootMap.CountNodeVisitsForDiagnostics(100) > 1);

        var ordinaryRootMap = separateRootMap.SetItem(100, -100);
        var ordinaryRoot = Assert.IsType<PersistentHashMap<int, int>.BitmapIndexedNode>(
            ordinaryRootMap.RootForTesting);
        Assert.Contains(
            ordinaryRoot.Children,
            child => child is PersistentHashMap<int, int>.SeparateTransientBranchNode);
        Assert.Equal(-513, ordinaryRootMap[513]);
        Assert.True(ordinaryRootMap.CountNodeVisitsForDiagnostics(513) > 1);
        ordinaryRootMap.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies diff and all structural algebra paths over published mixed graphs.</summary>
    [Fact]
    public void PublishedMixedGraph_SupportsDiffAndStructuralAlgebra()
    {
        var comparer = new ExplicitHashComparer();
        var source = PersistentHashMap<ExplicitHashKey, int>.Create(comparer);
        for (var index = 0; index < 256; index++)
            source = source.SetItem(new ExplicitHashKey(index, ClusteredHash(index)), index);

        var oracle = source;
        var kernel = source.CreateSeparateNodeTransientKernel();
        for (var index = 0; index < 96; index++)
        {
            var key = new ExplicitHashKey(index, ClusteredHash(index));
            kernel.SetItem(key, -index - 1);
            oracle = oracle.SetItem(key, -index - 1);
        }
        for (var index = 256; index < 288; index++)
        {
            var key = new ExplicitHashKey(index, ClusteredHash(index));
            kernel.SetItem(key, -index - 1);
            oracle = oracle.SetItem(key, -index - 1);
        }
        var mixed = kernel.Persist();

        var right = PersistentHashMap<ExplicitHashKey, int>.Create(comparer);
        for (var index = 64; index < 320; index++)
            right = right.SetItem(new ExplicitHashKey(index, ClusteredHash(index)), index * 17);

        Assert.Equal(oracle.Diff(right).ToArray(), mixed.Diff(right).ToArray());
        Assert.Equal(right.Diff(oracle).ToArray(), right.Diff(mixed).ToArray());

        var union = mixed.Union(right);
        var intersection = mixed.Intersect(right);
        var except = mixed.Except(right);
        var symmetric = mixed.SymmetricExcept(right);
        Assert.True(union.MapEquals(oracle.Union(right)));
        Assert.True(intersection.MapEquals(oracle.Intersect(right)));
        Assert.True(except.MapEquals(oracle.Except(right)));
        Assert.True(symmetric.MapEquals(oracle.SymmetricExcept(right)));

        var overlap = new ExplicitHashKey(80, ClusteredHash(80));
        Assert.Equal(right[overlap], union[overlap]);
        Assert.Equal(mixed[overlap], intersection[overlap]);
        Assert.Same(mixed, mixed.Union(mixed));
        Assert.Same(mixed, mixed.Intersect(mixed));
        Assert.Empty(mixed.Except(mixed));
        Assert.Empty(mixed.SymmetricExcept(mixed));

        union.ValidateCanonicalityForDiagnostics();
        intersection.ValidateCanonicalityForDiagnostics();
        except.ValidateCanonicalityForDiagnostics();
        symmetric.ValidateCanonicalityForDiagnostics();
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
            var kernel = source.CreateSeparateNodeTransientKernel(deferOwnershipUntilReuse: false);
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
                Assert.Equal(0L, kernel.VersionForDiagnostics);
                Assert.Equal(before, kernel.GetCountersForDiagnostics());
                Assert.Equal(expected, kernel.ToArrayForDiagnostics());
                Assert.Equal(129, source[129]);
            }
        }

        // The separate hierarchy prepares ownership copies for independently shared data and child
        // arrays, so this path must expose several deterministic allocation boundaries.
        Assert.True(observedFailures >= 4);
    }

    /// <summary>Verifies the deferred first-edit commit boundary is failure-atomic for every verb.</summary>
    [Fact]
    public void DeferredFirstMutationPreparedFailure_LeavesTheSessionPristine()
    {
        var comparer = new ConstantHashIntComparer();
        var source = PersistentHashMap<int, int>.Create(comparer)
            .SetItem(1, 10)
            .SetItem(2, 20)
            .SetItem(3, 30);

        AssertAtomic(kernel => kernel.SetItem(2, -20));
        AssertAtomic(kernel => Assert.True(kernel.TryAdd(4, 40)));
        AssertAtomic(kernel => Assert.True(kernel.Remove(2)));
        AssertAtomic(kernel => kernel.Clear());

        void AssertAtomic(Action<PersistentHashMap<int, int>.Transient> mutation)
        {
            var kernel = source.CreateSeparateNodeTransientKernel();
            var root = kernel.RootIdentityForDiagnostics;
            var entries = kernel.ToArrayForDiagnostics();
            var counters = kernel.GetCountersForDiagnostics();
            kernel.FailureInjector = point =>
            {
                if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.MutationPrepared)
                    throw new TestFailureException();
            };

            Assert.Throws<TestFailureException>(() => mutation(kernel));
            kernel.FailureInjector = null;

            Assert.Same(root, kernel.RootIdentityForDiagnostics);
            Assert.Equal(0L, kernel.VersionForDiagnostics);
            Assert.Null(kernel.DeferredPersistentIdentityForDiagnostics);
            Assert.False(kernel.TokenIsAllocatedForDiagnostics);
            Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);
            Assert.Equal(counters, kernel.GetCountersForDiagnostics());
            Assert.Equal(entries, kernel.ToArrayForDiagnostics());
            Assert.Same(source, kernel.Persist());
        }
    }

    /// <summary>Verifies every mutation failpoint is reached by a representative owner-free shape.</summary>
    [Fact]
    public void MutationFailpoints_AreIndividuallyFailureAtomicAcrossNodeShapes()
    {
        var branched = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 256).Select(index => KeyValuePair.Create(index, index)));
        var collision = PersistentHashMap<int, int>.Create(new ConstantHashIntComparer())
            .SetItem(0, 0)
            .SetItem(1, 1);
        var removableCollision = collision.SetItem(2, 2);
        var explicitComparer = new ExplicitHashComparer();
        var collidingFirst = new ExplicitHashKey(0, 0);
        var collidingSecond = new ExplicitHashKey(1, 0);
        var split = new ExplicitHashKey(2, 32);
        var contracting = PersistentHashMap<ExplicitHashKey, int>.Create(explicitComparer)
            .SetItem(collidingFirst, 0)
            .SetItem(collidingSecond, 1)
            .SetItem(split, 2);

        AssertMutationFailpointAtomic(
            branched,
            kernel => kernel.SetItem(129, -1),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeTokenAllocation);
        AssertMutationFailpointAtomic(
            PersistentHashMap<int, int>.Empty,
            kernel => kernel.SetItem(0, 1),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeNodeAllocation);
        AssertMutationFailpointAtomic(
            branched,
            kernel => kernel.SetItem(129, -1),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeDataArrayAllocation);
        AssertMutationFailpointAtomic(
            branched,
            kernel => kernel.SetItem(129, -1),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeChildArrayAllocation);
        AssertMutationFailpointAtomic(
            collision,
            kernel => kernel.SetItem(0, -1),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeCollisionArrayAllocation);
        AssertMutationFailpointAtomic(
            collision,
            kernel => kernel.SetItem(0, -1),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeNodeAllocation);
        AssertMutationFailpointAtomic(
            removableCollision,
            kernel => Assert.True(kernel.Remove(1)),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeCollisionArrayAllocation);
        AssertMutationFailpointAtomic(
            removableCollision,
            kernel => Assert.True(kernel.Remove(1)),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforeNodeAllocation);
        AssertMutationFailpointAtomic(
            removableCollision,
            kernel => Assert.True(kernel.Remove(1)),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.MutationPrepared);
        AssertMutationFailpointAtomic(
            branched,
            kernel => kernel.SetItem(129, -1),
            PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.MutationPrepared);
        AssertMutationFailpointAtomic(
            contracting,
            kernel => Assert.True(kernel.Remove(split)),
            PersistentHashMap<ExplicitHashKey, int>.OwnerTokenKernelFailurePoint.MutationPrepared);
    }

    /// <summary>Verifies a failed second edit cannot partly mutate an already-owned separate node.</summary>
    [Fact]
    public void FailedPreparedInPlaceWrite_DoesNotChangeOwnedStorage()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 512).Select(index => KeyValuePair.Create(index, index)));
        var kernel = source.CreateSeparateNodeTransientKernel(deferOwnershipUntilReuse: false);
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

    /// <summary>Verifies MutationPrepared protects an already-owned collision array.</summary>
    [Fact]
    public void FailedPreparedInPlaceCollisionWrite_DoesNotChangeOwnedStorage()
    {
        var source = PersistentHashMap<int, int>.Create(new ConstantHashIntComparer())
            .SetItem(0, 0)
            .SetItem(1, 1)
            .SetItem(2, 2);
        var kernel = source.CreateSeparateNodeTransientKernel(deferOwnershipUntilReuse: false);
        kernel.SetItem(1, -1);
        Assert.IsType<PersistentHashMap<int, int>.SeparateTransientCollisionNode>(
            kernel.RootIdentityForDiagnostics);
        var root = kernel.RootIdentityForDiagnostics;
        var expected = kernel.ToArrayForDiagnostics();
        var version = kernel.VersionForDiagnostics;
        var counters = kernel.GetCountersForDiagnostics();
        kernel.FailureInjector = point =>
        {
            if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.MutationPrepared)
                throw new TestFailureException();
        };

        Assert.Throws<TestFailureException>(() => kernel.SetItem(1, -2));
        kernel.FailureInjector = null;

        Assert.Same(root, kernel.RootIdentityForDiagnostics);
        Assert.Equal(expected, kernel.ToArrayForDiagnostics());
        Assert.Equal(version, kernel.VersionForDiagnostics);
        Assert.Equal(counters, kernel.GetCountersForDiagnostics());
        Assert.Equal(1, source[1]);
        kernel.SetItem(1, -3);
        Assert.Equal(-3, kernel.TryGetValue(1, out var value) ? value : 0);
    }

    /// <summary>Verifies throwing hash/equality/value callbacks cannot mutate owned nodes.</summary>
    [Fact]
    public void CallbackFailures_LeaveOwnedSeparateGraphAndBaseUsable()
    {
        var comparer = new SwitchableThrowingComparer();
        var source = PersistentHashMap<string, ThrowingValue>.Create(comparer)
            .SetItem("alpha", new ThrowingValue("one"))
            .SetItem("beta", new ThrowingValue("two"));
        var kernel = source.CreateSeparateNodeTransientKernel(deferOwnershipUntilReuse: false);
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

    /// <summary>Verifies production fast-path callbacks fail before any deferred state is installed.</summary>
    [Fact]
    public void DiagnosticsDisabledFirstEditFailures_LeaveTheSessionPristine()
    {
        var comparer = new SwitchableThrowingComparer();
        var source = PersistentHashMap<string, int>.Create(comparer)
            .SetItem("alpha", 1)
            .SetItem("beta", 2);
        var kernel = source.CreateSeparateNodeTransientKernel(enableDiagnostics: false);
        var root = kernel.RootIdentityForDiagnostics;

        comparer.ThrowHash = true;
        Assert.Throws<TestFailureException>(() => kernel.SetItem("alpha", -1));
        comparer.ThrowHash = false;
        comparer.ThrowEquals = true;
        Assert.Throws<TestFailureException>(() => kernel.SetItem("alpha", -1));
        comparer.ThrowEquals = false;

        Assert.Same(root, kernel.RootIdentityForDiagnostics);
        Assert.Equal(0L, kernel.VersionForDiagnostics);
        Assert.Null(kernel.DeferredPersistentIdentityForDiagnostics);
        Assert.False(kernel.TokenIsAllocatedForDiagnostics);
        Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);
        Assert.Throws<InvalidOperationException>(
            () => _ = kernel.GetCountersForDiagnostics());
        Assert.Throws<InvalidOperationException>(() => kernel.FailureInjector = _ => { });

        kernel.SetItem("alpha", -1);
        var result = kernel.Persist();
        Assert.Equal(-1, result["alpha"]);
        Assert.Equal(1, source["alpha"]);
        result.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies deferred publication retries the exact cached ordinary wrapper.</summary>
    [Fact]
    public void DeferredPublicationFailure_RetainsExactWrapperAndSkipsAllocationBoundary()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 128).Select(index => KeyValuePair.Create(index, index)));
        var kernel = source.CreateSeparateNodeTransientKernel();
        kernel.SetItem(17, -17);
        var deferred = Assert.IsType<PersistentHashMap<int, int>>(
            kernel.DeferredPersistentIdentityForDiagnostics);
        var root = kernel.RootIdentityForDiagnostics;
        var version = kernel.VersionForDiagnostics;
        var counters = kernel.GetCountersForDiagnostics();
        var sawAllocationBoundary = false;
        kernel.FailureInjector = point =>
        {
            if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.BeforePublicationAllocation)
                sawAllocationBoundary = true;
            if (point == PersistentHashMap<int, int>.OwnerTokenKernelFailurePoint.PublicationPrepared)
                throw new TestFailureException();
        };

        Assert.Throws<TestFailureException>(kernel.Persist);
        Assert.False(sawAllocationBoundary);
        Assert.True(kernel.IsActiveForDiagnostics);
        Assert.Same(deferred, kernel.DeferredPersistentIdentityForDiagnostics);
        Assert.Same(root, kernel.RootIdentityForDiagnostics);
        Assert.Equal(version, kernel.VersionForDiagnostics);
        Assert.Equal(counters, kernel.GetCountersForDiagnostics());
        Assert.False(kernel.TokenIsAllocatedForDiagnostics);
        Assert.False(kernel.CommitPlanIsAllocatedForDiagnostics);

        kernel.FailureInjector = null;
        Assert.Same(deferred, kernel.Persist());
    }

    /// <summary>Verifies failed publication leaves a dirty separate-node session active.</summary>
    [Fact]
    public void PublicationFailures_LeavePreparedSessionActiveAndUnpublished()
    {
        var source = PersistentHashMap<int, int>.Empty.SetItem(1, 1);
        var kernel = source.CreateSeparateNodeTransientKernel(deferOwnershipUntilReuse: false);
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
        var counters = kernel.GetCountersForDiagnostics();
        Assert.Equal(2, result[1]);
        Assert.Equal(1, counters.AdoptionCount);
        Assert.Equal(0, counters.AdoptionNodeVisits);
        Assert.Equal(1, counters.PublicationCount);
        Assert.Equal(0, counters.PublicationNodeVisits);
        Assert.Equal(1, counters.PersistentWrapperAllocationCount);
        result.ValidateCanonicalityForDiagnostics();
    }

    private static void AssertMutationFailpointAtomic<TKey>(
        PersistentHashMap<TKey, int> source,
        Action<PersistentHashMap<TKey, int>.Transient> mutation,
        PersistentHashMap<TKey, int>.OwnerTokenKernelFailurePoint target)
        where TKey : notnull
    {
        var expectedSource = source.ToArray();
        var kernel = source.CreateSeparateNodeTransientKernel(deferOwnershipUntilReuse: false);
        var root = kernel.RootIdentityForDiagnostics;
        var expectedKernel = kernel.ToArrayForDiagnostics();
        var version = kernel.VersionForDiagnostics;
        var counters = kernel.GetCountersForDiagnostics();
        var reached = false;
        kernel.FailureInjector = point =>
        {
            if (point != target)
                return;
            reached = true;
            throw new TestFailureException();
        };

        Assert.Throws<TestFailureException>(() => mutation(kernel));
        kernel.FailureInjector = null;

        Assert.True(reached, $"The representative edit did not reach {target}.");
        Assert.True(kernel.IsActiveForDiagnostics);
        Assert.True(kernel.TokenIsActiveForDiagnostics);
        Assert.Same(root, kernel.RootIdentityForDiagnostics);
        Assert.Equal(version, kernel.VersionForDiagnostics);
        Assert.Equal(counters, kernel.GetCountersForDiagnostics());
        Assert.Equal(expectedKernel, kernel.ToArrayForDiagnostics());
        Assert.Equal(expectedSource, source.ToArray());
    }

    private static string NewString(string value) => new(value.ToCharArray());

    private static int ClusteredHash(int value) => unchecked((value << 15) | 0x2a5b);

    private readonly record struct ExplicitHashKey(int Value, int Hash);

    private sealed class ExplicitHashComparer : IEqualityComparer<ExplicitHashKey>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(ExplicitHashKey left, ExplicitHashKey right) => left.Value == right.Value;

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(ExplicitHashKey value) => value.Hash;
    }

    private sealed class ConstantHashOrdinalIgnoreCaseComparer : IEqualityComparer<string>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(string? left, string? right) =>
            StringComparer.OrdinalIgnoreCase.Equals(left, right);

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(string value) => 0;
    }

    private sealed class ConstantHashIntComparer : IEqualityComparer<int>
    {
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(int left, int right) => left == right;

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(int value) => 0;
    }

    private sealed class SwitchableThrowingComparer : IEqualityComparer<string>
    {
        /// <summary>Throws the hash.</summary>
        internal bool ThrowHash { get; set; }

        /// <summary>Throws the equals.</summary>
        internal bool ThrowEquals { get; set; }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(string? left, string? right)
        {
            if (ThrowEquals)
                throw new TestFailureException();
            return StringComparer.Ordinal.Equals(left, right);
        }

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(string value)
        {
            if (ThrowHash)
                throw new TestFailureException();
            return 0;
        }
    }

    private sealed class ThrowingValue(string text) : IEquatable<ThrowingValue>
    {
        /// <summary>Gets the text.</summary>
        internal string Text { get; } = text;

        /// <summary>Throws the equals.</summary>
        internal bool ThrowEquals { get; set; }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(ThrowingValue? other)
        {
            if (ThrowEquals)
                throw new TestFailureException();
            return other is not null && Text == other.Text;
        }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public override bool Equals(object? obj) => obj is ThrowingValue other && Equals(other);

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public override int GetHashCode() => Text.GetHashCode(StringComparison.Ordinal);
    }

    private sealed class TestFailureException : Exception
    {
    }
}
