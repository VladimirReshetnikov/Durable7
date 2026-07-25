using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Exercises the public value-type-predicate ("zero closure") overloads of the product-measure named
/// operations the way an external caller would, asserting they produce results identical to the delegate
/// overloads. The delegate overloads are the known-correct oracle (already covered elsewhere); these tests
/// pin that the struct-predicate path is behaviorally equivalent.
/// </summary>
public sealed class ZeroClosureNamedOpTests
{
    private static readonly int[] Sample = [5, 1, 4, 2, 8, 3];   // running sums: 5, 6, 10, 12, 20, 23
    private static readonly int[] Thresholds = [-1, 0, 5, 6, 11, 19, 23, 100];

    /// <summary>A caller-supplied value-type predicate over the first (count) component.</summary>
    private readonly struct CountAbove(int index) : IMeasurePredicate<int>
    {
        public bool Invoke(int count) => count > index;
    }

    /// <summary>A caller-supplied value-type predicate over the second (sum) component.</summary>
    private readonly struct SumAbove(int threshold) : IMeasurePredicate<int>
    {
        public bool Invoke(int sum) => sum > threshold;
    }

    /// <summary>The struct-predicate first-component split matches the delegate overload at every index.</summary>
    [Fact]
    public void SplitByFirst_StructPredicate_MatchesDelegate()
    {
        var tree = ProductMeasures.CreateSizeAndSum(Sample);
        for (var index = 0; index <= Sample.Length; index++)
        {
            var (structLeft, structRight) = tree.SplitByFirst(new CountAbove(index));
            var (funcLeft, funcRight) = tree.SplitByFirst(count => count > index);
            Assert.Equal(funcLeft.ToArray(), structLeft.ToArray());
            Assert.Equal(funcRight.ToArray(), structRight.ToArray());
        }
    }

    /// <summary>The struct-predicate second-component split matches the delegate overload at every threshold.</summary>
    [Fact]
    public void SplitBySecond_StructPredicate_MatchesDelegate()
    {
        var tree = ProductMeasures.CreateSizeAndSum(Sample);
        foreach (var threshold in Thresholds)
        {
            var (structLeft, structRight) = tree.SplitBySecond(new SumAbove(threshold));
            var (funcLeft, funcRight) = tree.SplitBySecond(sum => sum > threshold);
            Assert.Equal(funcLeft.ToArray(), structLeft.ToArray());
            Assert.Equal(funcRight.ToArray(), structRight.ToArray());
        }
    }

    /// <summary>The struct-predicate first-component split-find matches the delegate overload at every index.</summary>
    [Fact]
    public void TrySplitFindByFirst_StructPredicate_MatchesDelegate()
    {
        var tree = ProductMeasures.CreateSizeAndSum(Sample);
        for (var index = 0; index <= Sample.Length; index++)
        {
            var structFound = tree.TrySplitFindByFirst(new CountAbove(index), out var sl, out var sHit, out var sr);
            var funcFound = tree.TrySplitFindByFirst(count => count > index, out var fl, out var fHit, out var fr);
            Assert.Equal(funcFound, structFound);
            if (funcFound)
            {
                Assert.Equal(fHit, sHit);
                Assert.Equal(fl.ToArray(), sl.ToArray());
                Assert.Equal(fr.ToArray(), sr.ToArray());
            }
        }
    }

    /// <summary>The struct-predicate second-component split-find matches the delegate overload at every threshold.</summary>
    [Fact]
    public void TrySplitFindBySecond_StructPredicate_MatchesDelegate()
    {
        var tree = ProductMeasures.CreateSizeAndSum(Sample);
        foreach (var threshold in Thresholds)
        {
            var structFound = tree.TrySplitFindBySecond(new SumAbove(threshold), out var sl, out var sHit, out var sr);
            var funcFound = tree.TrySplitFindBySecond(sum => sum > threshold, out var fl, out var fHit, out var fr);
            Assert.Equal(funcFound, structFound);
            if (funcFound)
            {
                Assert.Equal(fHit, sHit);
                Assert.Equal(fl.ToArray(), sl.ToArray());
                Assert.Equal(fr.ToArray(), sr.ToArray());
            }
        }
    }
}
