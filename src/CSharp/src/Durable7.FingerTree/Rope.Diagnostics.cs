using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Durable7.FingerTree;

public sealed partial class Rope<T>
{
    internal object RootIdentityForDiagnostics => _tree;

    internal RopeStructureDiagnostics GetStructureDiagnostics()
    {
        var chunkCount = 0;
        var minimumChunkLength = int.MaxValue;
        var maximumChunkLength = 0;
        var stores = new HashSet<object>(ReferenceEqualityComparer.Instance);
        long estimatedChunkStorageBytes = 0;

        foreach (var chunk in _tree)
        {
            chunkCount++;
            minimumChunkLength = Math.Min(minimumChunkLength, chunk.Length);
            maximumChunkLength = Math.Max(maximumChunkLength, chunk.Length);
            if (MemoryMarshal.TryGetArray(chunk.Data, out ArraySegment<T> segment) &&
                segment.Array is { } array &&
                stores.Add(array))
            {
                estimatedChunkStorageBytes = checked(
                    estimatedChunkStorageBytes + EstimateArrayBytes(array.Length, Unsafe.SizeOf<T>()));
            }
        }

        return new RopeStructureDiagnostics(
            ElementCount: Count,
            ChunkCount: chunkCount,
            MinimumChunkLength: chunkCount == 0 ? 0 : minimumChunkLength,
            MaximumChunkLength: maximumChunkLength,
            BackingStoreCount: stores.Count,
            EstimatedChunkStorageBytes: estimatedChunkStorageBytes);
    }

    internal int[] GetChunkLengthsForDiagnostics() => [.. _tree.Select(static chunk => chunk.Length)];

    internal int CountSharedBackingStoresForDiagnostics(Rope<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        var stores = GetBackingStores(_tree.Select(static chunk => chunk.Data));
        var shared = 0;
        foreach (var store in GetBackingStores(other._tree.Select(static chunk => chunk.Data)))
        {
            if (stores.Contains(store))
                shared++;
        }

        return shared;
    }

    private static HashSet<object> GetBackingStores(IEnumerable<ReadOnlyMemory<T>> memories)
    {
        var stores = new HashSet<object>(ReferenceEqualityComparer.Instance);
        foreach (var memory in memories)
        {
            if (MemoryMarshal.TryGetArray(memory, out ArraySegment<T> segment) && segment.Array is { } array)
                stores.Add(array);
        }

        return stores;
    }

    private static long EstimateArrayBytes(int length, int elementSize)
    {
        var bytes = checked((3L * IntPtr.Size) + ((long)length * elementSize));
        var alignment = IntPtr.Size;
        return checked((bytes + alignment - 1) & ~(alignment - 1));
    }
}

public sealed partial class MeasuredRope<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    internal object RootIdentityForDiagnostics => _tree;

    internal RopeStructureDiagnostics GetStructureDiagnostics()
    {
        var chunkCount = 0;
        var minimumChunkLength = int.MaxValue;
        var maximumChunkLength = 0;
        var stores = new HashSet<object>(ReferenceEqualityComparer.Instance);
        long estimatedChunkStorageBytes = 0;

        foreach (var chunk in _tree)
        {
            chunkCount++;
            minimumChunkLength = Math.Min(minimumChunkLength, chunk.Length);
            maximumChunkLength = Math.Max(maximumChunkLength, chunk.Length);
            var memory = chunk.Chunk.Data;
            if (MemoryMarshal.TryGetArray(memory, out ArraySegment<T> segment) &&
                segment.Array is { } array &&
                stores.Add(array))
            {
                estimatedChunkStorageBytes = checked(
                    estimatedChunkStorageBytes + EstimateMeasuredArrayBytes(array.Length, Unsafe.SizeOf<T>()));
            }
        }

        return new RopeStructureDiagnostics(
            ElementCount: Count,
            ChunkCount: chunkCount,
            MinimumChunkLength: chunkCount == 0 ? 0 : minimumChunkLength,
            MaximumChunkLength: maximumChunkLength,
            BackingStoreCount: stores.Count,
            EstimatedChunkStorageBytes: estimatedChunkStorageBytes);
    }

    internal int[] GetChunkLengthsForDiagnostics() => [.. _tree.Select(static chunk => chunk.Length)];

    internal int CountSharedBackingStoresForDiagnostics(MeasuredRope<T, TMeasure, TMeasureOps> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        var stores = GetMeasuredBackingStores(_tree.Select(static chunk => chunk.Chunk.Data));
        var shared = 0;
        foreach (var store in GetMeasuredBackingStores(other._tree.Select(static chunk => chunk.Chunk.Data)))
        {
            if (stores.Contains(store))
                shared++;
        }

        return shared;
    }

    private static HashSet<object> GetMeasuredBackingStores(IEnumerable<ReadOnlyMemory<T>> memories)
    {
        var stores = new HashSet<object>(ReferenceEqualityComparer.Instance);
        foreach (var memory in memories)
        {
            if (MemoryMarshal.TryGetArray(memory, out ArraySegment<T> segment) && segment.Array is { } array)
                stores.Add(array);
        }

        return stores;
    }

    private static long EstimateMeasuredArrayBytes(int length, int elementSize)
    {
        var bytes = checked((3L * IntPtr.Size) + ((long)length * elementSize));
        var alignment = IntPtr.Size;
        return checked((bytes + alignment - 1) & ~(alignment - 1));
    }
}

internal readonly record struct RopeStructureDiagnostics(
    int ElementCount,
    int ChunkCount,
    int MinimumChunkLength,
    int MaximumChunkLength,
    int BackingStoreCount,
    long EstimatedChunkStorageBytes);
