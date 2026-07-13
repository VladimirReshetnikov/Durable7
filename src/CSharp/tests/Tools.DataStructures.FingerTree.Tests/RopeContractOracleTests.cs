using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Locks the positional rope contracts used as the correctness and representation oracle for Axis 2 cursor work.
/// </summary>
public sealed class RopeContractOracleTests
{
    /// <summary>Bulk construction preserves logical order and produces the declared bounded, proportional chunk layout.</summary>
    [Fact]
    public void BulkConstruction_PreservesSequenceAndCanonicalChunkLayout()
    {
        var count = (RopeChunking.MaxChunkSize * 5) + 17;
        var values = Enumerable.Range(0, count).ToArray();

        var rope = Rope<int>.Create(values);
        var diagnostics = rope.GetStructureDiagnostics();
        var chunkLengths = rope.GetChunkLengthsForDiagnostics();

        Assert.Equal(values, rope.ToArray());
        Assert.Equal(count, diagnostics.ElementCount);
        Assert.Equal(6, diagnostics.ChunkCount);
        Assert.Equal([2048, 2048, 2048, 2048, 2048, 17], chunkLengths);
        Assert.Equal(chunkLengths.Length, diagnostics.BackingStoreCount);
        Assert.Equal(chunkLengths.Min(), diagnostics.MinimumChunkLength);
        Assert.Equal(chunkLengths.Max(), diagnostics.MaximumChunkLength);
        Assert.True(diagnostics.EstimatedChunkStorageBytes > 0);
        Assert.All(chunkLengths, length => Assert.InRange(length, 1, RopeChunking.MaxChunkSize));
        Assert.Equal(count, chunkLengths.Sum());

        var edited = rope.SetItem(RopeChunking.MaxChunkSize * 2, -1);
        var sliced = edited.Slice(1, edited.Count - 2);
        Assert.Equal(values.Skip(1).SkipLast(1).Select((value, index) =>
            index == (RopeChunking.MaxChunkSize * 2) - 1 ? -1 : value), sliced.ToArray());
        AssertProportionalChunkCount(sliced);
    }

    /// <summary>Public imports copy caller storage while persistent slicing and local edits share only rope-owned stores.</summary>
    [Fact]
    public void ImportsAndPersistentEdits_IsolateCallersAndShareUntouchedStores()
    {
        var count = (RopeChunking.MaxChunkSize * 5) + 17;
        var callerArray = Enumerable.Range(0, count).ToArray();
        var expected = callerArray.ToArray();
        var rope = Rope<int>.FromChunks(callerArray);

        callerArray[0] = -1;
        callerArray[RopeChunking.MaxChunkSize * 3] = -2;
        Assert.Equal(expected, rope.ToArray());

        var slice = rope.Slice(1, rope.Count - 2);
        Assert.Equal(rope.GetStructureDiagnostics().BackingStoreCount, rope.CountSharedBackingStoresForDiagnostics(slice));

        var edited = rope.SetItem((RopeChunking.MaxChunkSize * 2) + 7, -3);
        Assert.Equal(expected, rope.ToArray());
        Assert.Equal(
            rope.GetStructureDiagnostics().BackingStoreCount - 1,
            rope.CountSharedBackingStoresForDiagnostics(edited));

        var compacted = slice.Compact();
        Assert.Equal(slice.ToArray(), compacted.ToArray());
        Assert.Equal(0, slice.CountSharedBackingStoresForDiagnostics(compacted));
    }

    /// <summary>Builder snapshots preserve promised reference identity, isolate earlier versions, and adopt source roots.</summary>
    [Fact]
    public void BuilderSnapshots_CacheByReferenceAndRetainPriorVersions()
    {
        var builder = Rope<int>.CreateBuilder();
        Assert.Same(Rope<int>.Empty, builder.ToImmutable());

        var firstValues = Enumerable.Range(0, RopeChunking.MaxChunkSize * 2).ToArray();
        builder.AddRange(firstValues);
        var first = builder.ToImmutable();

        Assert.Same(first, builder.ToImmutable());
        Assert.Same(first.RootIdentityForDiagnostics, builder.ToImmutable().RootIdentityForDiagnostics);

        var appended = Enumerable.Range(firstValues.Length, RopeChunking.MaxChunkSize).ToArray();
        builder.AddRange(appended);
        var second = builder.ToImmutable();

        Assert.NotSame(first, second);
        Assert.NotSame(first.RootIdentityForDiagnostics, second.RootIdentityForDiagnostics);
        Assert.Equal(firstValues, first.ToArray());
        Assert.Equal(firstValues.Concat(appended), second.ToArray());
        Assert.Equal(
            first.GetStructureDiagnostics().BackingStoreCount,
            first.CountSharedBackingStoresForDiagnostics(second));

        var adopted = first.ToBuilder();
        Assert.Same(first, adopted.ToImmutable());
        Assert.Same(first.RootIdentityForDiagnostics, adopted.ToImmutable().RootIdentityForDiagnostics);
    }

    /// <summary>Overflowing ranges are rejected without replacing or otherwise disturbing the immutable root.</summary>
    [Fact]
    public void OverflowingRanges_AreRejectedWithoutChangingTheSource()
    {
        var rope = Rope<int>.Create(1, 2, 3, 4);
        var root = rope.RootIdentityForDiagnostics;

        Assert.Throws<ArgumentOutOfRangeException>(() => rope.RemoveRange(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.Slice(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.GetRange(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.CopyTo(2, new int[3]));

        Assert.Same(root, rope.RootIdentityForDiagnostics);
        Assert.Equal([1, 2, 3, 4], rope.ToArray());
    }

    private static void AssertProportionalChunkCount<T>(Rope<T> rope)
    {
        var diagnostics = rope.GetStructureDiagnostics();
        var maximumForPackedChunksAndTwoBoundaryFragments =
            ((rope.Count + RopeChunking.MaxChunkSize - 1) / RopeChunking.MaxChunkSize) + 2;

        Assert.InRange(diagnostics.ChunkCount, 1, maximumForPackedChunksAndTwoBoundaryFragments);
        Assert.All(rope.GetChunkLengthsForDiagnostics(), length =>
            Assert.InRange(length, 1, RopeChunking.MaxChunkSize));
    }
}
