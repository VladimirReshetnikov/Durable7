// Tests for the brodal okasaki heap.

using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Correctness, persistence, melding, and operation-bound coverage for <see cref="BrodalOkasakiHeap{T}"/>.</summary>
public sealed class BrodalOkasakiHeapTests
{
    /// <summary>Checks sorted draining across insertion patterns and duplicates.</summary>
    [Theory]
    [InlineData(1)]
    [InlineData(10)]
    [InlineData(1_000)]
    [InlineData(100_000)]
    public void InsertAndDeleteMinimum_ProduceSortedOrder(int count)
    {
        var random = new Random(20260718 + count);
        var values = Enumerable.Range(0, count).Select(_ => random.Next(count / 2 + 1)).ToArray();
        var heap = BrodalOkasakiHeap<int>.CreateRange(values);

        Assert.Equal(count, heap.Count);
        Assert.Equal(values.Order(), Drain(heap));
        Assert.Equal(values.Order(), heap.Order());
    }

    /// <summary>Checks repeated random meld trees against one sorted model.</summary>
    [Fact]
    public void Meld_RandomizedHeapForestMatchesModel()
    {
        var random = new Random(20260719);
        var heaps = new List<BrodalOkasakiHeap<int>>();
        var all = new List<int>();
        for (var i = 0; i < 1_000; i++)
        {
            var values = Enumerable.Range(0, random.Next(0, 100)).Select(_ => random.Next()).ToArray();
            heaps.Add(BrodalOkasakiHeap<int>.CreateRange(values));
            all.AddRange(values);
        }
        while (heaps.Count > 1)
        {
            var index = random.Next(heaps.Count - 1);
            heaps[index] = heaps[index].Meld(heaps[index + 1]);
            heaps.RemoveAt(index + 1);
        }

        Assert.Equal(all.Order(), Drain(heaps[0]));
    }

    /// <summary>Verifies insert and meld perform a size-independent bounded number of comparisons.</summary>
    [Fact]
    public void InsertAndMeld_HaveWorstCaseConstantComparisonCounts()
    {
        var comparer = new CountingComparer();
        var left = BrodalOkasakiHeap<int>.Create(comparer);
        var right = BrodalOkasakiHeap<int>.Create(comparer);
        for (var i = 0; i < 100_000; i++)
        {
            comparer.Reset();
            left = left.Insert(i * 2);
            Assert.InRange(comparer.Count, 0, 5);
            right = right.Insert(i * 2 + 1);
        }

        comparer.Reset();
        var melded = left.Meld(right);
        Assert.InRange(comparer.Count, 1, 5);
        Assert.Equal(200_000, melded.Count);
    }

    /// <summary>Verifies retained versions, empty behavior, and comparer identity gating.</summary>
    [Fact]
    public void PersistenceAndPolicyContracts()
    {
        var comparer = Comparer<int>.Create((left, right) => left.CompareTo(right));
        var empty = BrodalOkasakiHeap<int>.Create(comparer);
        Assert.False(empty.TryGetMinimum(out _));
        Assert.False(empty.TryDeleteMinimum(out _, out var unchanged));
        Assert.Same(empty, unchanged);
        Assert.Throws<InvalidOperationException>(() => empty.Minimum);
        Assert.Throws<InvalidOperationException>(empty.DeleteMinimum);

        var one = empty.Insert(10);
        var two = one.Insert(5);
        Assert.Equal(10, one.Minimum);
        Assert.Equal(5, two.Minimum);
        Assert.True(two.TryDeleteMinimum(out var minimum, out var remainder));
        Assert.Equal(5, minimum);
        Assert.Equal(10, remainder.Minimum);

        Assert.Same(two.RootIdentity, two.Meld(empty).RootIdentity);
        var incompatible = BrodalOkasakiHeap<int>.Create(Comparer<int>.Create((l, r) => l.CompareTo(r))).Insert(1);
        Assert.Throws<ArgumentException>(() => two.Meld(incompatible));
    }

    private static int[] Drain(BrodalOkasakiHeap<int> heap)
    {
        var result = new int[heap.Count];
        for (var i = 0; i < result.Length; i++)
        {
            Assert.True(heap.TryDeleteMinimum(out result[i], out heap));
        }
        Assert.True(heap.IsEmpty);
        return result;
    }

    private sealed class CountingComparer : IComparer<int>
    {
        public int Count { get; private set; }

        public int Compare(int x, int y)
        {
            Count++;
            return x.CompareTo(y);
        }

        public void Reset() => Count = 0;
    }
}
