using System.Runtime.CompilerServices;

namespace Tools.DataStructures.Hamt;

public sealed partial class PersistentHashMap<TKey, TValue>
{
    internal object? RootIdentityForDiagnostics => _root;

    internal PersistentHashMapStructureDiagnostics GetStructureDiagnostics()
    {
        var nodes = new HashSet<Node>(ReferenceEqualityComparer.Instance);
        var arrays = new HashSet<object>(ReferenceEqualityComparer.Instance);
        CollectIdentities(_root, nodes, arrays);

        var leafNodes = 0;
        var collisionNodes = 0;
        var branchNodes = 0;
        var maximumDepth = 0;
        var retainedBytes = EstimateMapWrapperBytes();
        if (_root is not null)
        {
            var pending = new Stack<(Node Node, int Depth)>();
            pending.Push((_root, 1));
            while (pending.TryPop(out var item))
            {
                maximumDepth = Math.Max(maximumDepth, item.Depth);
                switch (item.Node)
                {
                    case LeafNode:
                        leafNodes++;
                        retainedBytes += EstimateLeafNodeBytes();
                        break;
                    case CollisionNode collision:
                        collisionNodes++;
                        retainedBytes += EstimateCollisionNodeBytes();
                        retainedBytes += EstimateArrayBytes(collision.Entries.Length, Unsafe.SizeOf<Entry>());
                        break;
                    case BitmapIndexedNode branch:
                        branchNodes++;
                        retainedBytes += EstimateBranchNodeBytes();
                        retainedBytes += EstimateArrayBytes(branch.Data.Length, Unsafe.SizeOf<Entry>());
                        retainedBytes += EstimateArrayBytes(branch.Children.Length, IntPtr.Size);
                        for (var index = 0; index < branch.Children.Length; index++)
                            pending.Push((branch.Children[index], item.Depth + 1));
                        break;
                }
            }
        }

        return new PersistentHashMapStructureDiagnostics(
            EntryCount: _count,
            NodeCount: nodes.Count,
            LeafNodeCount: leafNodes,
            CollisionNodeCount: collisionNodes,
            BranchNodeCount: branchNodes,
            ArrayCount: arrays.Count,
            MaximumDepth: maximumDepth,
            OwnerTaggedNodeCount: 0,
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

        visits++;
        if (node is LeafNode leaf)
        {
            _ = leaf.Hash == hash && _comparer.Equals(leaf.Key, key);
            return visits;
        }

        var collision = (CollisionNode)node;
        if (collision.Hash == hash)
        {
            foreach (var entry in collision.Entries)
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
                case BitmapIndexedNode branch:
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

    private static long EstimateCollisionNodeBytes() =>
        Align(checked((2L * IntPtr.Size) + sizeof(uint) + IntPtr.Size));

    private static long EstimateBranchNodeBytes() =>
        Align(checked((2L * IntPtr.Size) + (3L * sizeof(int)) + (2L * IntPtr.Size)));

    private static long EstimateArrayBytes(int length, int elementSize) =>
        Align(checked((3L * IntPtr.Size) + ((long)length * elementSize)));

    private static long Align(long bytes)
    {
        var alignment = IntPtr.Size;
        return checked((bytes + alignment - 1) & ~(alignment - 1));
    }
}

internal readonly record struct PersistentHashMapStructureDiagnostics(
    int EntryCount,
    int NodeCount,
    int LeafNodeCount,
    int CollisionNodeCount,
    int BranchNodeCount,
    int ArrayCount,
    int MaximumDepth,
    int OwnerTaggedNodeCount,
    long EstimatedRetainedBytes);

internal readonly record struct PersistentHashMapMutationDiagnostics(
    int NodeVisits,
    int CopiedNodeCount,
    int CopiedArrayCount,
    int SharedNodeCount,
    int SharedArrayCount,
    int WrapperAllocationCount,
    bool RootChanged);
