using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Boundary, persistence, concatenation, and randomized model coverage for <see cref="RrbVector{T}"/>.</summary>
public sealed class RrbVectorTests
{
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
}
