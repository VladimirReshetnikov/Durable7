using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies the general measured finger tree across several measures: element-count (positional sequence),
/// maximum-priority (priority queue), and maximum-key (ordered set). The monotone-predicate
/// <see cref="FingerTree{TElement, TMeasure, TMeasureOps}.Split(System.Func{TMeasure, bool})"/> is the
/// single operation exercised in all three guises.
/// </summary>
public sealed class MeasuredFingerTreeTests
{
    private readonly struct MaxMeasure : IMeasure<int, int>
    {
        public static int Empty => int.MinValue;

        public static int Combine(int left, int right) => Math.Max(left, right);

        public static int Measure(int element) => element;
    }

    private readonly record struct SumKey(int Count, int LastKey);

    private readonly struct LastKeyMeasure : IMeasure<int, SumKey>
    {
        public static SumKey Empty => new(0, int.MinValue);

        public static SumKey Combine(SumKey left, SumKey right) =>
            new(left.Count + right.Count, right.Count == 0 ? left.LastKey : right.LastKey);

        public static SumKey Measure(int element) => new(1, element);
    }

    /// <summary>
    /// Verifies an empty tree reports the monoid identity measure and degenerate views/splits.
    /// </summary>
    [Fact]
    public void Empty_ReportsIdentityMeasureAndDegenerateOperations()
    {
        var empty = FingerTree<int, int, SizeMeasure<int>>.Empty;

        Assert.True(empty.IsEmpty);
        Assert.Equal(0, empty.Measure);
        Assert.Throws<InvalidOperationException>(() => empty.First);
        Assert.Throws<InvalidOperationException>(() => empty.Last);
        Assert.False(empty.TryViewLeft(out _, out var afterLeft));
        Assert.Same(FingerTree<int, int, SizeMeasure<int>>.Empty, afterLeft);
        Assert.False(empty.TryViewRight(out _, out _));

        var (left, right) = empty.Split(m => m > 0);
        Assert.True(left.IsEmpty);
        Assert.True(right.IsEmpty);
        Assert.False(empty.TrySplitFind(m => m > 0, out _, out _, out _));
    }

    /// <summary>
    /// Verifies the element-count measure makes the tree a positional sequence whose measure is its length,
    /// across endpoint construction from both sides.
    /// </summary>
    [Fact]
    public void SizeMeasure_TracksLengthUnderPrependAndAppend()
    {
        var tree = FingerTree<int, int, SizeMeasure<int>>.Empty;
        for (var i = 0; i < 100; i++)
            tree = i % 2 == 0 ? tree.Append(i) : tree.Prepend(i);

        Assert.Equal(100, tree.Measure);
        Assert.Equal(100, tree.ToArray().Length);
    }

    /// <summary>
    /// Verifies the public enumerator walks the measured tree incrementally instead of flattening every element
    /// into a temporary list before yielding the first item.
    /// </summary>
    [Fact]
    public void Enumerator_UsesBoundedTraversalStackBeforeFirstYield()
    {
        const int size = 1 << 15;
        var tree = FingerTree<int, int, SizeMeasure<int>>.CreateRange(Enumerable.Range(0, size));

        Assert.Equal(Enumerable.Range(0, size), tree);

        // Warm up JIT and the exact first-yield path outside the measured window.
        var warmup = tree.GetEnumerator();
        Assert.True(warmup.MoveNext());
        Assert.Equal(0, warmup.Current);

        var before = GC.GetAllocatedBytesForCurrentThread();
        var enumerator = tree.GetEnumerator();
        Assert.True(enumerator.MoveNext());
        Assert.Equal(0, enumerator.Current);
        var bytes = GC.GetAllocatedBytesForCurrentThread() - before;

        Assert.InRange(bytes, 1, 16 * 1024);
    }

    /// <summary>
    /// Verifies positional split via the size measure matches list slicing at every index for many sizes,
    /// and that the two halves concatenate back to the original.
    /// </summary>
    [Fact]
    public void SizeMeasure_SplitMatchesListSliceAtEveryIndex()
    {
        for (var size = 0; size <= 130; size++)
        {
            var values = Enumerable.Range(0, size).ToArray();
            var tree = FingerTree<int, int, SizeMeasure<int>>.CreateRange(values);
            Assert.Equal(values, tree.ToArray());

            for (var index = 0; index <= size; index++)
            {
                var (left, right) = tree.Split(measured => measured > index);
                Assert.Equal(values.Take(index), left.ToArray());
                Assert.Equal(values.Skip(index), right.ToArray());
                Assert.Equal(values, left.Concat(right).ToArray());
            }
        }
    }

    /// <summary>
    /// Verifies concatenation preserves order and measure across a spread of operand sizes, and is
    /// associative at the sequence level.
    /// </summary>
    [Fact]
    public void Concat_PreservesOrderMeasureAndIsAssociative()
    {
        int[] sizes = [0, 1, 2, 5, 13, 40];
        foreach (var a in sizes)
        {
            foreach (var b in sizes)
            {
                foreach (var c in sizes)
                {
                    var ta = FingerTree<int, int, SizeMeasure<int>>.CreateRange(Enumerable.Range(0, a));
                    var tb = FingerTree<int, int, SizeMeasure<int>>.CreateRange(Enumerable.Range(100, b));
                    var tc = FingerTree<int, int, SizeMeasure<int>>.CreateRange(Enumerable.Range(200, c));

                    var left = ta.Concat(tb).Concat(tc);
                    var right = ta.Concat(tb.Concat(tc));

                    var expected = Enumerable.Range(0, a)
                        .Concat(Enumerable.Range(100, b))
                        .Concat(Enumerable.Range(200, c))
                        .ToArray();

                    Assert.Equal(expected, left.ToArray());
                    Assert.Equal(expected, right.ToArray());
                    Assert.Equal(a + b + c, left.Measure);
                }
            }
        }
    }

    /// <summary>
    /// Verifies the maximum-priority measure turns the tree into a mergeable priority queue: the measure is
    /// the maximum, and repeatedly extracting an occurrence of the maximum yields the elements in
    /// nonincreasing order.
    /// </summary>
    [Fact]
    public void MaxMeasure_ActsAsMergeablePriorityQueue()
    {
        var random = new Random(20260613);
        var values = Enumerable.Range(0, 200).Select(_ => random.Next(1000)).ToArray();
        var queue = FingerTree<int, int, MaxMeasure>.CreateRange(values);

        Assert.Equal(values.Max(), queue.Measure);

        var extracted = new List<int>();
        while (!queue.IsEmpty)
        {
            var max = queue.Measure;
            Assert.True(queue.TrySplitFind(m => m >= max, out var before, out var found, out var after));
            Assert.Equal(max, found);
            extracted.Add(found);
            queue = before.Concat(after);
        }

        var expected = values.OrderByDescending(v => v).ToArray();
        Assert.Equal(expected, extracted);
    }

    /// <summary>
    /// Verifies the last-key product measure supports both order-statistic indexing (the count component)
    /// and ordered lower-bound search (the key component) on a sorted sequence.
    /// </summary>
    [Fact]
    public void ProductMeasure_SupportsIndexingAndOrderedSearch()
    {
        var sorted = Enumerable.Range(0, 300).Select(v => v * 2).ToArray();
        var tree = FingerTree<int, SumKey, LastKeyMeasure>.CreateRange(sorted);

        Assert.Equal(sorted.Length, tree.Measure.Count);
        Assert.Equal(sorted[^1], tree.Measure.LastKey);

        // Order-statistic: split by the count component to get a positional split.
        var (firstTen, rest) = tree.Split(m => m.Count > 10);
        Assert.Equal(sorted.Take(10), firstTen.ToArray());
        Assert.Equal(sorted.Skip(10), rest.ToArray());

        // Ordered lower bound: first element with key >= target via the key component.
        foreach (var target in new[] { -5, 0, 1, 199, 200, 598, 599, 600 })
        {
            var expectedIndex = Array.FindIndex(sorted, key => key >= target);
            if (expectedIndex < 0)
                expectedIndex = sorted.Length;

            var (less, greaterOrEqual) = tree.Split(m => m.LastKey >= target);
            Assert.Equal(sorted.Take(expectedIndex), less.ToArray());
            Assert.Equal(sorted.Skip(expectedIndex), greaterOrEqual.ToArray());
        }
    }

    /// <summary>
    /// Verifies views and array conversion agree with a list model under a deterministic mixed history of
    /// endpoint insertions, endpoint removals, and split/concat round-trips.
    /// </summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    public void MixedHistory_MatchesListModel(int seed)
    {
        var random = new Random(seed);
        var model = new List<int>();
        var tree = FingerTree<int, int, SizeMeasure<int>>.Empty;

        for (var step = 0; step < 200; step++)
        {
            switch (random.Next(6))
            {
                case 0:
                    model.Insert(0, step);
                    tree = tree.Prepend(step);
                    break;
                case 1:
                    model.Add(step);
                    tree = tree.Append(step);
                    break;
                case 2 when model.Count > 0:
                    model.RemoveAt(0);
                    Assert.True(tree.TryViewLeft(out _, out tree));
                    break;
                case 3 when model.Count > 0:
                    model.RemoveAt(model.Count - 1);
                    Assert.True(tree.TryViewRight(out _, out tree));
                    break;
                default:
                {
                    var index = random.Next(model.Count + 1);
                    var (left, right) = tree.Split(m => m > index);
                    tree = left.Concat(right);
                    break;
                }
            }

            Assert.Equal(model.Count, tree.Measure);
            Assert.Equal(model, tree.ToArray());
        }
    }
}
