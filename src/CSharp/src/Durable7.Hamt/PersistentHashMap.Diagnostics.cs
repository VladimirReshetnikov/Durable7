// The structural diagnostics for the persistent hash map, used by the tests to assert on shape and
// sharing.

using System.Runtime.CompilerServices;

namespace Durable7.Hamt;

public sealed partial class PersistentHashMap<TKey, TValue>
{
    internal object? RootIdentityForDiagnostics => _root;

    internal PersistentHashMapStructureDiagnostics GetStructureDiagnostics()
    {
        var nodes = new HashSet<Node>(ReferenceEqualityComparer.Instance);
        var arrays = new HashSet<object>(ReferenceEqualityComparer.Instance);
        var ownedArrays = new HashSet<object>(ReferenceEqualityComparer.Instance);

        var leafNodes = 0;
        var collisionNodes = 0;
        var branchNodes = 0;
        var separateCollisionNodes = 0;
        var separateBranchNodes = 0;
        var ownerTaggedNodes = 0;
        var maximumDepth = 0;
        var ownerTokens = new HashSet<EditToken>(ReferenceEqualityComparer.Instance);
        var retainedBytes = EstimateMapWrapperBytes();
        const long estimatedOwnerMetadataBytes = 0;
        long estimatedSeparateNodeMetadataBytes = 0;
        if (_root is not null)
        {
            var pending = new Stack<(Node Node, int Depth)>();
            pending.Push((_root, 1));
            while (pending.TryPop(out var item))
            {
                if (!nodes.Add(item.Node))
                    continue;

                maximumDepth = Math.Max(maximumDepth, item.Depth);
                switch (item.Node)
                {
                    case LeafNode:
                        leafNodes++;
                        AddEstimatedBytes(ref retainedBytes, EstimateLeafNodeBytes());
                        break;

                    case CollisionNode collision:
                        collisionNodes++;
                        AddEstimatedBytes(ref retainedBytes, EstimateOrdinaryCollisionNodeBytes());
                        if (arrays.Add(collision.Entries))
                        {
                            AddEstimatedBytes(
                                ref retainedBytes,
                                EstimateArrayBytes(collision.Entries.Length, Unsafe.SizeOf<Entry>()));
                        }
                        break;

                    case SeparateTransientCollisionNode collision:
                        if (collision.Owner.IsActive)
                        {
                            throw new InvalidOperationException(
                                "A published collision node retains an active edit token.");
                        }
                        collisionNodes++;
                        separateCollisionNodes++;
                        ownerTaggedNodes++;
                        ownerTokens.Add(collision.Owner);
                        AddEstimatedBytes(
                            ref estimatedSeparateNodeMetadataBytes,
                            EstimateSeparateCollisionNodeBytes() - EstimateOrdinaryCollisionNodeBytes());
                        AddEstimatedBytes(ref retainedBytes, EstimateSeparateCollisionNodeBytes());
                        if (collision.EntriesOwned && collision.Entries.Length != 0)
                            AddOwnedArray(ownedArrays, collision.Entries);
                        if (arrays.Add(collision.Entries))
                        {
                            AddEstimatedBytes(
                                ref retainedBytes,
                                EstimateArrayBytes(collision.Entries.Length, Unsafe.SizeOf<Entry>()));
                        }
                        break;

                    case BitmapIndexedNode branch:
                        branchNodes++;
                        AddEstimatedBytes(ref retainedBytes, EstimateOrdinaryBranchNodeBytes());
                        if (arrays.Add(branch.Data))
                        {
                            AddEstimatedBytes(
                                ref retainedBytes,
                                EstimateArrayBytes(branch.Data.Length, Unsafe.SizeOf<Entry>()));
                        }
                        if (arrays.Add(branch.Children))
                        {
                            AddEstimatedBytes(
                                ref retainedBytes,
                                EstimateArrayBytes(branch.Children.Length, IntPtr.Size));
                        }
                        for (var index = 0; index < branch.Children.Length; index++)
                            pending.Push((branch.Children[index], item.Depth + 1));
                        break;

                    case SeparateTransientBranchNode branch:
                        if (branch.Owner.IsActive)
                        {
                            throw new InvalidOperationException(
                                "A published branch retains an active edit token.");
                        }
                        branchNodes++;
                        separateBranchNodes++;
                        ownerTaggedNodes++;
                        ownerTokens.Add(branch.Owner);
                        AddEstimatedBytes(
                            ref estimatedSeparateNodeMetadataBytes,
                            EstimateSeparateBranchNodeBytes() - EstimateOrdinaryBranchNodeBytes());
                        AddEstimatedBytes(ref retainedBytes, EstimateSeparateBranchNodeBytes());
                        if (branch.DataOwned && branch.Data.Length != 0)
                            AddOwnedArray(ownedArrays, branch.Data);
                        if (branch.ChildrenOwned && branch.Children.Length != 0)
                            AddOwnedArray(ownedArrays, branch.Children);
                        if (arrays.Add(branch.Data))
                        {
                            AddEstimatedBytes(
                                ref retainedBytes,
                                EstimateArrayBytes(branch.Data.Length, Unsafe.SizeOf<Entry>()));
                        }
                        if (arrays.Add(branch.Children))
                        {
                            AddEstimatedBytes(
                                ref retainedBytes,
                                EstimateArrayBytes(branch.Children.Length, IntPtr.Size));
                        }
                        for (var index = 0; index < branch.Children.Length; index++)
                            pending.Push((branch.Children[index], item.Depth + 1));
                        break;
                }
            }
        }

        var estimatedOwnerTokenBytes = checked(ownerTokens.Count * EstimateEditTokenBytes());
        AddEstimatedBytes(ref retainedBytes, estimatedOwnerTokenBytes);

        return new PersistentHashMapStructureDiagnostics(
            EntryCount: _count,
            NodeCount: nodes.Count,
            LeafNodeCount: leafNodes,
            CollisionNodeCount: collisionNodes,
            BranchNodeCount: branchNodes,
            SeparateCollisionNodeCount: separateCollisionNodes,
            SeparateBranchNodeCount: separateBranchNodes,
            ArrayCount: arrays.Count,
            MaximumDepth: maximumDepth,
            OwnerTaggedNodeCount: ownerTaggedNodes,
            OwnerTaggedArrayCount: ownedArrays.Count,
            OwnerTokenCount: ownerTokens.Count,
            EstimatedOwnerMetadataBytes: estimatedOwnerMetadataBytes,
            EstimatedSeparateNodeMetadataBytes: estimatedSeparateNodeMetadataBytes,
            EstimatedOwnerTokenBytes: estimatedOwnerTokenBytes,
            EstimatedRetainedBytes: retainedBytes);
    }

    internal PersistentHashMapMutationDiagnostics GetMutationDiagnostics(
        PersistentHashMap<TKey, TValue> previous,
        TKey editedKey)
    {
        ArgumentNullException.ThrowIfNull(previous);
        if (!ReferenceEquals(_comparer, previous._comparer))
            throw new ArgumentException("Map versions must use the same comparer object.", nameof(previous));

        var previousNodes = new HashSet<Node>(ReferenceEqualityComparer.Instance);
        var previousArrays = new HashSet<object>(ReferenceEqualityComparer.Instance);
        CollectIdentities(previous._root, previousNodes, previousArrays);

        var currentNodes = new HashSet<Node>(ReferenceEqualityComparer.Instance);
        var currentArrays = new HashSet<object>(ReferenceEqualityComparer.Instance);
        CollectIdentities(_root, currentNodes, currentArrays);

        var copiedNodes = 0;
        var sharedNodes = 0;
        foreach (var node in currentNodes)
        {
            if (previousNodes.Contains(node))
                sharedNodes++;
            else
                copiedNodes++;
        }

        var copiedArrays = 0;
        var sharedArrays = 0;
        foreach (var array in currentArrays)
        {
            if (previousArrays.Contains(array))
                sharedArrays++;
            else
                copiedArrays++;
        }

        return new PersistentHashMapMutationDiagnostics(
            NodeVisits: previous.CountNodeVisitsForDiagnostics(editedKey),
            CopiedNodeCount: copiedNodes,
            CopiedArrayCount: copiedArrays,
            SharedNodeCount: sharedNodes,
            SharedArrayCount: sharedArrays,
            WrapperAllocationCount: ReferenceEquals(this, previous) ? 0 : 1,
            RootChanged: !ReferenceEquals(_root, previous._root));
    }

    internal int CountNodeVisitsForDiagnostics(TKey key)
    {
        var node = _root;
        if (node is null)
            return 0;

        var hash = GetHash(key);
        var shift = 0;
        var visits = 0;
        while (node is BitmapIndexedNode branch)
        {
            visits++;
            var bit = Bit(Index(hash, shift));
            if ((branch.DataMap & bit) != 0)
            {
                var entry = branch.Data[Slot(branch.DataMap, bit)];
                _ = entry.Hash == hash && _comparer.Equals(entry.Key, key);
                return visits;
            }

            if ((branch.NodeMap & bit) == 0)
                return visits;

            node = branch.Children[Slot(branch.NodeMap, bit)];
            shift += BitsPerLevel;
        }

        while (node is SeparateTransientBranchNode or BitmapIndexedNode)
        {
            uint dataMap;
            Entry[] data;
            uint nodeMap;
            Node[] children;
            if (node is SeparateTransientBranchNode separate)
            {
                dataMap = separate.DataMap;
                data = separate.Data;
                nodeMap = separate.NodeMap;
                children = separate.Children;
            }
            else
            {
                var ordinary = (BitmapIndexedNode)node;
                dataMap = ordinary.DataMap;
                data = ordinary.Data;
                nodeMap = ordinary.NodeMap;
                children = ordinary.Children;
            }

            visits++;
            var bit = Bit(Index(hash, shift));
            if ((dataMap & bit) != 0)
            {
                var entry = data[Slot(dataMap, bit)];
                _ = entry.Hash == hash && _comparer.Equals(entry.Key, key);
                return visits;
            }

            if ((nodeMap & bit) == 0)
                return visits;

            node = children[Slot(nodeMap, bit)];
            shift += BitsPerLevel;
        }

        visits++;
        if (node is LeafNode leaf)
        {
            _ = leaf.Hash == hash && _comparer.Equals(leaf.Key, key);
            return visits;
        }

        var collisionHash = ((HashNode)node).Hash;
        var entries = node switch
        {
            CollisionNode collision => collision.Entries,
            SeparateTransientCollisionNode collision => collision.Entries,
            _ => throw new InvalidOperationException("Unknown CHAMP hash node kind."),
        };
        if (collisionHash == hash)
        {
            foreach (var entry in entries)
            {
                if (_comparer.Equals(entry.Key, key))
                    break;
            }
        }

        return visits;
    }

    private static void CollectIdentities(
        Node? root,
        HashSet<Node> nodes,
        HashSet<object> arrays)
    {
        if (root is null)
            return;

        var pending = new Stack<Node>();
        pending.Push(root);
        while (pending.TryPop(out var node))
        {
            if (!nodes.Add(node))
                continue;

            switch (node)
            {
                case CollisionNode collision:
                    arrays.Add(collision.Entries);
                    break;
                case SeparateTransientCollisionNode collision:
                    arrays.Add(collision.Entries);
                    break;
                case BitmapIndexedNode branch:
                    arrays.Add(branch.Data);
                    arrays.Add(branch.Children);
                    for (var index = 0; index < branch.Children.Length; index++)
                        pending.Push(branch.Children[index]);
                    break;
                case SeparateTransientBranchNode branch:
                    arrays.Add(branch.Data);
                    arrays.Add(branch.Children);
                    for (var index = 0; index < branch.Children.Length; index++)
                        pending.Push(branch.Children[index]);
                    break;
            }
        }
    }

    private static long EstimateMapWrapperBytes() =>
        Align(checked((2L * IntPtr.Size) + IntPtr.Size + sizeof(int) + IntPtr.Size));

    private static long EstimateLeafNodeBytes() =>
        Align(checked((2L * IntPtr.Size) + sizeof(uint) + Unsafe.SizeOf<TKey>() + Unsafe.SizeOf<TValue>()));

    private static long EstimateOrdinaryCollisionNodeBytes() =>
        Align(checked((2L * IntPtr.Size) + sizeof(uint) + IntPtr.Size));

    private static long EstimateSeparateCollisionNodeBytes() =>
        Align(checked((2L * IntPtr.Size) + sizeof(uint) + (2L * IntPtr.Size) + sizeof(byte)));

    private static long EstimateOrdinaryBranchNodeBytes() =>
        Align(checked((2L * IntPtr.Size) + (3L * sizeof(int)) + (2L * IntPtr.Size)));

    private static long EstimateSeparateBranchNodeBytes() =>
        Align(checked((2L * IntPtr.Size) + (3L * sizeof(int)) + (3L * IntPtr.Size) + sizeof(byte)));

    private static long EstimateEditTokenBytes() =>
        Align(checked((2L * IntPtr.Size) + sizeof(int)));

    private static long EstimateArrayBytes(int length, int elementSize) =>
        Align(checked((3L * IntPtr.Size) + ((long)length * elementSize)));

    private static void AddEstimatedBytes(ref long total, long value) =>
        total = checked(total + value);

    private static void AddOwnedArray(HashSet<object> ownedArrays, object array)
    {
        if (!ownedArrays.Add(array))
        {
            throw new InvalidOperationException(
                "Two transient-editable CHAMP nodes claim the same nonempty array.");
        }
    }

    private static long Align(long bytes)
    {
        var alignment = IntPtr.Size;
        return checked((bytes + alignment - 1) & ~(alignment - 1));
    }
}

/// <summary>Node and entry measurements from a structural audit.</summary>
internal readonly record struct PersistentHashMapStructureDiagnostics(
    int EntryCount,
    int NodeCount,
    int LeafNodeCount,
    int CollisionNodeCount,
    int BranchNodeCount,
    int SeparateCollisionNodeCount,
    int SeparateBranchNodeCount,
    int ArrayCount,
    int MaximumDepth,
    int OwnerTaggedNodeCount,
    int OwnerTaggedArrayCount,
    int OwnerTokenCount,
    long EstimatedOwnerMetadataBytes,
    long EstimatedSeparateNodeMetadataBytes,
    long EstimatedOwnerTokenBytes,
    long EstimatedRetainedBytes);

/// <summary>
/// How many nodes an edit copied versus reused, which is what structural sharing is measured by.
/// </summary>
internal readonly record struct PersistentHashMapMutationDiagnostics(
    int NodeVisits,
    int CopiedNodeCount,
    int CopiedArrayCount,
    int SharedNodeCount,
    int SharedArrayCount,
    int WrapperAllocationCount,
    bool RootChanged);
