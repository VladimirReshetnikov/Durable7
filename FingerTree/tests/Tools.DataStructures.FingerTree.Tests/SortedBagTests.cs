using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies <see cref="SortedBag{T}"/> as a sorted multiset against a <see cref="List{T}"/> model: sorted
/// order, ranking and order-statistic indexing, multiplicity, range extraction, removal of one versus all
/// equal elements, and custom comparers.
/// </summary>
public sealed class SortedBagTests
{
    /// <summary>Verifies construction and enumeration produce sorted order with duplicates preserved.</summary>
    [Fact]
    public void CreateRange_SortsAndKeepsDuplicates()
    {
        var bag = SortedBag<int>.CreateRange(new[] { 5, 1, 3, 3, 9, 1 });

        Assert.Equal(6, bag.Count);
        Assert.Equal(new[] { 1, 1, 3, 3, 5, 9 }, bag.ToArray());
        Assert.Equal(1, bag.Min);
        Assert.Equal(9, bag.Max);
    }

    /// <summary>Verifies ranking, multiplicity, and order-statistic indexing against a sorted list model.</summary>
    [Fact]
    public void RankCountAndIndexing_MatchSortedListModel()
    {
        var values = new[] { 4, 1, 4, 7, 1, 9, 4, 2 };
        var bag = SortedBag<int>.CreateRange(values);
        var model = values.OrderBy(v => v).ToArray();

        for (var i = 0; i < model.Length; i++)
            Assert.Equal(model[i], bag[i]);

        foreach (var key in new[] { 0, 1, 2, 4, 5, 9, 10 })
        {
            Assert.Equal(model.Count(v => v < key), bag.CountLessThan(key));
            Assert.Equal(model.Count(v => v <= key), bag.CountAtMost(key));
            Assert.Equal(model.Count(v => v == key), bag.CountOf(key));
            Assert.Equal(model.Contains(key), bag.Contains(key));
        }

        Assert.Throws<ArgumentOutOfRangeException>(() => bag[-1]);
        Assert.Throws<ArgumentOutOfRangeException>(() => bag[model.Length]);
    }

    /// <summary>Verifies removing one versus all equal elements.</summary>
    [Fact]
    public void RemoveOneVersusAll_BehaveCorrectly()
    {
        var bag = SortedBag<int>.CreateRange(new[] { 1, 3, 3, 3, 7 });

        Assert.True(bag.TryRemove(3, out var oneGone));
        Assert.Equal(new[] { 1, 3, 3, 7 }, oneGone.ToArray());

        Assert.False(bag.TryRemove(4, out var unchanged));
        Assert.Equal(bag.ToArray(), unchanged.ToArray());

        Assert.Equal(new[] { 1, 7 }, bag.RemoveAll(3).ToArray());
        Assert.Equal(bag.ToArray(), bag.RemoveAll(4).ToArray());
    }

    /// <summary>Verifies inclusive range extraction.</summary>
    [Fact]
    public void GetRange_ReturnsInclusiveBand()
    {
        var bag = SortedBag<int>.CreateRange(Enumerable.Range(0, 20).Select(v => v * 2));

        Assert.Equal(new[] { 6, 8, 10, 12 }, bag.GetRange(5, 12).ToArray());
        Assert.Equal(new[] { 0 }, bag.GetRange(-5, 0).ToArray());
        Assert.Empty(bag.GetRange(100, 200).ToArray());
        Assert.Empty(bag.GetRange(12, 5).ToArray());
    }

    /// <summary>Verifies a custom (reverse) comparer orders the bag descending and is preserved across operations.</summary>
    [Fact]
    public void CustomComparer_OrdersDescendingAndIsPreserved()
    {
        var descending = Comparer<int>.Create((a, b) => b.CompareTo(a));
        var bag = SortedBag<int>.CreateRange(new[] { 5, 1, 9, 3 }, descending);

        Assert.Equal(new[] { 9, 5, 3, 1 }, bag.ToArray());
        Assert.Equal(9, bag.Min);   // "smallest" under the reversed order is the numeric maximum
        Assert.Equal(1, bag.Max);
        Assert.Same(descending, bag.Comparer);

        var added = bag.Add(4);
        Assert.Equal(new[] { 9, 5, 4, 3, 1 }, added.ToArray());
        Assert.Same(descending, added.Comparer);
        Assert.Equal(2, added.CountLessThan(4));  // 9 and 5 precede 4 under the reversed order
    }

    /// <summary>Runs a deterministic mixed history against a list model.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    public void MixedHistory_MatchesListModel(int seed)
    {
        var random = new Random(seed);
        var model = new List<int>();
        var bag = SortedBag<int>.Empty;

        for (var step = 0; step < 300; step++)
        {
            switch (random.Next(4))
            {
                case 0:
                case 1:
                {
                    var value = random.Next(50);
                    model.Add(value);
                    bag = bag.Add(value);
                    break;
                }
                case 2 when model.Count > 0:
                {
                    var value = model[random.Next(model.Count)];
                    model.Remove(value);   // removes one occurrence
                    Assert.True(bag.TryRemove(value, out bag));
                    break;
                }
                default:
                {
                    var value = random.Next(50);
                    model.RemoveAll(v => v == value);
                    bag = bag.RemoveAll(value);
                    break;
                }
            }

            Assert.Equal(model.Count, bag.Count);
            Assert.Equal(model.OrderBy(v => v).ToArray(), bag.ToArray());
        }
    }

    /// <summary>Verifies the empty bag's degenerate behavior.</summary>
    [Fact]
    public void Empty_HasNoElements()
    {
        var empty = SortedBag<int>.Empty;
        Assert.True(empty.IsEmpty);
        Assert.Empty(empty);
        Assert.False(empty.Contains(1));
        Assert.Equal(0, empty.CountOf(1));
        Assert.Throws<InvalidOperationException>(() => empty.Min);
        Assert.Throws<InvalidOperationException>(() => empty.Max);
        Assert.False(empty.TryRemove(1, out _));
    }
}
