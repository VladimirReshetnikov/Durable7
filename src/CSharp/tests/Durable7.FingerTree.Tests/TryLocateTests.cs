using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

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

    /// <summary>A user-defined non-capturing struct predicate, the public zero-allocation locate API.</summary>
    private readonly struct AtLeastSum(int threshold) : IMeasurePredicate<int>
    {
        public bool Invoke(int runningTotal) => runningTotal >= threshold;
    }

    /// <summary>
    /// Verifies the public generic <see cref="FingerTree{TElement, TMeasure, TMeasureOps}.TryLocate{TPredicate}"/>
    /// works with a caller-supplied struct predicate (matching the documented example) and allocates nothing.
    /// </summary>
    [Fact]
    public void PublicStructPredicate_LocatesAndAllocatesNothing()
    {
        var tree = FingerTree<int, int, SumMeasure<int>>.Create(5, 1, 4, 2);

        Assert.True(tree.TryLocate(new AtLeastSum(7), out var before, out var element));
        Assert.Equal(4, element);    // 5 + 1 + 4 = 10 first reaches 7 at the third element
        Assert.Equal(6, before);     // summed measure to its left

        Assert.False(tree.TryLocate(new AtLeastSum(100), out var none, out _));
        Assert.Equal(12, none);      // whole-tree measure when the predicate is never satisfied

        // The struct-predicate overload allocates nothing once the spine measures are memoized.
        var large = FingerTree<int, int, SumMeasure<int>>.CreateRange(Enumerable.Range(1, 1 << 14));
        for (var i = 0; i < 50; i++)
            large.TryLocate(new AtLeastSum(i * 1000), out _, out _);   // warm up
        var beforeBytes = GC.GetAllocatedBytesForCurrentThread();
        var sink = 0;
        for (var i = 0; i < 1000; i++)
            if (large.TryLocate(new AtLeastSum(i * 100), out _, out var hit)) sink += hit;
        Assert.InRange(GC.GetAllocatedBytesForCurrentThread() - beforeBytes, 0, 256);
        Assert.True(sink != int.MinValue);
    }

    /// <summary>
    /// Verifies the public generic struct-predicate <c>Split</c>/<c>TrySplitFind</c> overloads produce exactly
    /// the same halves and boundary element as the delegate overloads, across every threshold.
    /// </summary>
    [Fact]
    public void StructPredicateSplit_MatchesDelegateSplit()
    {
        var tree = FingerTree<int, int, SizeMeasure<int>>.CreateRange(Enumerable.Range(0, 60));

        for (var k = -1; k <= 61; k++)
        {
            var (fl, fr) = tree.Split(m => m >= k);
            var (sl, sr) = tree.Split(new AtLeastSum(k));
            Assert.Equal(fl.ToArray(), sl.ToArray());
            Assert.Equal(fr.ToArray(), sr.ToArray());

            var delegateFound = tree.TrySplitFind(m => m >= k, out var fLeft, out var fHit, out var fRight);
            var structFound = tree.TrySplitFind(new AtLeastSum(k), out var sLeft, out var sHit, out var sRight);
            Assert.Equal(delegateFound, structFound);
            if (delegateFound)
            {
                Assert.Equal(fHit, sHit);
                Assert.Equal(fLeft.ToArray(), sLeft.ToArray());
                Assert.Equal(fRight.ToArray(), sRight.ToArray());
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
