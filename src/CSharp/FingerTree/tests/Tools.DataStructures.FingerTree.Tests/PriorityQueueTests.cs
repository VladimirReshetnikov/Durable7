using Tools.DataStructures.FingerTree;
using Xunit;
using BclPriorityQueue = System.Collections.Generic.PriorityQueue<string, int>;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies the meldable priority queue: minimum-first dequeue order against the BCL
/// <see cref="System.Collections.Generic.PriorityQueue{TElement, TPriority}"/>, stable FIFO ordering among
/// equal priorities, O(1) peek of the minimum priority, and melding.
/// </summary>
public sealed class PriorityQueueTests
{
    /// <summary>Verifies draining yields nondecreasing priorities and the same multiset as the BCL queue.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    public void Drain_MatchesBclPriorityOrder(int seed)
    {
        var random = new Random(seed);
        var pairs = Enumerable.Range(0, 300)
            .Select(i => ($"e{i}", random.Next(50)))
            .ToArray();

        var queue = PriorityQueue<string, int>.CreateRange(pairs);
        var bcl = new BclPriorityQueue();
        foreach (var (e, p) in pairs)
            bcl.Enqueue(e, p);

        Assert.Equal(pairs.Length, queue.Count);

        var drainedPriorities = new List<int>();
        var drainedPairs = new List<(string, int)>();
        while (queue.TryDequeue(out var element, out var priority, out queue))
        {
            drainedPriorities.Add(priority);
            drainedPairs.Add((element, priority));
        }

        // Priorities come out nondecreasing and equal to the BCL dequeue order.
        var bclPriorities = new List<int>();
        while (bcl.TryDequeue(out _, out var p))
            bclPriorities.Add(p);

        Assert.Equal(bclPriorities, drainedPriorities);
        Assert.Equal(
            pairs.OrderBy(x => x.Item2).ThenBy(x => x.Item1).ToArray(),
            drainedPairs.OrderBy(x => x.Item2).ThenBy(x => x.Item1).ToArray());
    }

    /// <summary>Verifies FIFO stability among entries of equal least priority.</summary>
    [Fact]
    public void EqualPriorities_DequeueInFifoOrder()
    {
        var queue = PriorityQueue<string, int>.Empty
            .Enqueue("first", 1)
            .Enqueue("second", 1)
            .Enqueue("third", 1)
            .Enqueue("early", 0);

        Assert.True(queue.TryDequeue(out var e0, out _, out queue));
        Assert.Equal("early", e0);                         // strictly smaller priority first
        Assert.True(queue.TryDequeue(out var e1, out _, out queue));
        Assert.Equal("first", e1);                         // then FIFO among equal priorities
        Assert.True(queue.TryDequeue(out var e2, out _, out queue));
        Assert.Equal("second", e2);
        Assert.True(queue.TryDequeue(out var e3, out _, out queue));
        Assert.Equal("third", e3);
        Assert.False(queue.TryDequeue(out _, out _, out _));
    }

    /// <summary>Verifies O(1) priority peek and full peek agree with the front of the queue.</summary>
    [Fact]
    public void Peek_ReturnsFrontWithoutRemoving()
    {
        var queue = PriorityQueue<string, int>.Empty.Enqueue("a", 5).Enqueue("b", 2).Enqueue("c", 8);

        Assert.True(queue.TryPeekPriority(out var p));
        Assert.Equal(2, p);
        Assert.True(queue.TryPeek(out var element, out var priority));
        Assert.Equal("b", element);
        Assert.Equal(2, priority);
        Assert.Equal(3, queue.Count);                      // peek did not remove
    }

    /// <summary>Verifies melding two queues preserves all entries in priority order.</summary>
    [Fact]
    public void Meld_CombinesBothQueues()
    {
        var a = PriorityQueue<string, int>.CreateRange(new[] { ("a1", 4), ("a2", 1), ("a3", 7) });
        var b = PriorityQueue<string, int>.CreateRange(new[] { ("b1", 2), ("b2", 6), ("b3", 0) });

        var melded = a.Meld(b);
        Assert.Equal(6, melded.Count);

        var priorities = new List<int>();
        while (melded.TryDequeue(out _, out var priority, out melded))
            priorities.Add(priority);

        Assert.Equal(new[] { 0, 1, 2, 4, 6, 7 }, priorities);
    }

    /// <summary>Verifies persistence: dequeuing from a queue leaves the original intact.</summary>
    [Fact]
    public void Dequeue_PreservesOriginal()
    {
        var original = PriorityQueue<string, int>.CreateRange(new[] { ("a", 3), ("b", 1), ("c", 2) });

        Assert.True(original.TryDequeue(out var first, out _, out var rest));
        Assert.Equal("b", first);
        Assert.Equal(3, original.Count);   // original unchanged
        Assert.Equal(2, rest.Count);
    }

    /// <summary>Verifies a max-priority queue via a reversed priority order.</summary>
    [Fact]
    public void ReversedPriority_ActsAsMaxQueue()
    {
        // Negating the priority turns the min-queue into a max-queue.
        var queue = PriorityQueue<string, int>.Empty
            .Enqueue("low", -1)
            .Enqueue("high", -9)
            .Enqueue("mid", -5);

        Assert.True(queue.TryDequeue(out var top, out var priority, out _));
        Assert.Equal("high", top);
        Assert.Equal(-9, priority);   // most-negative dequeues first => largest original priority
    }

    /// <summary>Verifies empty-queue degenerate behavior.</summary>
    [Fact]
    public void Empty_HasNoEntries()
    {
        var empty = PriorityQueue<string, int>.Empty;
        Assert.True(empty.IsEmpty);
        Assert.Empty(empty);
        Assert.False(empty.TryPeek(out _, out _));
        Assert.False(empty.TryPeekPriority(out _));
        Assert.False(empty.TryDequeue(out _, out _, out var rest));
        Assert.Same(PriorityQueue<string, int>.Empty, rest);
    }
}
