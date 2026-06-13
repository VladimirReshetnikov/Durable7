using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies the ready-made measures and their extension operations: max/min priority queues, key-measured
/// ordered lower/upper-bound splits, and the order-statistic tree that is positional and ordered at once.
/// </summary>
public sealed class BuiltInMeasureTests
{
    /// <summary>Verifies the optional carrier reports presence and value correctly.</summary>
    [Fact]
    public void Optional_NoneAndSome_BehaveAsExpected()
    {
        Assert.False(Optional<int>.None.HasValue);
        var some = Optional<string>.Some("x");
        Assert.True(some.HasValue);
        Assert.Equal("x", some.Value);
    }

    /// <summary>
    /// Verifies a max-measured tree behaves as a max-priority queue: the measure is the maximum, peeking is
    /// O(1)-correct, and draining via extraction yields nonincreasing order.
    /// </summary>
    [Fact]
    public void MaxMeasure_DrainsInNonincreasingOrder()
    {
        var random = new Random(13);
        var values = Enumerable.Range(0, 400).Select(_ => random.Next(1000)).ToArray();
        var pq = FingerTree<int, Optional<int>, MaxMeasure<int>>.CreateRange(values);

        Assert.True(pq.TryPeekMax(out var peeked));
        Assert.Equal(values.Max(), peeked);
        Assert.Equal(values.Max(), pq.Measure.Value);

        var drained = new List<int>();
        while (pq.TryExtractMax(out var top, out pq))
            drained.Add(top);

        Assert.Equal(values.OrderByDescending(v => v).ToArray(), drained);
        Assert.False(pq.TryPeekMax(out _));
    }

    /// <summary>Verifies a min-measured tree behaves as a min-priority queue draining in nondecreasing order.</summary>
    [Fact]
    public void MinMeasure_DrainsInNondecreasingOrder()
    {
        var random = new Random(14);
        var values = Enumerable.Range(0, 400).Select(_ => random.Next(1000)).ToArray();
        var pq = FingerTree<int, Optional<int>, MinMeasure<int>>.CreateRange(values);

        Assert.True(pq.TryPeekMin(out var peeked));
        Assert.Equal(values.Min(), peeked);

        var drained = new List<int>();
        while (pq.TryExtractMin(out var bottom, out pq))
            drained.Add(bottom);

        Assert.Equal(values.OrderBy(v => v).ToArray(), drained);
    }

    /// <summary>Verifies empty priority queues report no maximum or minimum.</summary>
    [Fact]
    public void EmptyPriorityQueues_ReportNoExtremes()
    {
        Assert.False(FingerTree<int, Optional<int>, MaxMeasure<int>>.Empty.TryPeekMax(out _));
        Assert.False(FingerTree<int, Optional<int>, MaxMeasure<int>>.Empty.TryExtractMax(out _, out _));
        Assert.False(FingerTree<int, Optional<int>, MinMeasure<int>>.Empty.TryPeekMin(out _));
        Assert.False(FingerTree<int, Optional<int>, MinMeasure<int>>.Empty.TryExtractMin(out _, out _));
    }

    /// <summary>
    /// Verifies key-measured lower-bound and upper-bound splits match a binary-search model on a sorted
    /// sequence with duplicates.
    /// </summary>
    [Fact]
    public void KeyMeasure_LowerAndUpperBoundSplitsMatchModel()
    {
        var sorted = new[] { 1, 3, 3, 3, 7, 9, 9, 12 };
        var tree = FingerTree<int, Optional<int>, KeyMeasure<int>>.CreateRange(sorted);

        foreach (var key in new[] { 0, 1, 2, 3, 4, 9, 12, 13 })
        {
            var lower = Array.FindIndex(sorted, v => v >= key);
            if (lower < 0)
                lower = sorted.Length;
            var upper = Array.FindIndex(sorted, v => v > key);
            if (upper < 0)
                upper = sorted.Length;

            var (less, greaterOrEqual) = tree.SplitByLowerBound(key);
            Assert.Equal(sorted.Take(lower), less.ToArray());
            Assert.Equal(sorted.Skip(lower), greaterOrEqual.ToArray());

            var (lessOrEqual, greater) = tree.SplitByUpperBound(key);
            Assert.Equal(sorted.Take(upper), lessOrEqual.ToArray());
            Assert.Equal(sorted.Skip(upper), greater.ToArray());
        }
    }

    /// <summary>
    /// Verifies an order-statistic tree supports positional split by index AND ordered lower-bound split by
    /// key on the very same instance.
    /// </summary>
    [Fact]
    public void OrderStatisticMeasure_IsPositionalAndOrderedAtOnce()
    {
        var sorted = Enumerable.Range(0, 200).Select(v => v * 3).ToArray();
        var tree = FingerTree<int, RankedKey<int>, OrderStatisticMeasure<int>>.CreateRange(sorted);

        Assert.Equal(sorted.Length, tree.Measure.Count);
        Assert.Equal(sorted[^1], tree.Measure.Key.Value);

        for (var index = 0; index <= sorted.Length; index += 17)
        {
            var (left, right) = tree.SplitAtIndex(index);
            Assert.Equal(sorted.Take(index), left.ToArray());
            Assert.Equal(sorted.Skip(index), right.ToArray());
        }

        foreach (var key in new[] { -1, 0, 1, 3, 298, 299, 300, 598, 599 })
        {
            var expected = Array.FindIndex(sorted, v => v >= key);
            if (expected < 0)
                expected = sorted.Length;

            var (less, greaterOrEqual) = tree.SplitByLowerBound(key);
            Assert.Equal(sorted.Take(expected), less.ToArray());
            Assert.Equal(sorted.Skip(expected), greaterOrEqual.ToArray());
        }
    }

    /// <summary>
    /// Verifies the max measure honors a non-default natural order through reference types
    /// (<see cref="string"/>) via <see cref="Comparer{T}.Default"/>.
    /// </summary>
    [Fact]
    public void MaxMeasure_UsesDefaultComparerForReferenceTypes()
    {
        var tree = FingerTree<string, Optional<string>, MaxMeasure<string>>.Create("banana", "apple", "cherry");
        Assert.True(tree.TryPeekMax(out var max));
        Assert.Equal("cherry", max);
    }
}
