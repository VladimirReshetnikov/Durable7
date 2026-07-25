using System.Reflection;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text;
using Xunit;

namespace Durable7.Hamt.Tests;

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
        Assert.Equal(0, diagnostics.OwnerTokenCount);
        Assert.Equal(0, diagnostics.EstimatedOwnerMetadataBytes);
        Assert.Equal(0, diagnostics.EstimatedSeparateNodeMetadataBytes);
        Assert.True(diagnostics.EstimatedRetainedBytes > 0);
        Assert.Same(map.RootForTesting, map.RootIdentityForDiagnostics);
    }

    /// <summary>Independently pins the physical field shape of ordinary persistent nodes.</summary>
    [Fact]
    public void OrdinaryNodeTypes_ArePhysicallyOwnerFreeAndReadonlyShaped()
    {
        const BindingFlags flags =
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        var collisionFields = typeof(PersistentHashMap<int, int>.CollisionNode).GetFields(flags);
        var branchFields = typeof(PersistentHashMap<int, int>.BitmapIndexedNode).GetFields(flags);
        var separateCollisionType =
            typeof(PersistentHashMap<int, int>.SeparateTransientCollisionNode);
        var separateBranchType =
            typeof(PersistentHashMap<int, int>.SeparateTransientBranchNode);

        Assert.Equal(typeof(PersistentHashMap<int, int>.HashNode),
            typeof(PersistentHashMap<int, int>.CollisionNode).BaseType);
        Assert.Equal(typeof(PersistentHashMap<int, int>.Node),
            typeof(PersistentHashMap<int, int>.BitmapIndexedNode).BaseType);
        Assert.Single(collisionFields);
        Assert.Equal(typeof(PersistentHashMap<int, int>.Entry[]), collisionFields[0].FieldType);
        Assert.True(collisionFields[0].IsInitOnly);

        Assert.Equal(5, branchFields.Length);
        Assert.Equal(1, branchFields.Count(field => field.FieldType == typeof(int)));
        Assert.Equal(2, branchFields.Count(field => field.FieldType == typeof(uint)));
        Assert.Equal(1, branchFields.Count(field => field.FieldType == typeof(PersistentHashMap<int, int>.Entry[])));
        Assert.Equal(1, branchFields.Count(field => field.FieldType == typeof(PersistentHashMap<int, int>.Node[])));
        Assert.All(branchFields, field => Assert.True(field.IsInitOnly));

        Assert.DoesNotContain(
            collisionFields.Concat(branchFields),
            field => field.FieldType == typeof(PersistentHashMap<int, int>.EditToken)
                || field.Name.Contains("owner", StringComparison.OrdinalIgnoreCase)
                || field.Name.Contains("owned", StringComparison.OrdinalIgnoreCase));

        Assert.NotNull(typeof(PersistentHashMap<int, int>.CollisionNode).GetMethod("Set", flags));
        Assert.NotNull(typeof(PersistentHashMap<int, int>.CollisionNode).GetMethod("Remove", flags));
        Assert.NotNull(typeof(PersistentHashMap<int, int>.BitmapIndexedNode).GetMethod("Set", flags));
        Assert.NotNull(typeof(PersistentHashMap<int, int>.BitmapIndexedNode).GetMethod("Remove", flags));

        Assert.Equal(typeof(PersistentHashMap<int, int>.HashNode), separateCollisionType.BaseType);
        Assert.Equal(typeof(PersistentHashMap<int, int>.Node), separateBranchType.BaseType);
        var separateCollisionFields = separateCollisionType.GetFields(flags);
        var separateBranchFields = separateBranchType.GetFields(flags);
        Assert.Equal(3, separateCollisionFields.Length);
        Assert.Equal(1, separateCollisionFields.Count(field => field.FieldType == typeof(byte)));
        Assert.Equal(
            1,
            separateCollisionFields.Count(
                field => field.FieldType == typeof(PersistentHashMap<int, int>.EditToken)));
        Assert.Equal(7, separateBranchFields.Length);
        Assert.Equal(1, separateBranchFields.Count(field => field.FieldType == typeof(byte)));
        Assert.Equal(
            1,
            separateBranchFields.Count(
                field => field.FieldType == typeof(PersistentHashMap<int, int>.EditToken)));
    }

    /// <summary>Pins the b590 ordinary collision, branch, and monomorphic lookup source blocks.</summary>
    [Fact]
    public void OrdinaryNodeAndLookupSource_RemainsAtTheB590MeasurementShape()
    {
        var testSource = CurrentSourcePath();
        var mapSource = LocateMapSource(testSource);

        var source = File.ReadAllText(mapSource).Replace("\r\n", "\n", StringComparison.Ordinal);
        Assert.Equal(
            "44441c5613b07a0bb3c4ff5a0061dc855b3e0cc11ff8ca9f2626f93ef4a6c65b",
            SourceHash(ExtractBalancedBlock(source, "    internal sealed class CollisionNode(")));
        Assert.Equal(
            "43174447096bd020d1990155ad65a876b20324293195fd04ba82173ac829a049",
            SourceHash(ExtractBalancedBlock(source, "    internal sealed class BitmapIndexedNode(")));
        Assert.Equal(
            "bb78c838e5de0bc3a3c46b7a2803ce6b5f8a3f5de92166f2e777f0cdae862c6d",
            SourceHash(ExtractBalancedBlock(source, "            while (node is BitmapIndexedNode branch)")));
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

    /// <summary>Verifies the separate-node layout excludes empty arrays from writable-storage counts.</summary>
    [Fact]
    public void StructureDiagnostics_SeparateNodesExcludeEmptyArraysFromOwnerTaggedCount()
    {
        var kernel = PersistentHashMap<int, int>.Empty.CreateSeparateNodeTransientKernel();
        kernel.SetItem(0, 0);
        kernel.SetItem(1, 1);

        var map = kernel.Persist();
        var diagnostics = map.GetStructureDiagnostics();

        Assert.Equal(1, diagnostics.BranchNodeCount);
        Assert.Equal(2, diagnostics.ArrayCount);
        Assert.Equal(1, diagnostics.OwnerTaggedNodeCount);
        Assert.Equal(1, diagnostics.OwnerTaggedArrayCount);
        Assert.Equal(1, diagnostics.OwnerTokenCount);
        Assert.Equal(0, diagnostics.EstimatedOwnerMetadataBytes);
        Assert.Equal(SeparateBranchMetadataBytes(), diagnostics.EstimatedSeparateNodeMetadataBytes);
    }

    /// <summary>Verifies separate-node collision arrays are charged once as writable storage.</summary>
    [Fact]
    public void StructureDiagnostics_ChargeSeparateCollisionArrayOnce()
    {
        var empty = PersistentHashMap<int, int>.Create(new ConstantHashComparer());
        var kernel = empty.CreateSeparateNodeTransientKernel();
        kernel.SetItem(0, 0);
        kernel.SetItem(1, 1);

        var diagnostics = kernel.Persist().GetStructureDiagnostics();

        Assert.Equal(1, diagnostics.CollisionNodeCount);
        Assert.Equal(1, diagnostics.ArrayCount);
        Assert.Equal(1, diagnostics.OwnerTaggedNodeCount);
        Assert.Equal(1, diagnostics.OwnerTaggedArrayCount);
        Assert.Equal(1, diagnostics.OwnerTokenCount);
        Assert.Equal(0, diagnostics.EstimatedOwnerMetadataBytes);
        Assert.Equal(SeparateCollisionMetadataBytes(), diagnostics.EstimatedSeparateNodeMetadataBytes);
    }

    /// <summary>Verifies owner-free and separate-node object-size attribution exactly.</summary>
    [Fact]
    public void LayoutDiagnostics_ChargeOnlyActualSeparateNodeAndTokenMetadata()
    {
        var ordinary = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(index => KeyValuePair.Create(index, index)));
        var ordinaryDiagnostics = ordinary.GetStructureDiagnostics();

        Assert.Equal(0, ordinaryDiagnostics.OwnerTaggedNodeCount);
        Assert.Equal(0, ordinaryDiagnostics.OwnerTaggedArrayCount);
        Assert.Equal(0, ordinaryDiagnostics.OwnerTokenCount);
        Assert.Equal(0, ordinaryDiagnostics.EstimatedOwnerMetadataBytes);
        Assert.Equal(0, ordinaryDiagnostics.EstimatedSeparateNodeMetadataBytes);

        var kernel = ordinary.CreateSeparateNodeTransientKernel();
        for (var index = 0; index < 64; index++)
            kernel.SetItem(index, -index - 1);
        var publishedDiagnostics = kernel.Persist().GetStructureDiagnostics();

        var expectedSeparateMetadataBytes = checked(
            (publishedDiagnostics.SeparateCollisionNodeCount * SeparateCollisionMetadataBytes())
            + (publishedDiagnostics.SeparateBranchNodeCount * SeparateBranchMetadataBytes()));

        Assert.Equal(0, publishedDiagnostics.EstimatedOwnerMetadataBytes);
        Assert.Equal(expectedSeparateMetadataBytes, publishedDiagnostics.EstimatedSeparateNodeMetadataBytes);
        Assert.True(publishedDiagnostics.OwnerTaggedNodeCount > 0);
        Assert.True(publishedDiagnostics.OwnerTaggedArrayCount > 0);
        Assert.Equal(1, publishedDiagnostics.OwnerTokenCount);
        Assert.True(publishedDiagnostics.EstimatedOwnerTokenBytes > 0);
        Assert.True(publishedDiagnostics.EstimatedRetainedBytes > 0);
    }

    /// <summary>Independently totals every reachable object and array in representative layouts.</summary>
    [Fact]
    public void EstimatedRetainedBytes_EqualsIndependentExactGraphSum()
    {
        var ordinary = PersistentHashMap<int, int>.CreateRange(
            Enumerable.Range(0, 1_024).Select(index => KeyValuePair.Create(index, index)));

        var branchKernel = ordinary.CreateSeparateNodeTransientKernel();
        branchKernel.SetItem(513, -513);
        var mixedBranch = branchKernel.Persist();

        var comparer = new ConstantHashComparer();
        var collision = PersistentHashMap<int, int>.Create(comparer);
        for (var index = 0; index < 8; index++)
            collision = collision.SetItem(index, index);
        var collisionKernel = collision.CreateSeparateNodeTransientKernel();
        collisionKernel.SetItem(3, -3);
        collisionKernel.SetItem(8, 8);
        var mixedCollision = collisionKernel.Persist();

        var singleton = PersistentHashMap<int, int>.Empty.SetItem(42, 42);
        foreach (var map in new[]
                 {
                     PersistentHashMap<int, int>.Empty,
                     singleton,
                     ordinary,
                     collision,
                     mixedBranch,
                     mixedCollision,
                 })
        {
            Assert.Equal(
                IndependentlyEstimateRetainedBytes(map),
                map.GetStructureDiagnostics().EstimatedRetainedBytes);
        }
    }

    /// <summary>Verifies retained-graph diagnostics reject unpublished active editable nodes.</summary>
    [Fact]
    public void StructureDiagnostics_RejectActiveSeparateNodes()
    {
        var branchKernel = PersistentHashMap<int, int>.Empty.CreateSeparateNodeTransientKernel();
        branchKernel.SetItem(0, 0);
        branchKernel.SetItem(1, 1);
        var activeBranchMap = WrapActiveKernelRoot(branchKernel);
        Assert.Throws<InvalidOperationException>(() => _ = activeBranchMap.GetStructureDiagnostics());

        var collisionKernel = PersistentHashMap<int, int>.Create(new ConstantHashComparer())
            .CreateSeparateNodeTransientKernel();
        collisionKernel.SetItem(0, 0);
        collisionKernel.SetItem(1, 1);
        var activeCollisionMap = WrapActiveKernelRoot(collisionKernel);
        Assert.Throws<InvalidOperationException>(() => _ = activeCollisionMap.GetStructureDiagnostics());
    }

    /// <summary>Verifies two editable nodes cannot claim one nonempty array identity.</summary>
    [Fact]
    public void StructureDiagnostics_RejectDuplicateOwnedArrayClaims()
    {
        var token = new PersistentHashMap<int, int>.EditToken();
        token.Seal();
        var entries = new[]
        {
            new PersistentHashMap<int, int>.Entry(0, 0, 0),
            new PersistentHashMap<int, int>.Entry(0, 1, 1),
        };
        var left = new PersistentHashMap<int, int>.SeparateTransientCollisionNode(
            0,
            entries,
            token,
            entriesOwned: true);
        var right = new PersistentHashMap<int, int>.SeparateTransientCollisionNode(
            0,
            entries,
            token,
            entriesOwned: true);
        var root = new PersistentHashMap<int, int>.BitmapIndexedNode(
            dataMap: 0,
            data: [],
            nodeMap: 3,
            children: [left, right]);
        var malformed = WrapRoot(
            root,
            count: 4,
            comparer: EqualityComparer<int>.Default);

        var failure = Assert.Throws<InvalidOperationException>(
            () => _ = malformed.GetStructureDiagnostics());
        Assert.Contains("claim the same nonempty array", failure.Message, StringComparison.Ordinal);
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
                case PersistentHashMap<int, int>.CollisionNode collision:
                    referenceCount++;
                    if (collision.Entries.Length == 0)
                        emptyReferenceCount++;
                    arrays.Add(collision.Entries);
                    break;
                case PersistentHashMap<int, int>.SeparateTransientCollisionNode collision:
                    referenceCount++;
                    if (collision.Entries.Length == 0)
                        emptyReferenceCount++;
                    arrays.Add(collision.Entries);
                    break;
                case PersistentHashMap<int, int>.BitmapIndexedNode branch:
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
                case PersistentHashMap<int, int>.SeparateTransientBranchNode branch:
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

    private static long IndependentlyEstimateRetainedBytes(PersistentHashMap<int, int> map)
    {
        var total = AlignObject((2L * IntPtr.Size) + IntPtr.Size + sizeof(int) + IntPtr.Size);
        if (map.RootForTesting is null)
            return total;

        var nodes = new HashSet<PersistentHashMap<int, int>.Node>(ReferenceEqualityComparer.Instance);
        var arrays = new HashSet<object>(ReferenceEqualityComparer.Instance);
        var tokens = new HashSet<PersistentHashMap<int, int>.EditToken>(ReferenceEqualityComparer.Instance);
        var pending = new Stack<PersistentHashMap<int, int>.Node>();
        pending.Push(map.RootForTesting);
        while (pending.TryPop(out var node))
        {
            if (!nodes.Add(node))
                continue;

            switch (node)
            {
                case PersistentHashMap<int, int>.LeafNode:
                    AddIndependentBytes(
                        ref total,
                        AlignObject((2L * IntPtr.Size) + sizeof(uint) + sizeof(int) + sizeof(int)));
                    break;

                case PersistentHashMap<int, int>.CollisionNode collision:
                    AddIndependentBytes(
                        ref total,
                        AlignObject((2L * IntPtr.Size) + sizeof(uint) + IntPtr.Size));
                    AddIndependentArray(
                        ref total,
                        arrays,
                        collision.Entries,
                        Unsafe.SizeOf<PersistentHashMap<int, int>.Entry>());
                    break;

                case PersistentHashMap<int, int>.SeparateTransientCollisionNode collision:
                    AddIndependentBytes(
                        ref total,
                        AlignObject(
                            (2L * IntPtr.Size)
                            + sizeof(uint)
                            + (2L * IntPtr.Size)
                            + sizeof(byte)));
                    tokens.Add(collision.Owner);
                    AddIndependentArray(
                        ref total,
                        arrays,
                        collision.Entries,
                        Unsafe.SizeOf<PersistentHashMap<int, int>.Entry>());
                    break;

                case PersistentHashMap<int, int>.BitmapIndexedNode branch:
                    AddIndependentBytes(
                        ref total,
                        AlignObject(
                            (2L * IntPtr.Size)
                            + (3L * sizeof(int))
                            + (2L * IntPtr.Size)));
                    AddIndependentArray(
                        ref total,
                        arrays,
                        branch.Data,
                        Unsafe.SizeOf<PersistentHashMap<int, int>.Entry>());
                    AddIndependentArray(ref total, arrays, branch.Children, IntPtr.Size);
                    foreach (var child in branch.Children)
                        pending.Push(child);
                    break;

                case PersistentHashMap<int, int>.SeparateTransientBranchNode branch:
                    AddIndependentBytes(
                        ref total,
                        AlignObject(
                            (2L * IntPtr.Size)
                            + (3L * sizeof(int))
                            + (3L * IntPtr.Size)
                            + sizeof(byte)));
                    tokens.Add(branch.Owner);
                    AddIndependentArray(
                        ref total,
                        arrays,
                        branch.Data,
                        Unsafe.SizeOf<PersistentHashMap<int, int>.Entry>());
                    AddIndependentArray(ref total, arrays, branch.Children, IntPtr.Size);
                    foreach (var child in branch.Children)
                        pending.Push(child);
                    break;

                default:
                    throw new InvalidOperationException($"Unknown CHAMP node type {node.GetType()}.");
            }
        }

        AddIndependentBytes(
            ref total,
            checked(tokens.Count * AlignObject((2L * IntPtr.Size) + sizeof(int))));
        return total;
    }

    private static void AddIndependentArray<T>(
        ref long total,
        HashSet<object> arrays,
        T[] array,
        int elementSize)
    {
        if (!arrays.Add(array))
            return;
        AddIndependentBytes(
            ref total,
            AlignObject(checked((3L * IntPtr.Size) + ((long)array.Length * elementSize))));
    }

    private static void AddIndependentBytes(ref long total, long bytes) =>
        total = checked(total + bytes);

    private static PersistentHashMap<int, int> WrapActiveKernelRoot(
        PersistentHashMap<int, int>.Transient kernel)
        => WrapRoot(
            (PersistentHashMap<int, int>.Node)kernel.RootIdentityForDiagnostics!,
            kernel.Count,
            kernel.Comparer);

    private static PersistentHashMap<int, int> WrapRoot(
        PersistentHashMap<int, int>.Node root,
        int count,
        IEqualityComparer<int> comparer)
    {
        var constructor = typeof(PersistentHashMap<int, int>).GetConstructor(
            BindingFlags.Instance | BindingFlags.NonPublic,
            binder: null,
            [typeof(PersistentHashMap<int, int>.Node), typeof(int), typeof(IEqualityComparer<int>)],
            modifiers: null);
        Assert.NotNull(constructor);
        return (PersistentHashMap<int, int>)constructor.Invoke(
            [root, count, comparer]);
    }

    private readonly record struct ArrayIdentityCounts(
        int ReferenceCount,
        int DistinctCount,
        int EmptyReferenceCount);

    private static string CurrentSourcePath([CallerFilePath] string path = "") => path;

    private static string LocateMapSource(string testSource)
    {
        if (File.Exists(testSource))
        {
            var besideWorkspace = Path.GetFullPath(
                Path.Combine(
                    Path.GetDirectoryName(testSource)!,
                    "..",
                    "..",
                    "src",
                    "Durable7.Hamt",
                    "PersistentHashMap.cs"));
            if (File.Exists(besideWorkspace))
                return besideWorkspace;
        }

        foreach (var origin in new[] { AppContext.BaseDirectory, Directory.GetCurrentDirectory() })
        {
            for (DirectoryInfo? directory = new(origin);
                 directory is not null;
                 directory = directory.Parent)
            {
                var candidate = Path.Combine(
                    directory.FullName,
                    "src",
                    "CSharp",
                    "src",
                    "Durable7.Hamt",
                    "PersistentHashMap.cs");
                if (File.Exists(candidate))
                    return candidate;
            }
        }

        throw new InvalidOperationException(
            $"Cannot locate PersistentHashMap.cs from '{testSource}' or the test output directory.");
    }

    private static string ExtractBalancedBlock(string source, string marker)
    {
        var start = source.IndexOf(marker, StringComparison.Ordinal);
        Assert.True(start >= 0, $"Missing guarded source marker '{marker}'.");
        var open = source.IndexOf('{', start);
        Assert.True(open >= 0, $"Missing opening brace after '{marker}'.");

        var depth = 0;
        for (var index = open; index < source.Length; index++)
        {
            switch (source[index])
            {
                case '{':
                    depth++;
                    break;
                case '}':
                    depth--;
                    if (depth == 0)
                        return source[start..(index + 1)];
                    break;
            }
        }

        throw new InvalidOperationException($"The guarded source block '{marker}' is unbalanced.");
    }

    private static string SourceHash(string source) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(source))).ToLowerInvariant();

    private static long AlignObject(long bytes) =>
        checked((bytes + IntPtr.Size - 1) & ~(IntPtr.Size - 1));

    private static long SeparateCollisionMetadataBytes() =>
        AlignObject((2L * IntPtr.Size) + sizeof(uint) + (2L * IntPtr.Size) + sizeof(byte))
        - AlignObject((2L * IntPtr.Size) + sizeof(uint) + IntPtr.Size);

    private static long SeparateBranchMetadataBytes() =>
        AlignObject((2L * IntPtr.Size) + (3L * sizeof(int)) + (3L * IntPtr.Size) + sizeof(byte))
        - AlignObject((2L * IntPtr.Size) + (3L * sizeof(int)) + (2L * IntPtr.Size));

    private sealed class ConstantHashComparer : IEqualityComparer<int>
    {
        public bool Equals(int left, int right) => left == right;

        public int GetHashCode(int value) => 0;
    }
}
