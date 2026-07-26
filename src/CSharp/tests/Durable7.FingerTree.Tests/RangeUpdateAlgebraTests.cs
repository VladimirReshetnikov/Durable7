// Tests for the range update algebra.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Executable laws for the affine tag monoid and its action on elements and ordered measures.</summary>
public sealed class RangeUpdateAlgebraTests
{
    private static readonly RangeUpdateTag[] Tags =
    [
        RangeUpdateTag.Identity,
        RangeUpdateTag.DistinctIdentity(17),
        RangeUpdateTag.DistinctIdentity(91),
        RangeUpdateTag.Add(-3),
        RangeUpdateTag.Add(4),
        RangeUpdateTag.Assign(-2),
        RangeUpdateTag.Assign(5),
        RangeUpdateTag.ScaleAndAdd(2, 1),
    ];

    /// <summary>Checks identity recognition, two-sided identity, and associativity for every small tag triple.</summary>
    [Fact]
    public void TagMonoid_LawsHoldExhaustively()
    {
        Assert.True(RangeUpdateAffineAlgebra.IsIdentity(RangeUpdateAffineAlgebra.IdentityTag));
        Assert.True(RangeUpdateAffineAlgebra.IsIdentity(RangeUpdateTag.DistinctIdentity(17)));
        Assert.NotEqual(RangeUpdateAffineAlgebra.IdentityTag, RangeUpdateTag.DistinctIdentity(17));

        foreach (var tag in Tags)
        {
            AssertTagsEquivalent(tag, RangeUpdateAffineAlgebra.Compose(RangeUpdateTag.Identity, tag));
            AssertTagsEquivalent(tag, RangeUpdateAffineAlgebra.Compose(tag, RangeUpdateTag.Identity));
        }

        foreach (var older in Tags)
        {
            foreach (var middle in Tags)
            {
                foreach (var newer in Tags)
                {
                    AssertTagsEquivalent(
                        RangeUpdateAffineAlgebra.Compose(
                            newer,
                            RangeUpdateAffineAlgebra.Compose(middle, older)),
                        RangeUpdateAffineAlgebra.Compose(
                            RangeUpdateAffineAlgebra.Compose(newer, middle),
                            older));
                }
            }
        }
    }

    /// <summary>
    /// Locks the named composition direction: <c>Compose(newer, older)</c> applies the older tag first.
    /// </summary>
    [Fact]
    public void Compose_AppliesOlderThenNewer()
    {
        var addThenAssign = RangeUpdateAffineAlgebra.Compose(
            RangeUpdateTag.Assign(7),
            RangeUpdateTag.Add(3));
        var assignThenAdd = RangeUpdateAffineAlgebra.Compose(
            RangeUpdateTag.Add(3),
            RangeUpdateTag.Assign(7));

        Assert.Equal(7, RangeUpdateAffineAlgebra.ApplyElement(addThenAssign, 100));
        Assert.Equal(10, RangeUpdateAffineAlgebra.ApplyElement(assignThenAdd, 100));
        Assert.NotEqual(addThenAssign, assignThenAdd);

        foreach (var element in Enumerable.Range(-10, 21))
        {
            foreach (var older in Tags)
            {
                foreach (var newer in Tags)
                {
                    var composed = RangeUpdateAffineAlgebra.Compose(newer, older);
                    Assert.Equal(
                        RangeUpdateAffineAlgebra.ApplyElement(
                            newer,
                            RangeUpdateAffineAlgebra.ApplyElement(older, element)),
                        RangeUpdateAffineAlgebra.ApplyElement(composed, element));
                }
            }
        }
    }

    /// <summary>Checks element/measure consistency, action composition, empty preservation, and distribution.</summary>
    [Fact]
    public void TagAction_LawsHoldOverOrderedMeasures()
    {
        var samples = EnumerateSamples(maximumLength: 4, minimumValue: -2, maximumValue: 2).ToArray();

        foreach (var sample in samples)
        {
            var measure = RangeUpdateAssert.Fold(sample);
            Assert.Equal(measure, RangeUpdateAffineAlgebra.ApplyMeasure(
                RangeUpdateTag.DistinctIdentity(123),
                measure,
                sample.Length));

            foreach (var tag in Tags)
            {
                Assert.Equal(
                    RangeUpdateAssert.Fold(sample.Select(value =>
                        RangeUpdateAffineAlgebra.ApplyElement(tag, value))),
                    RangeUpdateAffineAlgebra.ApplyMeasure(tag, measure, sample.Length));

                Assert.Equal(
                    RangeUpdateAffineAlgebra.Empty,
                    RangeUpdateAffineAlgebra.ApplyMeasure(tag, RangeUpdateAffineAlgebra.Empty, 0));

                for (var split = 0; split <= sample.Length; split++)
                {
                    var left = RangeUpdateAssert.Fold(sample.Take(split));
                    var right = RangeUpdateAssert.Fold(sample.Skip(split));
                    Assert.Equal(
                        RangeUpdateAffineAlgebra.ApplyMeasure(
                            tag,
                            RangeUpdateAffineAlgebra.Combine(left, right),
                            sample.Length),
                        RangeUpdateAffineAlgebra.Combine(
                            RangeUpdateAffineAlgebra.ApplyMeasure(tag, left, split),
                            RangeUpdateAffineAlgebra.ApplyMeasure(tag, right, sample.Length - split)));
                }
            }

            foreach (var older in Tags)
            {
                foreach (var newer in Tags)
                {
                    Assert.Equal(
                        RangeUpdateAffineAlgebra.ApplyMeasure(
                            newer,
                            RangeUpdateAffineAlgebra.ApplyMeasure(older, measure, sample.Length),
                            sample.Length),
                        RangeUpdateAffineAlgebra.ApplyMeasure(
                            RangeUpdateAffineAlgebra.Compose(newer, older),
                            measure,
                            sample.Length));
                }
            }
        }

        foreach (var element in Enumerable.Range(-5, 11))
        {
            foreach (var tag in Tags)
            {
                Assert.Equal(
                    RangeUpdateAffineAlgebra.Measure(
                        RangeUpdateAffineAlgebra.ApplyElement(tag, element)),
                    RangeUpdateAffineAlgebra.ApplyMeasure(
                        tag,
                        RangeUpdateAffineAlgebra.Measure(element),
                        1));
            }
        }
    }

    /// <summary>Proves the primary test measure is genuinely ordered and noncommutative.</summary>
    [Fact]
    public void OrderedMeasure_IsAssociativeButNotCommutative()
    {
        var first = RangeUpdateAffineAlgebra.Measure(2);
        var second = RangeUpdateAffineAlgebra.Measure(11);
        var third = RangeUpdateAffineAlgebra.Measure(-3);

        Assert.NotEqual(
            RangeUpdateAffineAlgebra.Combine(first, second),
            RangeUpdateAffineAlgebra.Combine(second, first));
        Assert.Equal(
            RangeUpdateAffineAlgebra.Combine(
                RangeUpdateAffineAlgebra.Combine(first, second),
                third),
            RangeUpdateAffineAlgebra.Combine(
                first,
                RangeUpdateAffineAlgebra.Combine(second, third)));
        Assert.Equal(
            new RangeUpdateMeasure(3, 10, 5),
            RangeUpdateAssert.Fold([2, 11, -3]));
    }

    private static IEnumerable<int[]> EnumerateSamples(
        int maximumLength,
        int minimumValue,
        int maximumValue)
    {
        yield return [];
        var radix = maximumValue - minimumValue + 1;
        for (var length = 1; length <= maximumLength; length++)
        {
            var count = (int)Math.Pow(radix, length);
            for (var encoded = 0; encoded < count; encoded++)
            {
                var value = encoded;
                var sample = new int[length];
                for (var index = 0; index < length; index++)
                {
                    sample[index] = minimumValue + (value % radix);
                    value /= radix;
                }

                yield return sample;
            }
        }
    }

    private static void AssertTagsEquivalent(RangeUpdateTag expected, RangeUpdateTag actual)
    {
        foreach (var element in Enumerable.Range(-7, 15))
        {
            Assert.Equal(
                RangeUpdateAffineAlgebra.ApplyElement(expected, element),
                RangeUpdateAffineAlgebra.ApplyElement(actual, element));
        }

        var measure = RangeUpdateAssert.Fold([-3, 1, 4, -1, 5]);
        Assert.Equal(
            RangeUpdateAffineAlgebra.ApplyMeasure(expected, measure, measure.Count),
            RangeUpdateAffineAlgebra.ApplyMeasure(actual, measure, measure.Count));
    }
}
