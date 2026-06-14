using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies the allocation-free <see cref="FingerTree{TElement, TMeasure, TMeasureOps}.TryLocate"/> read
/// primitive agrees with the tree-building <c>TrySplitFind</c> on the boundary element and the measure
/// accumulated before it, across every threshold and the no-boundary case — so the sorted and order-statistic
/// collections that now route their reads through it keep identical results.
/// </summary>
public sealed class TryLocateTests
{
    /// <summary>Verifies TryLocate matches TrySplitFind's found element and left measure at every count threshold.</summary>
    [Fact]
    public void Locate_MatchesSplitFind_ByCount()
    {
        for (var size = 0; size <= 64; size++)
        {
            var tree = FingerTree<int, int, SizeMeasure<int>>.CreateRange(Enumerable.Range(0, size));
            for (var threshold = -1; threshold <= size + 1; threshold++)
            {
                var located = tree.TryLocate(m => m > threshold, out var measureBefore, out var found);
                var split = tree.TrySplitFind(m => m > threshold, out var left, out var splitFound, out _);

                Assert.Equal(split, located);
                if (split)
                {
                    Assert.Equal(splitFound, found);
                    Assert.Equal(left.Measure, measureBefore);
                }
                else
                {
                    // No boundary: measureBefore reports the whole-tree measure (full count).
                    Assert.Equal(tree.Measure, measureBefore);
                }
            }
        }
    }

    /// <summary>Verifies TryLocate matches TrySplitFind for an ordered key search (lower bound) over a non-group measure.</summary>
    [Fact]
    public void Locate_MatchesSplitFind_ByKey()
    {
        var random = new Random(7);
        var values = Enumerable.Range(0, 200).Select(_ => random.Next(0, 100)).OrderBy(x => x).ToArray();
        var tree = FingerTree<int, RankedKey<int>, OrderStatisticMeasure<int>>.CreateRange(values);
        var comparer = Comparer<int>.Default;

        for (var key = -1; key <= 101; key++)
        {
            var k = key;
            bool Pred(RankedKey<int> m) => m.Key.HasValue && comparer.Compare(m.Key.Value, k) >= 0;

            var located = tree.TryLocate(Pred, out var measureBefore, out var found);
            var split = tree.TrySplitFind(Pred, out var left, out var splitFound, out _);

            Assert.Equal(split, located);
            if (split)
            {
                Assert.Equal(splitFound, found);
                Assert.Equal(left.Measure.Count, measureBefore.Count);
            }
        }
    }

    /// <summary>Verifies the degenerate cases: empty tree and a predicate satisfied by the very first element.</summary>
    [Fact]
    public void Locate_HandlesEdges()
    {
        var empty = FingerTree<int, int, SizeMeasure<int>>.Empty;
        Assert.False(empty.TryLocate(m => m > 0, out var emptyBefore, out _));
        Assert.Equal(0, emptyBefore);

        var single = FingerTree<int, int, SizeMeasure<int>>.Create(42);
        Assert.True(single.TryLocate(m => m > 0, out var before, out var found));
        Assert.Equal(0, before);
        Assert.Equal(42, found);
    }
}
