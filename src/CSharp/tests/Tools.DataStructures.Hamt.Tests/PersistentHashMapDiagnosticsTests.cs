using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Verifies the internal Axis 2 structural-counter seam against immutable CHAMP versions.</summary>
public sealed class PersistentHashMapDiagnosticsTests
{
    /// <summary>Verifies structure counters describe every entry and retained storage category.</summary>
    [Fact]
    public void StructureDiagnostics_DescribePublishedGraph()
    {
        var map = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 512).Select(index => KeyValuePair.Create(index, index * 17)));

        var diagnostics = map.GetStructureDiagnostics();

        Assert.Equal(map.Count, diagnostics.EntryCount);
        Assert.True(diagnostics.NodeCount > 0);
        Assert.Equal(
            diagnostics.NodeCount,
            diagnostics.LeafNodeCount + diagnostics.CollisionNodeCount + diagnostics.BranchNodeCount);
        Assert.True(diagnostics.ArrayCount > 0);
        Assert.InRange(diagnostics.MaximumDepth, 1, 7);
        Assert.Equal(0, diagnostics.OwnerTaggedNodeCount);
        Assert.True(diagnostics.EstimatedRetainedBytes > 0);
        Assert.Same(map.RootForTesting, map.RootIdentityForDiagnostics);
    }

    /// <summary>Verifies a real edit reports copied and shared graph pieces while a no-op reports none.</summary>
    [Fact]
    public void MutationDiagnostics_DistinguishPathCopyFromLogicalNoOp()
    {
        var source = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 512).Select(index => KeyValuePair.Create(index, index)));
        var edited = source.SetItem(257, -1);
        var changed = edited.GetMutationDiagnostics(source, 257);

        Assert.True(changed.NodeVisits > 0);
        Assert.True(changed.CopiedNodeCount > 0);
        Assert.True(changed.CopiedArrayCount > 0);
        Assert.True(changed.SharedNodeCount > 0);
        Assert.True(changed.SharedArrayCount > 0);
        Assert.Equal(1, changed.WrapperAllocationCount);
        Assert.True(changed.RootChanged);

        var noOp = source.SetItem(257, 257);
        var unchanged = noOp.GetMutationDiagnostics(source, 257);
        Assert.Same(source, noOp);
        Assert.Equal(0, unchanged.CopiedNodeCount);
        Assert.Equal(0, unchanged.CopiedArrayCount);
        Assert.Equal(0, unchanged.WrapperAllocationCount);
        Assert.False(unchanged.RootChanged);
    }

    /// <summary>Verifies collision storage and the canonical bulk-builder seam are visible to diagnostics.</summary>
    [Fact]
    public void CollisionAndBulkBuilder_AreRepresentedByDiagnosticSeam()
    {
        var comparer = new ConstantHashComparer();
        var builder = PersistentHashMap<int, int>.CreateBulkBuilder(comparer);
        for (var index = 0; index < 8; index++)
            builder.SetItem(index, index * 3);

        Assert.Equal(8, builder.Count);
        var map = builder.ToImmutable();
        var diagnostics = map.GetStructureDiagnostics();

        Assert.Equal(8, diagnostics.EntryCount);
        Assert.Equal(1, diagnostics.CollisionNodeCount);
        Assert.Equal(1, diagnostics.ArrayCount);
        Assert.Equal(1, map.CountNodeVisitsForDiagnostics(7));
    }

    /// <summary>Verifies shared empty arrays are charged once in the retained graph.</summary>
    [Fact]
    public void StructureDiagnostics_CountDistinctArrayIdentities()
    {
        var map = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(index => KeyValuePair.Create(index, index)));

        var expected = CountArrayIdentities(map.RootForTesting);
        var diagnostics = map.GetStructureDiagnostics();

        Assert.True(expected.ReferenceCount > expected.DistinctCount);
        Assert.True(expected.EmptyReferenceCount > 1);
        Assert.Equal(expected.DistinctCount, diagnostics.ArrayCount);
    }

    /// <summary>Verifies both T1 layouts exclude empty arrays from writable-storage counts.</summary>
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void StructureDiagnostics_ExcludeEmptyArraysFromOwnerTaggedCount(bool separateNodes)
    {
        var kernel = separateNodes
            ? PersistentHashMap<int, int>.Empty.CreateSeparateNodeTransientKernel()
            : PersistentHashMap<int, int>.Empty.CreateOwnerTokenTransientKernel();
        kernel.SetItem(0, 0);
        kernel.SetItem(1, 1);

        var map = kernel.Persist();
        var diagnostics = map.GetStructureDiagnostics();

        Assert.Equal(1, diagnostics.BranchNodeCount);
        Assert.Equal(2, diagnostics.ArrayCount);
        Assert.Equal(1, diagnostics.OwnerTaggedNodeCount);
        Assert.Equal(1, diagnostics.OwnerTaggedArrayCount);
        Assert.Equal(1, diagnostics.OwnerTokenCount);
    }

    /// <summary>Verifies collision arrays are charged once and recognized as writable storage.</summary>
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void StructureDiagnostics_ChargeOwnedCollisionArrayOnce(bool separateNodes)
    {
        var empty = PersistentHashMap<int, int>.Create(new ConstantHashComparer());
        var kernel = separateNodes
            ? empty.CreateSeparateNodeTransientKernel()
            : empty.CreateOwnerTokenTransientKernel();
        kernel.SetItem(0, 0);
        kernel.SetItem(1, 1);

        var diagnostics = kernel.Persist().GetStructureDiagnostics();

        Assert.Equal(1, diagnostics.CollisionNodeCount);
        Assert.Equal(1, diagnostics.ArrayCount);
        Assert.Equal(1, diagnostics.OwnerTaggedNodeCount);
        Assert.Equal(1, diagnostics.OwnerTaggedArrayCount);
        Assert.Equal(1, diagnostics.OwnerTokenCount);
    }

    private static ArrayIdentityCounts CountArrayIdentities(PersistentHashMap<int, int>.Node? root)
    {
        if (root is null)
            return default;

        var nodes = new HashSet<PersistentHashMap<int, int>.Node>(ReferenceEqualityComparer.Instance);
        var arrays = new HashSet<object>(ReferenceEqualityComparer.Instance);
        var pending = new Stack<PersistentHashMap<int, int>.Node>();
        var referenceCount = 0;
        var emptyReferenceCount = 0;
        pending.Push(root);
        while (pending.TryPop(out var node))
        {
            if (!nodes.Add(node))
                continue;

            switch (node)
            {
                case PersistentHashMap<int, int>.CollisionNodeBase collision:
                    referenceCount++;
                    if (collision.Entries.Length == 0)
                        emptyReferenceCount++;
                    arrays.Add(collision.Entries);
                    break;
                case PersistentHashMap<int, int>.BranchNode branch:
                    referenceCount += 2;
                    if (branch.Data.Length == 0)
                        emptyReferenceCount++;
                    if (branch.Children.Length == 0)
                        emptyReferenceCount++;
                    arrays.Add(branch.Data);
                    arrays.Add(branch.Children);
                    for (var index = 0; index < branch.Children.Length; index++)
                        pending.Push(branch.Children[index]);
                    break;
            }
        }

        return new ArrayIdentityCounts(referenceCount, arrays.Count, emptyReferenceCount);
    }

    private readonly record struct ArrayIdentityCounts(
        int ReferenceCount,
        int DistinctCount,
        int EmptyReferenceCount);

    private sealed class ConstantHashComparer : IEqualityComparer<int>
    {
        public bool Equals(int left, int right) => left == right;

        public int GetHashCode(int value) => 0;
    }
}
