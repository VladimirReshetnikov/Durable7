// Tests for the DABA Lite lite.

using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Correctness and worst-case combine-count coverage for <see cref="DabaLite{T,TMonoid}"/>.</summary>
public sealed class DabaLiteTests
{
    /// <summary>Checks a noncommutative monoid across the paper's incremental reversal cases.</summary>
    [Fact]
    public void StringConcatenation_PreservesFifoOrder()
    {
        var daba = new DabaLite<string, StringMonoid>();
        var model = new Queue<string>();
        for (var i = 0; i < 10_000; i++)
        {
            var value = ((char)('a' + (i % 26))).ToString();
            daba.Insert(value);
            model.Enqueue(value);
            if (i % 3 == 0)
            {
                daba.Evict();
                model.Dequeue();
            }
            Assert.Equal(string.Concat(model), daba.Aggregate);
        }
    }

    /// <summary>Checks variable-size randomized windows against naive reaggregation.</summary>
    [Fact]
    public void RandomizedHistory_MatchesNaiveSum()
    {
        var random = new Random(20260715);
        var daba = new DabaLite<long, SumMonoid>();
        var model = new Queue<long>();
        for (var i = 0; i < 100_000; i++)
        {
            if (model.Count == 0 || random.Next(2) == 0)
            {
                var value = random.NextInt64(-10_000, 10_001);
                daba.Insert(value);
                model.Enqueue(value);
            }
            else
            {
                Assert.True(daba.TryEvict());
                model.Dequeue();
            }
            Assert.Equal(model.Sum(), daba.Aggregate);
            Assert.Equal(model.Count, daba.Count);
        }
    }

    /// <summary>Locks down the paper's worst-case monoid invocation limits.</summary>
    [Fact]
    public void Operations_RespectWorstCaseCombineBounds()
    {
        var daba = new DabaLite<int, CountingSumMonoid>();
        for (var i = 0; i < 10_000; i++)
        {
            CountingSumMonoid.Reset();
            daba.Insert(i);
            Assert.InRange(CountingSumMonoid.Count, 1, 3);

            CountingSumMonoid.Reset();
            _ = daba.Aggregate;
            Assert.Equal(1, CountingSumMonoid.Count);

            if (i % 2 == 0)
            {
                CountingSumMonoid.Reset();
                daba.Evict();
                Assert.InRange(CountingSumMonoid.Count, 0, 2);
            }
        }
    }

    /// <summary>Verifies empty, clear, and failed-eviction behavior.</summary>
    [Fact]
    public void EmptyAndClearContracts()
    {
        var daba = new DabaLite<long, SumMonoid>();
        Assert.True(daba.IsEmpty);
        Assert.Equal(0, daba.Aggregate);
        Assert.False(daba.TryEvict());
        Assert.Throws<InvalidOperationException>(daba.Evict);
        for (var i = 0; i < 1_000; i++)
            daba.Insert(i);
        daba.Clear();
        Assert.True(daba.IsEmpty);
        Assert.Equal(0, daba.Aggregate);
    }

    private readonly struct StringMonoid : IMonoid<string>
    {
        /// <summary>Gets the identity: the measure of an empty tree.</summary>
        public static string Empty => string.Empty;

        /// <summary>Combines two measures in order. Must be associative; it need not be commutative.</summary>
        public static string Combine(string left, string right) => left + right;
    }

    private readonly struct SumMonoid : IMonoid<long>
    {
        /// <summary>Gets the identity: the measure of an empty tree.</summary>
        public static long Empty => 0;

        /// <summary>Combines two measures in order. Must be associative; it need not be commutative.</summary>
        public static long Combine(long left, long right) => left + right;
    }

    private readonly struct CountingSumMonoid : IMonoid<int>
    {
        private static int _count;

        /// <summary>Gets the identity: the measure of an empty tree.</summary>
        public static int Empty => 0;

        /// <summary>Gets the number of elements in the collection.</summary>
        public static int Count => _count;

        /// <summary>Combines two measures in order. Must be associative; it need not be commutative.</summary>
        public static int Combine(int left, int right)
        {
            _count++;
            return left + right;
        }

        /// <summary>Returns the value to its initial state.</summary>
        public static void Reset() => _count = 0;
    }
}
