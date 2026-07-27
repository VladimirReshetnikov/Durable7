// Tests for the custom comparison measure.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies the custom-comparison measure variants and ordered-split overloads: ordering by a projection,
/// reversing an order, and that <see cref="DefaultComparison{T}"/> reproduces the non-parameterized measures.
/// </summary>
public sealed class CustomComparisonMeasureTests
{
    private readonly struct ByLength : IComparison<string>
    {
        /// <summary>Orders two values.</summary>
        public static int Compare(string left, string right) => left.Length.CompareTo(right.Length);
    }

    /// <summary>Verifies the reverse combinator inverts an order's sign.</summary>
    [Fact]
    public void ReverseComparison_InvertsOrder()
    {
        Assert.True(DefaultComparison<int>.Compare(1, 2) < 0);
        Assert.True(ReverseComparison<int, DefaultComparison<int>>.Compare(1, 2) > 0);
        Assert.Equal(0, ReverseComparison<int, DefaultComparison<int>>.Compare(5, 5));
    }

    /// <summary>
    /// Verifies a projection-ordered max priority queue drains longest-first, an order the default comparer
    /// would not produce.
    /// </summary>
    [Fact]
    public void MaxMeasureWithProjection_DrainsByProjectedKey()
    {
        var words = new[] { "a", "ccc", "bb", "dddd", "e", "ff" };
        var pq = FingerTree<string, Optional<string>, MaxMeasure<string, ByLength>>.CreateRange(words);

        Assert.True(pq.TryPeekMax(out var longest));
        Assert.Equal(4, longest.Length);

        var lengths = new List<int>();
        while (pq.TryExtractMax(out var word, out pq))
            lengths.Add(word.Length);

        Assert.Equal(words.Select(w => w.Length).OrderByDescending(n => n).ToArray(), lengths);
    }

    /// <summary>Verifies a min priority queue under a reversed comparison drains as a max queue would.</summary>
    [Fact]
    public void MinMeasureWithReversedComparison_DrainsLikeMax()
    {
        var values = new[] { 5, 1, 9, 3, 7, 2 };
        var pq = FingerTree<int, Optional<int>, MinMeasure<int, ReverseComparison<int, DefaultComparison<int>>>>
            .CreateRange(values);

        var drained = new List<int>();
        while (pq.TryExtractMin(out var v, out pq))
            drained.Add(v);

        // "Minimum under the reversed order" == ordinary maximum, so this drains in descending order.
        Assert.Equal(values.OrderByDescending(v => v).ToArray(), drained);
    }

    /// <summary>
    /// Verifies key-measured lower/upper-bound splits under a reversed comparison on a descending-sorted
    /// sequence match a descending-order binary-search model.
    /// </summary>
    [Fact]
    public void KeyMeasureWithReversedComparison_SearchesDescendingSequence()
    {
        var descending = new[] { 12, 9, 9, 7, 3, 3, 3, 1 };
        var tree = FingerTree<int, Optional<int>, KeyMeasure<int>>.CreateRange(descending);

        foreach (var key in new[] { 13, 12, 10, 9, 4, 3, 1, 0 })
        {
            // Under the reversed order, "lower bound" is the first element that is <= key (descending sort).
            var lower = Array.FindIndex(descending, v => v <= key);
            if (lower < 0)
                lower = descending.Length;
            var upper = Array.FindIndex(descending, v => v < key);
            if (upper < 0)
                upper = descending.Length;

            var (less, greaterOrEqual) = tree.SplitByLowerBound<int, ReverseComparison<int, DefaultComparison<int>>>(key);
            Assert.Equal(descending.Take(lower), less.ToArray());
            Assert.Equal(descending.Skip(lower), greaterOrEqual.ToArray());

            var (lessOrEqual, greater) = tree.SplitByUpperBound<int, ReverseComparison<int, DefaultComparison<int>>>(key);
            Assert.Equal(descending.Take(upper), lessOrEqual.ToArray());
            Assert.Equal(descending.Skip(upper), greater.ToArray());
        }
    }

    /// <summary>Verifies the default-comparison variant reproduces the non-parameterized max measure.</summary>
    [Fact]
    public void DefaultComparison_ReproducesNonParameterizedMeasure()
    {
        var values = new[] { 4, 8, 2, 8, 5 };
        var parameterized = FingerTree<int, Optional<int>, MaxMeasure<int, DefaultComparison<int>>>.CreateRange(values);
        var plain = FingerTree<int, Optional<int>, MaxMeasure<int>>.CreateRange(values);

        Assert.Equal(plain.Measure.Value, parameterized.Measure.Value);

        var a = new List<int>();
        while (parameterized.TryExtractMax(out var v, out parameterized))
            a.Add(v);
        var b = new List<int>();
        while (plain.TryExtractMax(out var v, out plain))
            b.Add(v);

        Assert.Equal(b, a);
    }

    /// <summary>
    /// Verifies an order-statistic tree honors a custom-comparison key lower bound while still splitting
    /// positionally by index.
    /// </summary>
    [Fact]
    public void OrderStatistic_CustomComparisonKeySplit()
    {
        // Descending-sorted by value; positional index is independent of the order.
        var descending = Enumerable.Range(0, 100).Select(v => (99 - v) * 2).ToArray();
        var tree = FingerTree<int, RankedKey<int>, OrderStatisticMeasure<int>>.CreateRange(descending);

        var (firstTen, _) = tree.SplitAtIndex(10);
        Assert.Equal(descending.Take(10), firstTen.ToArray());

        foreach (var key in new[] { 300, 100, 1, 0 })
        {
            var expected = Array.FindIndex(descending, v => v <= key);
            if (expected < 0)
                expected = descending.Length;

            var (less, greaterOrEqual) =
                tree.SplitByLowerBound<int, ReverseComparison<int, DefaultComparison<int>>>(key);
            Assert.Equal(descending.Take(expected), less.ToArray());
            Assert.Equal(descending.Skip(expected), greaterOrEqual.ToArray());
        }
    }
}
