// Tests for the persistent chunked bit set.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Verifies sparse chunking, rank/select, algebra, and persistent branching.</summary>
public sealed class PersistentChunkedBitSetTests
{
    /// <summary>Verifies empty and point-update contracts including the largest supported index.</summary>
    [Fact]
    public void EmptyAndPointUpdates_RespectDomainAndIdentity()
    {
        var empty = PersistentChunkedBitSet.Empty;
        Assert.True(empty.IsEmpty);
        Assert.False(empty.Contains(-1));
        Assert.Same(empty, empty.Remove(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => empty.Add(-1));

        var set = empty.Add(0).Add(int.MaxValue);
        Assert.True(set.Contains(0));
        Assert.True(set.Contains(int.MaxValue));
        Assert.Equal(2, set.Count);
        Assert.Equal(2, set.ChunkCount);
        Assert.Same(set, set.Add(int.MaxValue));
        Assert.False(set.TryAdd(0, out var unchanged));
        Assert.Same(set, unchanged);
        Assert.False(empty.Contains(0));
    }

    /// <summary>Verifies enumeration and chunk accounting across word boundaries.</summary>
    [Fact]
    public void CreateRange_OrdersAndDeduplicatesAcrossChunks()
    {
        var expected = new[] { 0, 1, 63, 64, 65, 130, int.MaxValue };
        var set = PersistentChunkedBitSet.CreateRange([65, 0, 64, 63, 1, 130, 65, int.MaxValue]);

        Assert.Equal(expected, set);
        Assert.Equal(expected.Length, set.Count);
        Assert.Equal(4, set.ChunkCount);
        set.ValidateInvariants();
    }

    /// <summary>Verifies inclusive rank at, between, and beyond represented chunks.</summary>
    [Fact]
    public void Rank_IsInclusive()
    {
        var set = PersistentChunkedBitSet.CreateRange([0, 2, 63, 64, 100, 130]);

        Assert.Equal(0, set.Rank(-1));
        Assert.Equal(1, set.Rank(0));
        Assert.Equal(1, set.Rank(1));
        Assert.Equal(2, set.Rank(2));
        Assert.Equal(3, set.Rank(63));
        Assert.Equal(4, set.Rank(64));
        Assert.Equal(4, set.Rank(99));
        Assert.Equal(5, set.Rank(100));
        Assert.Equal(6, set.Rank(int.MaxValue));
    }

    /// <summary>Verifies zero-based select is the inverse of enumeration order.</summary>
    [Fact]
    public void Select_ReturnsPopulationOrder()
    {
        var expected = new[] { 0, 1, 63, 64, 65, 511, int.MaxValue };
        var set = PersistentChunkedBitSet.CreateRange(expected);

        for (var rank = 0; rank < expected.Length; rank++)
        {
            Assert.Equal(expected[rank], set.Select(rank));
            Assert.True(set.TrySelect(rank, out var selected));
            Assert.Equal(expected[rank], selected);
        }

        Assert.False(set.TrySelect(-1, out _));
        Assert.False(set.TrySelect(expected.Length, out _));
        Assert.Throws<ArgumentOutOfRangeException>(() => set.Select(expected.Length));
    }

    /// <summary>Verifies removing the final bit from a word contracts its chunk.</summary>
    [Fact]
    public void Remove_ContractsEmptyChunksAndPreservesAbsentIdentity()
    {
        var source = PersistentChunkedBitSet.CreateRange([1, 64, 65]);
        var reduced = source.Remove(64).Remove(65);

        Assert.Equal([1], reduced);
        Assert.Equal(1, reduced.ChunkCount);
        Assert.Same(reduced, reduced.Remove(65));
        Assert.False(reduced.TryRemove(65, out var unchanged));
        Assert.Same(reduced, unchanged);
        Assert.Equal([1, 64, 65], source);
    }

    /// <summary>Verifies all set-algebra operations and useful receiver-identity cases.</summary>
    [Fact]
    public void SetAlgebra_MatchesMathematicalSets()
    {
        var left = PersistentChunkedBitSet.CreateRange([0, 1, 64, 130]);
        var right = PersistentChunkedBitSet.CreateRange([1, 2, 64, 65]);

        Assert.Equal([0, 1, 2, 64, 65, 130], left.Union(right));
        Assert.Equal([1, 64], left.Intersect(right));
        Assert.Equal([0, 130], left.Except(right));
        Assert.Equal([0, 2, 65, 130], left.SymmetricExcept(right));
        Assert.Same(left, left.Union(PersistentChunkedBitSet.CreateRange([1, 64])));
        Assert.Same(left, left.Intersect(left.Union(right)));
        Assert.Same(PersistentChunkedBitSet.Empty, left.Except(left));
        Assert.Same(PersistentChunkedBitSet.Empty, left.SymmetricExcept(left));
    }

    /// <summary>Exercises point edits, rank, and select against a mutable sorted-set model.</summary>
    [Fact]
    public void RandomizedPointOperations_MatchSortedSetModel()
    {
        var random = new Random(1729);
        var model = new System.Collections.Generic.SortedSet<int>();
        var set = PersistentChunkedBitSet.Empty;

        for (var operation = 0; operation < 700; operation++)
        {
            var bit = random.Next(0, 4096);
            if (random.Next(2) == 0)
            {
                model.Add(bit);
                set = set.Add(bit);
            }
            else
            {
                model.Remove(bit);
                set = set.Remove(bit);
            }

            Assert.Equal(model, set);
            Assert.Equal(model.Count, set.Count);
            var probe = random.Next(-1, 4097);
            Assert.Equal(model.Count(value => value <= probe), set.Rank(probe));
            var ordered = model.ToArray();
            for (var rank = 0; rank < ordered.Length; rank++)
                Assert.Equal(ordered[rank], set.Select(rank));
            set.ValidateInvariants();
        }
    }

    /// <summary>Verifies retained source snapshots can branch independently.</summary>
    [Fact]
    public void Branches_AreIndependent()
    {
        var root = PersistentChunkedBitSet.CreateRange([1, 64, 130]);
        var left = root.Add(2).Remove(64);
        var right = root.Add(65).Remove(1);

        Assert.Equal([1, 64, 130], root);
        Assert.Equal([1, 2, 130], left);
        Assert.Equal([64, 65, 130], right);
        Assert.Same(PersistentChunkedBitSet.Empty, root.Clear().Clear());
        root.ValidateInvariants();
        left.ValidateInvariants();
        right.ValidateInvariants();
    }
}
