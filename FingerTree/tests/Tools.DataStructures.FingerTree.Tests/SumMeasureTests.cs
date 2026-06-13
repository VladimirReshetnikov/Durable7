using System.Numerics;
using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies the numeric sum measure and its weighted-selection operations against brute-force prefix-sum
/// models, across several generic-math numeric types.
/// </summary>
public sealed class SumMeasureTests
{
    /// <summary>Verifies the tree's measure is the running total, including the empty identity.</summary>
    [Fact]
    public void Measure_IsTheRunningTotal()
    {
        Assert.Equal(0, FingerTree<int, int, SumMeasure<int>>.Empty.Measure);

        var values = new[] { 3, 1, 4, 1, 5, 9, 2, 6 };
        var tree = FingerTree<int, int, SumMeasure<int>>.CreateRange(values);
        Assert.Equal(values.Sum(), tree.Measure);
    }

    /// <summary>
    /// Verifies cumulative-weight split matches the prefix-sum model: Left is the longest prefix whose total
    /// is at most the threshold.
    /// </summary>
    [Fact]
    public void SplitByCumulativeWeight_MatchesPrefixSumModel()
    {
        var weights = new[] { 5, 2, 8, 1, 4, 7, 3 };
        var tree = FingerTree<int, int, SumMeasure<int>>.CreateRange(weights);

        var total = weights.Sum();
        for (var threshold = -1; threshold <= total + 1; threshold++)
        {
            // Longest prefix whose running total is <= threshold.
            var running = 0;
            var prefixLength = 0;
            while (prefixLength < weights.Length && running + weights[prefixLength] <= threshold)
            {
                running += weights[prefixLength];
                prefixLength++;
            }

            var (left, right) = tree.SplitByCumulativeWeight(threshold);
            Assert.Equal(weights.Take(prefixLength), left.ToArray());
            Assert.Equal(weights.Skip(prefixLength), right.ToArray());
            Assert.Equal(weights.Take(prefixLength).Sum(), left.Measure);
        }
    }

    /// <summary>
    /// Verifies weighted selection picks the element whose cumulative interval contains the threshold, and
    /// reports the weight accumulated before it — matching a brute-force scan.
    /// </summary>
    [Fact]
    public void TrySelectByCumulativeWeight_MatchesBruteForce()
    {
        var weights = new[] { 5, 1, 4, 2, 3 };
        var tree = FingerTree<int, int, SumMeasure<int>>.CreateRange(weights);
        var total = weights.Sum();

        for (var threshold = 0; threshold < total; threshold++)
        {
            // Brute force: element whose interval [cumBefore, cumBefore + weight) contains threshold.
            var cumulative = 0;
            var expectedIndex = 0;
            while (cumulative + weights[expectedIndex] <= threshold)
            {
                cumulative += weights[expectedIndex];
                expectedIndex++;
            }

            Assert.True(tree.TrySelectByCumulativeWeight(threshold, out var selected, out var before));
            Assert.Equal(weights[expectedIndex], selected);
            Assert.Equal(cumulative, before);
        }

        // A threshold at or beyond the total selects nothing.
        Assert.False(tree.TrySelectByCumulativeWeight(total, out _, out var emptyBefore));
        Assert.Equal(0, emptyBefore);
    }

    /// <summary>
    /// Verifies a full weighted draw covers every element with frequency proportional to its weight (every
    /// threshold in <c>[0, total)</c> maps to the right element).
    /// </summary>
    [Fact]
    public void WeightedSelection_CoversEachElementProportionally()
    {
        var weights = new[] { 5, 1, 4 };   // intervals: [0,5) [5,6) [6,10)
        var tree = FingerTree<int, int, SumMeasure<int>>.CreateRange(weights);

        var counts = new int[weights.Length];
        for (var t = 0; t < weights.Sum(); t++)
        {
            Assert.True(tree.TrySelectByCumulativeWeight(t, out var selected, out var before));
            var index = before switch { 0 => 0, 5 => 1, _ => 2 };
            counts[index]++;
            Assert.Equal(weights[index], selected);
        }

        Assert.Equal(weights, counts);   // each element selected exactly weight-many times
    }

    /// <summary>Verifies the measure works over other generic-math numeric types (double and BigInteger).</summary>
    [Fact]
    public void Works_AcrossNumericTypes()
    {
        var doubles = FingerTree<double, double, SumMeasure<double>>.Create(1.5, 2.25, 0.25);
        Assert.Equal(4.0, doubles.Measure, precision: 10);
        var (doubleLeft, doubleRight) = doubles.SplitByCumulativeWeight(3.0); // 1.5 <= 3 < 1.5 + 2.25
        Assert.Equal(new[] { 1.5 }, doubleLeft.ToArray());
        Assert.Equal(new[] { 2.25, 0.25 }, doubleRight.ToArray());

        var big = FingerTree<BigInteger, BigInteger, SumMeasure<BigInteger>>.Create(
            BigInteger.Pow(10, 30), BigInteger.Pow(10, 30));
        Assert.Equal(new BigInteger(2) * BigInteger.Pow(10, 30), big.Measure);
    }

    /// <summary>Verifies empty/degenerate behavior.</summary>
    [Fact]
    public void Empty_SelectsNothing()
    {
        var empty = FingerTree<int, int, SumMeasure<int>>.Empty;
        Assert.False(empty.TrySelectByCumulativeWeight(0, out _, out var before));
        Assert.Equal(0, before);
        var (left, right) = empty.SplitByCumulativeWeight(5);
        Assert.True(left.IsEmpty);
        Assert.True(right.IsEmpty);
    }
}
