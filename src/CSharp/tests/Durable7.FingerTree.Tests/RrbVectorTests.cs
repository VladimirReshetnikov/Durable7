// Tests for the RRB vector.

using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Boundary, persistence, concatenation, and randomized model coverage for <see cref="RrbVector{T}"/>.</summary>
public sealed class RrbVectorTests
{
    /// <summary>Verifies that replacing an item with itself does not invoke user equality.</summary>
    [Fact]
    public void SameReferenceReplacement_BypassesValueEquality()
    {
        var value = new EqualityCountingValue();
        var vector = RrbVector<EqualityCountingValue>.CreateRange([value]);

        Assert.Same(vector, vector.SetItem(0, value));
        Assert.Equal(0, value.EqualityCalls);
    }

    private sealed class EqualityCountingValue
    {
        /// <summary>Gets how many times the policy was asked to compare.</summary>
        public int EqualityCalls { get; private set; }
        /// <summary>Determines whether both values hold the same elements.</summary>
        public override bool Equals(object? obj) { EqualityCalls++; return ReferenceEquals(this, obj); }
        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public override int GetHashCode() => 0;
    }

    /// <summary>Verifies construction and indexing across every branch-factor boundary.</summary>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(31)]
    [InlineData(32)]
    [InlineData(33)]
    [InlineData(1_023)]
    [InlineData(1_024)]
    [InlineData(1_025)]
    [InlineData(100_000)]
    public void CreateRange_IndexesAndEnumerates(int count)
    {
        var vector = RrbVector<int>.CreateRange(Enumerable.Range(0, count));
        Assert.Equal(count, vector.Count);
        Assert.Equal(Enumerable.Range(0, count), vector);
        for (var i = 0; i < count; i += Math.Max(1, count / 101))
            Assert.Equal(i, vector[i]);
        Assert.True(vector.ValidateStructure(out var statistics));
        Assert.Equal(count, statistics.Count);
        Assert.Equal(0, statistics.RelaxedBranchCount);
    }

    /// <summary>Verifies concatenation across unequal heights and small boundary chunks.</summary>
    [Theory]
    [InlineData(1, 100_000)]
    [InlineData(100_000, 1)]
    [InlineData(31, 33)]
    [InlineData(1_023, 1_025)]
    [InlineData(50_000, 50_000)]
    public void Concat_RebalancesBoundarySpines(int leftCount, int rightCount)
    {
        var left = RrbVector<int>.CreateRange(Enumerable.Range(0, leftCount));
        var right = RrbVector<int>.CreateRange(Enumerable.Range(leftCount, rightCount));
        var combined = left.Concat(right);

        Assert.Equal(leftCount + rightCount, combined.Count);
        Assert.Equal(Enumerable.Range(0, leftCount + rightCount), combined);
        Assert.True(combined.ValidateStructure(out _));
        Assert.Same(left.RootIdentity, left.Concat(RrbVector<int>.Empty).RootIdentity);
        Assert.Same(right.RootIdentity, RrbVector<int>.Empty.Concat(right).RootIdentity);
    }

    /// <summary>Verifies splits round-trip and preserve boundary identity.</summary>
    [Fact]
    public void SplitAt_RoundTripsEveryRepresentativeBoundary()
    {
        var vector = RrbVector<int>.CreateRange(Enumerable.Range(0, 10_000));
        foreach (var index in new[] { 0, 1, 31, 32, 33, 999, 1_024, 5_000, 9_999, 10_000 })
        {
            var (left, right) = vector.SplitAt(index);
            Assert.Equal(Enumerable.Range(0, index), left);
            Assert.Equal(Enumerable.Range(index, 10_000 - index), right);
            Assert.Equal(vector, left.Concat(right));
        }

        Assert.Same(vector, vector.SplitAt(0).Right);
        Assert.Same(vector, vector.SplitAt(vector.Count).Left);
    }

    /// <summary>Verifies a split on a leaf boundary reuses every original leaf on the appropriate side.</summary>
    [Fact]
    public void SplitAt_ExactLeafBoundariesReuseLeaves()
    {
        var vector = RrbVector<int>.CreateRange(Enumerable.Range(0, 32 * 128));
        var originalLeaves = vector.LeafIdentitiesForTesting();

        foreach (var index in new[] { 32, 64, 32 * 31, 32 * 32, 32 * 33, 32 * 96, 32 * 127 })
        {
            var (left, right) = vector.SplitAt(index);
            var retainedLeaves = left.LeafIdentitiesForTesting().Concat(right.LeafIdentitiesForTesting()).ToArray();

            Assert.Equal(originalLeaves.Length, retainedLeaves.Length);
            for (var i = 0; i < originalLeaves.Length; i++)
                Assert.Same(originalLeaves[i], retainedLeaves[i]);
            Assert.True(left.ValidateStructure(out _));
            Assert.True(right.ValidateStructure(out _));
        }
    }

    /// <summary>Verifies packed trees use radix nodes while a non-left-aligned suffix becomes relaxed.</summary>
    [Fact]
    public void Representation_UsesRadixNodesAndRestrictsSizeTablesToRelaxedBranches()
    {
        var packed = RrbVector<int>.CreateRange(Enumerable.Range(0, 32 * 1_024));
        Assert.True(packed.ValidateStructure(out var packedStatistics));
        Assert.True(packedStatistics.RegularBranchCount > 0);
        Assert.Equal(0, packedStatistics.RelaxedBranchCount);

        var (_, relaxed) = packed.SplitAt(1);
        Assert.True(relaxed.ValidateStructure(out var relaxedStatistics));
        Assert.True(relaxedStatistics.RelaxedBranchCount > 0);
        Assert.Equal(Enumerable.Range(1, packed.Count - 1), relaxed);
        for (var index = 0; index < relaxed.Count; index += 997)
            Assert.Equal(index + 1, relaxed[index]);
    }

    /// <summary>Stress-tests boundary spines without permitting density or height to drift pathologically.</summary>
    [Fact]
    public void AdversarialSplitConcatHistories_PreserveDensityAndLogarithmicHeight()
    {
        const int count = 32 * 1_024;
        var vector = RrbVector<int>.CreateRange(Enumerable.Range(0, count));
        var random = new Random(20260724);

        for (var operation = 0; operation < 2_000; operation++)
        {
            var boundary = operation % 7 switch
            {
                0 => 1,
                1 => 31,
                2 => 32,
                3 => 1_023,
                4 => 1_024,
                5 => count - 1,
                _ => random.Next(1, count),
            };
            var (left, right) = vector.SplitAt(boundary);
            vector = left.Concat(right);

            if (operation % 127 == 0)
                Assert.True(vector.ValidateStructure(out _));
        }

        Assert.Equal(Enumerable.Range(0, count), vector);
        Assert.True(vector.ValidateStructure(out var statistics));
        Assert.True(statistics.Height <= MinimumHeight(count) + 1);
        Assert.True(statistics.LeafCount <= (count + 15) / 16);
        Assert.True(statistics.BranchCount < statistics.LeafCount);
    }

    /// <summary>Verifies many uneven fragments remain dense after repeated boundary-spine concatenation.</summary>
    [Fact]
    public void AdversarialUnevenFragments_RemainDenseAndBalanced()
    {
        var vector = RrbVector<int>.Empty;
        var model = new List<int>();
        for (var fragment = 0; fragment < 2_048; fragment++)
        {
            var length = 1 + fragment * 17 % 63;
            var values = Enumerable.Range(model.Count, length).ToArray();
            vector = vector.Concat(RrbVector<int>.CreateRange(values));
            model.AddRange(values);
        }

        Assert.Equal(model, vector);
        Assert.True(vector.ValidateStructure(out var statistics));
        Assert.True(statistics.Height <= MinimumHeight(vector.Count) + 1);
        Assert.True(statistics.LeafCount <= (vector.Count + 15) / 16);
    }

    /// <summary>Checks mixed persistent edits against a list model while retaining old versions.</summary>
    [Fact]
    public void RandomizedHistory_MatchesListModelAndRetainedSnapshots()
    {
        var random = new Random(20260714);
        var vector = RrbVector<int>.Empty;
        var model = new List<int>();
        var snapshots = new List<(RrbVector<int>, int[])>();

        for (var step = 0; step < 10_000; step++)
        {
            switch (random.Next(5))
            {
                case 0:
                    vector = vector.AddLast(step);
                    model.Add(step);
                    break;
                case 1:
                    vector = vector.AddFirst(step);
                    model.Insert(0, step);
                    break;
                case 2 when model.Count != 0:
                    var replace = random.Next(model.Count);
                    vector = vector.SetItem(replace, -step);
                    model[replace] = -step;
                    break;
                case 3:
                    var insert = random.Next(model.Count + 1);
                    int[] values = [step, step + 1, step + 2];
                    vector = vector.InsertRange(insert, values);
                    model.InsertRange(insert, values);
                    break;
                case 4 when model.Count != 0:
                    var start = random.Next(model.Count);
                    var count = random.Next(model.Count - start + 1);
                    vector = vector.RemoveRange(start, count);
                    model.RemoveRange(start, count);
                    break;
            }

            Assert.Equal(model.Count, vector.Count);
            if (step % 701 == 0)
                snapshots.Add((vector, [.. model]));
        }

        Assert.Equal(model, vector);
        foreach (var (oldVector, oldModel) in snapshots)
            Assert.Equal(oldModel, oldVector);
    }

    /// <summary>Verifies no-op replacement and try-remove-last behavior.</summary>
    [Fact]
    public void NoOpAndEndpointContracts_PreserveIdentity()
    {
        var empty = RrbVector<string>.Empty;
        Assert.False(empty.TryRemoveLast(out var unchanged, out _));
        Assert.Same(empty, unchanged);

        var vector = RrbVector<string>.CreateRange(["a", "b", "c"]);
        Assert.Same(vector, vector.SetItem(1, "b"));
        Assert.True(vector.TryRemoveLast(out var remainder, out var value));
        Assert.Equal("c", value);
        Assert.Equal(new[] { "a", "b" }, remainder);
        Assert.Equal(new[] { "a", "b", "c" }, vector);
    }

    /// <summary>Verifies range-removal boundary failures identify the invalid argument.</summary>
    [Fact]
    public void RemoveRange_ValidatesIndexBeforeCount()
    {
        var vector = RrbVector<int>.CreateRange([1, 2, 3]);

        Assert.Equal("index", Assert.Throws<ArgumentOutOfRangeException>(() => vector.RemoveRange(-1, 0)).ParamName);
        Assert.Equal("index", Assert.Throws<ArgumentOutOfRangeException>(() => vector.RemoveRange(4, 0)).ParamName);
        Assert.Equal("count", Assert.Throws<ArgumentOutOfRangeException>(() => vector.RemoveRange(0, -1)).ParamName);
        Assert.Equal("count", Assert.Throws<ArgumentOutOfRangeException>(() => vector.RemoveRange(2, 2)).ParamName);
        Assert.Same(vector, vector.RemoveRange(vector.Count, 0));
    }

    /// <summary>Verifies builder freezes are cached and isolated from later mutable staging.</summary>
    [Fact]
    public void Builder_BulkStagesImmutableSnapshotsAndAdoptsFrozenPrefixes()
    {
        var builder = RrbVector<int>.CreateBuilder();
        var source = Enumerable.Range(0, 1_000).ToArray();
        builder.AddRange(source.AsSpan());
        source[0] = -1;
        var first = builder.ToImmutable();

        Assert.Same(first, builder.ToImmutable());
        Assert.Equal(1_000, builder.Count);
        Assert.Equal(Enumerable.Range(0, 1_000), first);
        Assert.True(first.ValidateStructure(out var firstStatistics));
        Assert.Equal(0, firstStatistics.RelaxedBranchCount);

        builder.Add(1_000);
        builder.AddRange(Enumerable.Range(1_001, 999));
        var second = builder.ToImmutable();
        Assert.Equal(Enumerable.Range(0, 1_000), first);
        Assert.Equal(Enumerable.Range(0, 2_000), second);
        Assert.True(second.ValidateStructure(out _));

        var adopted = second.ToBuilder();
        Assert.Same(second, adopted.ToImmutable());
        adopted.Add(2_000);
        var third = adopted.ToImmutable();
        Assert.Equal(Enumerable.Range(0, 2_000), second);
        Assert.Equal(Enumerable.Range(0, 2_001), third);

        var enumerator = adopted.GetEnumerator();
        adopted.Add(2_001);
        Assert.Throws<InvalidOperationException>(() => enumerator.MoveNext());
        adopted.Clear();
        Assert.Empty(adopted);
        Assert.Same(RrbVector<int>.Empty, adopted.ToImmutable());
    }

    private static int MinimumHeight(int count)
    {
        var height = 0;
        var capacity = 32L;
        while (capacity < count)
        {
            capacity *= 32;
            height++;
        }
        return height;
    }
}
