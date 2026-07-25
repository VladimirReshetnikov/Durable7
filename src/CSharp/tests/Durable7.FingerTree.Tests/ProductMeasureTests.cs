using System.Numerics;
using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies the <see cref="ProductMeasure{TElement, TFirst, TSecond, TFirstOps, TSecondOps}"/> combinator: the
/// monoid laws of the product, the generic component-projecting splits, the named size+sum (Fenwick) and
/// size+max / size+min (priority-queue) operations against brute-force oracles, the construction factories,
/// and three-component nesting.
/// </summary>
public sealed class ProductMeasureTests
{
    // Convenient aliases for the three headline measure types.
    private static FingerTree<int, MeasurePair<int, int>, ProductMeasure<int, int, int, SizeMeasure<int>, SumMeasure<int>>>
        SizeSum(params int[] values) => ProductMeasures.CreateSizeAndSumRange(values);

    /// <summary>Verifies the product measure is the component-wise pair (count, sum), including the identity.</summary>
    [Fact]
    public void Measure_IsComponentWisePair()
    {
        Assert.Equal(new MeasurePair<int, int>(0, 0), ProductMeasures.EmptySizeAndSum<int>().Measure);

        var values = new[] { 3, 1, 4, 1, 5, 9 };
        var tree = SizeSum(values);
        Assert.Equal(new MeasurePair<int, int>(values.Length, values.Sum()), tree.Measure);
    }

    /// <summary>Verifies the product of two monoids satisfies identity and associativity component-wise.</summary>
    [Fact]
    public void ProductMonoid_SatisfiesLaws()
    {
        var random = new Random(1234);
        for (var trial = 0; trial < 200; trial++)
        {
            MeasurePair<int, int> Rand() => new(random.Next(-50, 50), random.Next(-50, 50));
            var a = Rand();
            var b = Rand();
            var c = Rand();
            var empty = ProductMeasure<int, int, int, SizeMeasure<int>, SumMeasure<int>>.Empty;

            // Identity (componentwise: size identity is 0, but Combine of the PAIR must be a two-sided identity).
            Assert.Equal(a, ProductMeasure<int, int, int, SizeMeasure<int>, SumMeasure<int>>.Combine(empty, a));
            Assert.Equal(a, ProductMeasure<int, int, int, SizeMeasure<int>, SumMeasure<int>>.Combine(a, empty));

            // Associativity.
            var leftFirst = ProductMeasure<int, int, int, SizeMeasure<int>, SumMeasure<int>>.Combine(
                ProductMeasure<int, int, int, SizeMeasure<int>, SumMeasure<int>>.Combine(a, b), c);
            var rightFirst = ProductMeasure<int, int, int, SizeMeasure<int>, SumMeasure<int>>.Combine(
                a, ProductMeasure<int, int, int, SizeMeasure<int>, SumMeasure<int>>.Combine(b, c));
            Assert.Equal(leftFirst, rightFirst);
        }
    }

    /// <summary>Verifies generic component projection splits match positional and prefix-sum models.</summary>
    [Fact]
    public void SplitByFirstAndSecond_ProjectOntoEachComponent()
    {
        var weights = new[] { 5, 2, 8, 1, 4, 7, 3 };
        var tree = SizeSum(weights);

        // First component = size: SplitByFirst(count => count > k) keeps the first k elements left.
        for (var k = 0; k <= weights.Length; k++)
        {
            var (left, right) = tree.SplitByFirst(count => count > k);
            Assert.Equal(weights.Take(k), left.ToArray());
            Assert.Equal(weights.Skip(k), right.ToArray());
            // Reconstruction.
            Assert.Equal(weights, left.Concat(right).ToArray());
        }

        // Second component = sum: SplitBySecond(sum => sum > t) matches the prefix-sum model.
        var total = weights.Sum();
        for (var t = -1; t <= total + 1; t++)
        {
            var running = 0;
            var prefix = 0;
            while (prefix < weights.Length && running + weights[prefix] <= t)
            {
                running += weights[prefix];
                prefix++;
            }

            var (left, right) = tree.SplitBySecond(sum => sum > t);
            Assert.Equal(weights.Take(prefix), left.ToArray());
            Assert.Equal(weights.Skip(prefix), right.ToArray());
        }
    }

    /// <summary>Verifies size+sum positional and cumulative-weight named splits, plus reconstruction.</summary>
    [Fact]
    public void SizeSum_NamedSplits_MatchModels()
    {
        var weights = new[] { 5, 2, 8, 1, 4 };
        var tree = SizeSum(weights);

        for (var index = 0; index <= weights.Length; index++)
        {
            var (left, right) = tree.SplitAtIndex(index);
            Assert.Equal(index, left.Measure.First);
            Assert.Equal(weights.Take(index), left.ToArray());
            Assert.Equal(weights.Skip(index), right.ToArray());
            Assert.Equal(weights.Take(index).Sum(), left.Measure.Second);
        }

        var total = weights.Sum();
        for (var threshold = 0; threshold <= total; threshold++)
        {
            var running = 0;
            var prefix = 0;
            while (prefix < weights.Length && running + weights[prefix] <= threshold)
            {
                running += weights[prefix];
                prefix++;
            }

            var (left, right) = tree.SplitByCumulativeWeight(threshold);
            Assert.Equal(weights.Take(prefix), left.ToArray());
            Assert.Equal(weights.Skip(prefix), right.ToArray());
        }
    }

    /// <summary>
    /// Verifies weighted selection on a size+sum tree picks the right element AND reports its index — the
    /// product's value-add over the pure sum measure — matching a brute-force scan.
    /// </summary>
    [Fact]
    public void SizeSum_WeightedSelection_ReportsElementAndIndex()
    {
        var weights = new[] { 5, 1, 4, 2, 3 };   // intervals: [0,5)[5,6)[6,10)[10,12)[12,15)
        var tree = SizeSum(weights);
        var total = weights.Sum();

        for (var threshold = 0; threshold < total; threshold++)
        {
            var cumulative = 0;
            var expectedIndex = 0;
            while (cumulative + weights[expectedIndex] <= threshold)
            {
                cumulative += weights[expectedIndex];
                expectedIndex++;
            }

            Assert.True(tree.TrySelectByCumulativeWeight(threshold, out var selected, out var indexBefore));
            Assert.Equal(weights[expectedIndex], selected);
            Assert.Equal(expectedIndex, indexBefore);
        }

        Assert.False(tree.TrySelectByCumulativeWeight(total, out _, out var none));
        Assert.Equal(0, none);
    }

    /// <summary>Verifies size+max gives O(1) peek, positional split, and descending extract-max drain.</summary>
    [Fact]
    public void SizeMax_PeekSplitAndDrain()
    {
        var values = new[] { 3, 9, 1, 9, 4, 7 };
        var tree = ProductMeasures.CreateSizeAndMaxRange(values);

        Assert.True(tree.TryPeekMax(out var max));
        Assert.Equal(values.Max(), max);
        Assert.Equal(values.Length, tree.Measure.First);

        // Positional split still works on the same tree.
        var (left, right) = tree.SplitByFirst(count => count > 2);
        Assert.Equal(values.Take(2), left.ToArray());
        Assert.Equal(values.Skip(2), right.ToArray());

        // Drain: extract-max repeatedly yields descending order (stable among ties).
        var drained = new List<int>();
        while (tree.TryExtractMax(out var top, out tree))
            drained.Add(top);
        Assert.Equal(values.OrderByDescending(v => v), drained);
        Assert.True(tree.IsEmpty);
        Assert.False(tree.TryPeekMax(out _));
    }

    /// <summary>Verifies size+min gives O(1) peek and ascending extract-min drain.</summary>
    [Fact]
    public void SizeMin_PeekAndDrain()
    {
        var values = new[] { 3, 9, 1, 9, 4, 7 };
        var tree = ProductMeasures.CreateSizeAndMinRange(values);

        Assert.True(tree.TryPeekMin(out var min));
        Assert.Equal(values.Min(), min);

        var drained = new List<int>();
        while (tree.TryExtractMin(out var bottom, out tree))
            drained.Add(bottom);
        Assert.Equal(values.OrderBy(v => v), drained);
        Assert.True(tree.IsEmpty);
        Assert.False(tree.TryPeekMin(out _));
    }

    /// <summary>Verifies the size+sum tree against an independent Fenwick (prefix-sum) oracle on random queries.</summary>
    [Fact]
    public void SizeSum_MatchesFenwickOracle()
    {
        var random = new Random(99);
        var weights = Enumerable.Range(0, 64).Select(_ => random.Next(1, 10)).ToArray();
        var tree = SizeSum(weights);

        // Prefix sums oracle.
        var prefix = new int[weights.Length + 1];
        for (var i = 0; i < weights.Length; i++)
            prefix[i + 1] = prefix[i] + weights[i];
        var total = prefix[^1];

        for (var trial = 0; trial < 100; trial++)
        {
            var threshold = random.Next(0, total);
            // Oracle: first index whose prefix sum strictly exceeds threshold is the selected element.
            var index = 0;
            while (prefix[index + 1] <= threshold)
                index++;

            Assert.True(tree.TrySelectByCumulativeWeight(threshold, out var selected, out var indexBefore));
            Assert.Equal(weights[index], selected);
            Assert.Equal(index, indexBefore);

            // Cumulative-weight split prefix sum equals the oracle prefix sum at that boundary.
            var (left, _) = tree.SplitByCumulativeWeight(threshold);
            Assert.Equal(prefix[index], left.Measure.Second);
            Assert.Equal(index, left.Measure.First);
        }
    }

    /// <summary>Verifies the factories build the same tree from spans and enumerables, and the empty tree.</summary>
    [Fact]
    public void Factories_BuildEquivalentTrees()
    {
        Assert.Equal(new[] { 1, 2, 3 }, ProductMeasures.CreateSizeAndSum(1, 2, 3).ToArray());
        Assert.Equal(new[] { 1, 2, 3 }, ProductMeasures.CreateSizeAndSumRange(new[] { 1, 2, 3 }).ToArray());
        Assert.True(ProductMeasures.EmptySizeAndSum<int>().IsEmpty);
        Assert.True(ProductMeasures.EmptySizeAndMax<int>().IsEmpty);
        Assert.True(ProductMeasures.EmptySizeAndMin<int>().IsEmpty);
    }

    /// <summary>Verifies the measure works across generic-math numeric types (double, BigInteger).</summary>
    [Fact]
    public void SizeSum_WorksAcrossNumericTypes()
    {
        var doubles = ProductMeasures.CreateSizeAndSum(1.5, 2.25, 0.25);
        Assert.Equal(3, doubles.Measure.First);
        Assert.Equal(4.0, doubles.Measure.Second, precision: 10);
        var (left, right) = doubles.SplitByCumulativeWeight(3.0);
        Assert.Equal(new[] { 1.5 }, left.ToArray());
        Assert.Equal(new[] { 2.25, 0.25 }, right.ToArray());

        var big = ProductMeasures.CreateSizeAndSum(BigInteger.Pow(10, 30), BigInteger.Pow(10, 30));
        Assert.Equal(new BigInteger(2) * BigInteger.Pow(10, 30), big.Measure.Second);
    }

    /// <summary>
    /// Verifies a three-component nested product (size + sum + max) is queryable on every axis through nested
    /// component access, demonstrating n-ary composition by nesting <see cref="MeasurePair{TFirst, TSecond}"/>.
    /// </summary>
    [Fact]
    public void NestedProduct_SizeSumMax_QueryableOnEveryAxis()
    {
        var values = new[] { 3, 9, 1, 9, 4, 7, 2 };
        var tree = FingerTree<int,
            MeasurePair<int, MeasurePair<int, Optional<int>>>,
            ProductMeasure<int, int, MeasurePair<int, Optional<int>>,
                SizeMeasure<int>,
                ProductMeasure<int, int, Optional<int>, SumMeasure<int>, MaxMeasure<int>>>>
            .CreateRange(values);

        var measure = tree.Measure;
        Assert.Equal(values.Length, measure.First);        // size
        Assert.Equal(values.Sum(), measure.Second.First);  // sum
        Assert.Equal(values.Max(), measure.Second.Second.Value); // max

        // Split by the outer first component (size).
        var (left, right) = tree.SplitByFirst(count => count > 3);
        Assert.Equal(values.Take(3), left.ToArray());
        Assert.Equal(values.Skip(3), right.ToArray());

        // Split by the nested sum component (m.Second.First) via the generic Split.
        var total = values.Sum();
        var runningPrefix = 0;
        var prefixLen = 0;
        while (prefixLen < values.Length && runningPrefix + values[prefixLen] <= total / 2)
        {
            runningPrefix += values[prefixLen];
            prefixLen++;
        }
        var (lo, hi) = tree.Split(m => m.Second.First > total / 2);
        Assert.Equal(values.Take(prefixLen), lo.ToArray());
        Assert.Equal(values.Skip(prefixLen), hi.ToArray());
    }

    /// <summary>Verifies argument-null guards on the projection operations and factories.</summary>
    [Fact]
    public void NullArguments_Throw()
    {
        var tree = SizeSum(1, 2, 3);
        Assert.Throws<ArgumentNullException>(() => tree.SplitByFirst(null!));
        Assert.Throws<ArgumentNullException>(() => tree.SplitBySecond(null!));
        Assert.Throws<ArgumentNullException>(() => ProductMeasures.CreateSizeAndSumRange<int>(null!));
        Assert.Throws<ArgumentNullException>(() => ProductMeasures.CreateSizeAndMaxRange<int>(null!));
    }
}
