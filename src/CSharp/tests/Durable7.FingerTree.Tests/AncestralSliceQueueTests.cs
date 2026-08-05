using System.Collections.Concurrent;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Contract, model, branching, concurrency, and representation-boundary coverage for
/// <see cref="AncestralSliceQueue{T}"/>.
/// </summary>
public sealed class AncestralSliceQueueTests
{
    /// <summary>Verifies empty, singleton, nullable-value, and non-throwing removal contracts.</summary>
    [Fact]
    public void EmptySingletonAndNullableValues_UseQueueEndpointContracts()
    {
        var empty = AncestralSliceQueue<string?>.CreateMyers();

        AssertState(empty, []);
        Assert.Throws<InvalidOperationException>(() => empty.First);
        Assert.Throws<InvalidOperationException>(() => empty.Last);
        Assert.Throws<ArgumentOutOfRangeException>(() => empty[-1]);
        Assert.Throws<ArgumentOutOfRangeException>(() => empty[0]);
        Assert.Throws<InvalidOperationException>(() => empty.RemoveFirst());
        Assert.Throws<InvalidOperationException>(() => empty.RemoveLast());

        Assert.False(empty.TryRemoveFirst(out var missingFirst, out var unchangedFirst));
        Assert.Null(missingFirst);
        Assert.Same(empty, unchangedFirst);
        Assert.False(empty.TryRemoveLast(out var missingLast, out var unchangedLast));
        Assert.Null(missingLast);
        Assert.Same(empty, unchangedLast);

        var singleton = empty.AddLast(null);
        AssertState(singleton, [null]);
        Assert.Null(singleton.First);
        Assert.Null(singleton.Last);

        var pair = singleton.AddLast("tail");
        AssertState(pair, [null, "tail"]);
        AssertState(singleton, [null]);

        Assert.True(pair.TryRemoveFirst(out var removedFirst, out var afterFirst));
        Assert.Null(removedFirst);
        AssertState(afterFirst, ["tail"]);
        Assert.True(pair.TryRemoveLast(out var removedLast, out var afterLast));
        Assert.Equal("tail", removedLast);
        AssertState(afterLast, [null]);
        AssertState(pair, [null, "tail"]);
    }

    /// <summary>
    /// Verifies every valid range of a representative source, including an empty slice at every ancestry
    /// boundary. Appending to an empty slice must continue at that exact anchor without revealing old values.
    /// </summary>
    [Fact]
    public void Slice_EveryBoundaryMatchesModelAndEmptyAnchorsRemainAppendable()
    {
        const int length = 65;
        var values = Enumerable.Range(0, length).ToArray();
        var source = Create(values);

        for (var start = 0; start <= length; start++)
        {
            for (var count = 0; count <= length - start; count++)
            {
                var slice = source.Slice(start, count);
                AssertState(slice, values.AsSpan(start, count).ToArray());

                if (count == 0)
                {
                    var continued = slice.AddLast(-start).AddLast(10_000 + start);
                    AssertState(continued, [-start, 10_000 + start]);
                    AssertState(slice, []);
                }
            }
        }

        Assert.Same(source, source.Slice(0, source.Count));
        Assert.Same(source, source.Take(source.Count));
        Assert.Same(source, source.Drop(0));
        AssertState(source, values);
    }

    /// <summary>Verifies Take, Drop, and SplitAt agree at every boundary and retain the source version.</summary>
    [Fact]
    public void TakeDropAndSplitAt_EveryBoundaryPartitionTheSource()
    {
        var values = Enumerable.Range(0, 257).ToArray();
        var source = Create(values);

        for (var position = 0; position <= values.Length; position++)
        {
            var prefix = source.Take(position);
            var suffix = source.Drop(position);
            AssertState(prefix, values[..position]);
            AssertState(suffix, values[position..]);

            var split = source.SplitAt(position);
            AssertState(split.Left, values[..position]);
            AssertState(split.Right, values[position..]);

            // Both results remain real ancestry slices and can independently start new branches.
            AssertState(prefix.AddLast(-position), [.. values[..position], -position]);
            AssertState(suffix.AddLast(-position), [.. values[position..], -position]);
        }

        AssertState(source, values);
    }

    /// <summary>
    /// Exercises every square boundary through 128^2. These are the exact seams induced by conceptual
    /// odd-sized blocks 1, 3, 5, ... even when the selected online-ancestor backend encodes them differently.
    /// </summary>
    [Fact]
    public void SquareAndOddBlockBoundaries_SupportIndexSliceAndBranching()
    {
        const int maximumRoot = 128;
        var length = checked(maximumRoot * maximumRoot + 1);
        var source = Create(Enumerable.Range(0, length));

        for (var root = 0; root <= maximumRoot; root++)
        {
            var square = root * root;
            Assert.Equal(square, source[square]);

            var start = Math.Max(0, square - 1);
            var count = Math.Min(3, source.Count - start);
            AssertState(source.Slice(start, count), Enumerable.Range(start, count).ToArray());

            var emptyAtSquare = source.Slice(square, 0);
            AssertState(emptyAtSquare.AddLast(-square), [-square]);

            // Arena handle square starts a new block. Queue count equals its tail handle, so the
            // versions of lengths square-1 and square end immediately before and at that seam.
            if (square > 0)
            {
                var beforeSeam = source.Take(square - 1);
                AssertState(beforeSeam.AddLast(-square - 1), [.. valuesBefore(square - 1), -square - 1]);

                var atSeam = source.Take(square);
                AssertState(atSeam.AddLast(-square - 2), [.. valuesBefore(square), -square - 2]);
            }

            if (root == maximumRoot)
                continue;

            var nextSquare = (root + 1) * (root + 1);
            Assert.Equal(2 * root + 1, nextSquare - square);
            AssertState(
                source.Slice(square, nextSquare - square),
                Enumerable.Range(square, nextSquare - square).ToArray());
        }

        AssertState(source, Enumerable.Range(0, length).ToArray());

        static int[] valuesBefore(int count) => Enumerable.Range(0, count).ToArray();
    }

    /// <summary>Verifies each enumerator observes exactly the immutable handle from which it was obtained.</summary>
    [Fact]
    public void Enumeration_IsRepeatableAndDoesNotObserveLaterBranches()
    {
        var source = Create(Enumerable.Range(0, 100));
        var slice = source.Slice(20, 40);
        using var captured = slice.GetEnumerator();

        var branch = slice.AddLast(-1).AddLast(-2);

        Assert.Equal(Enumerable.Range(20, 40), slice.ToArray());
        Assert.Equal(Enumerable.Range(20, 40), slice.ToArray());
        Assert.Equal([.. Enumerable.Range(20, 40), -1, -2], branch.ToArray());

        var capturedValues = new List<int>();
        while (captured.MoveNext())
            capturedValues.Add(captured.Current);

        Assert.Equal(Enumerable.Range(20, 40), capturedValues);
        AssertState(source, Enumerable.Range(0, 100).ToArray());
    }

    /// <summary>Compares Myers level-ancestor answers on a deep, irregular tree with a naive parent oracle.</summary>
    [Fact]
    public void MyersArena_BranchedAncestorQueriesMatchNaiveParentOracle()
    {
        const int chainLength = 1_024;
        const int branchCount = 1_024;
        var arena = new MyersIncrementalAncestorArena<int>();
        var random = new Random(0xA11CE);
        var nodes = new List<int> { arena.Bottom };
        var models = new Dictionary<int, (int Parent, int Depth, int Value)>
        {
            [arena.Bottom] = (arena.Bottom, -1, default),
        };

        var parent = arena.Bottom;
        for (var value = 0; value < chainLength; value++)
        {
            var node = arena.AddLeaf(parent, value);
            var depth = models[parent].Depth + 1;
            nodes.Add(node);
            models.Add(node, (parent, depth, value));
            parent = node;
        }

        for (var offset = 0; offset < branchCount; offset++)
        {
            parent = nodes[random.Next(nodes.Count)];
            var value = chainLength + offset;
            var node = arena.AddLeaf(parent, value);
            var depth = models[parent].Depth + 1;
            nodes.Add(node);
            models.Add(node, (parent, depth, value));
        }

        Assert.Equal(-1, arena.GetDepth(arena.Bottom));
        Assert.Equal(chainLength + branchCount, arena.PublishedNodeCount);
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.GetParent(arena.Bottom));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.GetValue(arena.Bottom));

        long expectedQueryCount = 0;
        foreach (var node in nodes.Skip(1))
        {
            var model = models[node];
            Assert.Equal(model.Depth, arena.GetDepth(node));
            Assert.Equal(model.Parent, arena.GetParent(node));
            Assert.Equal(model.Value, arena.GetValue(node));

            var expectedAncestor = node;
            for (var targetDepth = model.Depth; targetDepth >= -1; targetDepth--)
            {
                Assert.Equal(expectedAncestor, arena.AncestorAtDepth(node, targetDepth));
                expectedQueryCount++;
                if (targetDepth >= 0)
                    expectedAncestor = models[expectedAncestor].Parent;
            }
        }

        Assert.Equal(arena.Bottom, arena.AncestorAtDepth(arena.Bottom, -1));
        expectedQueryCount++;
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AncestorAtDepth(arena.Bottom, -2));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AncestorAtDepth(arena.Bottom, 0));

        var beforeRejectedAdds = arena.GetStatistics();
        Assert.Equal(expectedQueryCount, beforeRejectedAdds.AncestorQueryCount);
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AddLeaf(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AddLeaf(int.MaxValue, 0));
        Assert.Equal(beforeRejectedAdds, arena.GetStatistics());
    }

    /// <summary>Verifies the odd block directory allocates exactly at labeled-node square boundaries.</summary>
    [Fact]
    public void MyersArena_OddBlockStatisticsMatchExactSquareLayout()
    {
        const int maximumPublishedCount = 4_096;
        var arena = new MyersIncrementalAncestorArena<int>();
        var parent = arena.Bottom;

        for (var publishedCount = 0; publishedCount <= maximumPublishedCount; publishedCount++)
        {
            var statistics = arena.GetStatistics();
            var lastBlock = IntegerSquareRoot(publishedCount);
            var expectedBlockCount = lastBlock + 1;

            Assert.Equal(publishedCount, arena.PublishedNodeCount);
            Assert.Equal(publishedCount, statistics.PublishedNodeCount);
            Assert.Equal(expectedBlockCount, statistics.BlockCount);
            Assert.Equal((long)expectedBlockCount * expectedBlockCount, statistics.AllocatedSlotCount);
            Assert.Equal(publishedCount, statistics.AddLeafCount);
            Assert.Equal(0, statistics.AncestorQueryCount);
            Assert.Equal(0, statistics.TotalAncestorHopCount);

            if (publishedCount != maximumPublishedCount)
                parent = arena.AddLeaf(parent, publishedCount);
        }
    }

    /// <summary>Guards the Myers backend against accidentally degenerating to linear ancestor walks.</summary>
    [Fact]
    public void MyersArena_DeepChainQueriesUseConservativelyLogarithmicHops()
    {
        const int nodeCount = 32_768;
        var arena = new MyersIncrementalAncestorArena<int>();
        var nodesByDepth = new List<int>(nodeCount + 1) { arena.Bottom };
        var tail = arena.Bottom;

        for (var value = 0; value < nodeCount; value++)
        {
            tail = arena.AddLeaf(tail, value);
            nodesByDepth.Add(tail);
        }

        var afterAdds = arena.GetStatistics();
        Assert.Equal(nodeCount, afterAdds.AddLeafCount);
        Assert.Equal(0, afterAdds.AncestorQueryCount);
        Assert.Equal(0, afterAdds.MaximumAncestorHopCount);
        Assert.Equal(0, afterAdds.TotalAncestorHopCount);

        for (var targetDepth = nodeCount - 1; targetDepth >= -1; targetDepth--)
            Assert.Equal(nodesByDepth[targetDepth + 1], arena.AncestorAtDepth(tail, targetDepth));

        var afterQueries = arena.GetStatistics();
        var conservativeLogarithmicBound = 4 * CeilingLog2(nodeCount + 1);
        Assert.Equal(nodeCount + 1, afterQueries.AncestorQueryCount);
        Assert.InRange(afterQueries.MaximumAncestorHopCount, 1, conservativeLogarithmicBound);
        Assert.InRange(
            afterQueries.TotalAncestorHopCount,
            1,
            (long)(nodeCount + 1) * conservativeLogarithmicBound);
    }

    /// <summary>Verifies the queue routes each scalar operation through no more backend work than specified.</summary>
    [Fact]
    public void QueueOperations_UseTheParameterizedBackendComplexityBoundary()
    {
        var arena = new MyersIncrementalAncestorArena<int>();
        var queue = AncestralSliceQueue<int>.Create(arena);
        for (var value = 0; value < 16; value++)
            queue = queue.AddLast(value);

        AssertArenaDelta(arena, expectedAdds: 0, expectedQueries: 0, () =>
        {
            Assert.Equal(16, queue.Count);
            Assert.False(queue.IsEmpty);
            Assert.Equal(15, queue.Last);
            Assert.Equal(15, queue[15]);
        });
        AssertArenaDelta(arena, 0, 1, () => Assert.Equal(0, queue.First));
        AssertArenaDelta(arena, 0, 1, () => Assert.Equal(7, queue[7]));
        AssertArenaDelta(arena, 0, 0, () => Assert.Equal(15, queue.RemoveFirst().Count));
        AssertArenaDelta(arena, 0, 0, () => Assert.Equal(15, queue.RemoveLast().Count));
        AssertArenaDelta(arena, 0, 0, () => Assert.Equal(9, queue.Drop(7).Count));
        AssertArenaDelta(arena, 0, 1, () => Assert.Equal(7, queue.Take(7).Count));
        AssertArenaDelta(arena, 0, 1, () => Assert.Equal(5, queue.Slice(3, 5).Count));
        AssertArenaDelta(arena, 0, 0, () => Assert.Equal(13, queue.Slice(3, 13).Count));
        AssertArenaDelta(arena, 0, 1, () =>
        {
            var split = queue.SplitAt(7);
            Assert.Equal(7, split.Left.Count);
            Assert.Equal(9, split.Right.Count);
        });
        AssertArenaDelta(arena, 0, 1, () =>
        {
            Assert.True(queue.TryRemoveFirst(out var value, out var result));
            Assert.Equal(0, value);
            Assert.Equal(15, result.Count);
        });
        AssertArenaDelta(arena, 0, 0, () =>
        {
            Assert.True(queue.TryRemoveLast(out var value, out var result));
            Assert.Equal(15, value);
            Assert.Equal(15, result.Count);
        });
        AssertArenaDelta(arena, expectedAdds: 1, expectedQueries: 0, () =>
        {
            var appended = queue.AddLast(16);
            Assert.Equal(17, appended.Count);
            Assert.Equal(16, appended.Last);
        });
    }

    /// <summary>Verifies explicit and convenience factories, including invalid arena contracts.</summary>
    [Fact]
    public void Factories_CreateIndependentValidQueuesAndRejectInvalidInputs()
    {
        var arena = new MyersIncrementalAncestorArena<int>();
        var explicitQueue = AncestralSliceQueue<int>.Create(arena).AddLast(10).AddLast(20);
        AssertState(explicitQueue, [10, 20]);
        Assert.Equal(2, arena.PublishedNodeCount);

        var range = AncestralSliceQueue<int>.CreateRange(new[] { 1, 2, 3 });
        AssertState(range, [1, 2, 3]);
        AssertState(range.AddLast(4), [1, 2, 3, 4]);
        AssertState(range, [1, 2, 3]);

        Assert.Throws<ArgumentNullException>(() => AncestralSliceQueue<int>.Create(null!));
        Assert.Throws<ArgumentNullException>(() => AncestralSliceQueue<int>.CreateRange(null!));
        Assert.Throws<ArgumentException>(() =>
            AncestralSliceQueue<int>.Create(new InvalidBottomArena<int>()));
    }

    /// <summary>Verifies both endpoint-removal lineages and every retained intermediate version.</summary>
    [Fact]
    public void EndpointRemovalHistories_RetainEveryVersion()
    {
        var values = Enumerable.Range(0, 512).ToArray();
        var source = Create(values);
        var frontVersions = new List<AncestralSliceQueue<int>> { source };
        var backVersions = new List<AncestralSliceQueue<int>> { source };

        for (var i = 0; i < values.Length; i++)
        {
            frontVersions.Add(frontVersions[^1].RemoveFirst());
            backVersions.Add(backVersions[^1].RemoveLast());
        }

        for (var removed = 0; removed <= values.Length; removed++)
        {
            AssertState(frontVersions[removed], values[removed..]);
            AssertState(backVersions[removed], values[..(values.Length - removed)]);
        }

        // Empty versions keep different ancestry anchors, but both must behave extensionally as empty queues.
        AssertState(frontVersions[^1].AddLast(-1), [-1]);
        AssertState(backVersions[^1].AddLast(-2), [-2]);
        AssertState(source, values);
    }

    /// <summary>Verifies many independent descendants of the same retained handles never disturb each other.</summary>
    [Fact]
    public void RetainedBranches_FromWholeAndSlicedVersionsRemainIndependent()
    {
        var values = Enumerable.Range(0, 200).ToArray();
        var source = Create(values);
        var middle = source.Slice(50, 100);

        var branches = new[]
        {
            middle.AddLast(-1),
            middle.AddLast(-2).AddLast(-3),
            middle.RemoveFirst().AddLast(-4),
            middle.RemoveLast().AddLast(-5),
            middle.Take(40).AddLast(-6),
            middle.Drop(60).AddLast(-7),
            middle.Slice(30, 0).AddLast(-8),
        };

        AssertState(branches[0], [.. values[50..150], -1]);
        AssertState(branches[1], [.. values[50..150], -2, -3]);
        AssertState(branches[2], [.. values[51..150], -4]);
        AssertState(branches[3], [.. values[50..149], -5]);
        AssertState(branches[4], [.. values[50..90], -6]);
        AssertState(branches[5], [.. values[110..150], -7]);
        AssertState(branches[6], [-8]);
        AssertState(middle, values[50..150]);
        AssertState(source, values);
    }

    /// <summary>Runs a deterministic branching command history against independent array models.</summary>
    [Fact]
    public void RandomizedBranchingHistory_MatchesArrayModels()
    {
        const int steps = 2_500;
        var random = new Random(0x51_1CE);
        var histories = new List<(AncestralSliceQueue<int> Queue, int[] Model)>
        {
            (AncestralSliceQueue<int>.CreateMyers(), []),
        };

        for (var step = 0; step < steps; step++)
        {
            var (source, model) = histories[random.Next(histories.Count)];
            AncestralSliceQueue<int> result;
            int[] expected;

            switch (random.Next(9))
            {
                case 0:
                    result = source.AddLast(step);
                    expected = [.. model, step];
                    break;
                case 1 when model.Length != 0:
                    result = source.RemoveFirst();
                    expected = model[1..];
                    break;
                case 2 when model.Length != 0:
                    result = source.RemoveLast();
                    expected = model[..^1];
                    break;
                case 3:
                    {
                        var count = random.Next(model.Length + 1);
                        result = source.Take(count);
                        expected = model[..count];
                        break;
                    }
                case 4:
                    {
                        var count = random.Next(model.Length + 1);
                        result = source.Drop(count);
                        expected = model[count..];
                        break;
                    }
                case 5:
                    {
                        var start = random.Next(model.Length + 1);
                        var count = random.Next(model.Length - start + 1);
                        result = source.Slice(start, count);
                        expected = model.AsSpan(start, count).ToArray();
                        break;
                    }
                case 6:
                    {
                        var position = random.Next(model.Length + 1);
                        var split = source.SplitAt(position);
                        if (random.Next(2) == 0)
                        {
                            result = split.Left;
                            expected = model[..position];
                        }
                        else
                        {
                            result = split.Right;
                            expected = model[position..];
                        }
                        break;
                    }
                case 7 when model.Length != 0:
                    {
                        Assert.True(source.TryRemoveFirst(out var removed, out result));
                        Assert.Equal(model[0], removed);
                        expected = model[1..];
                        break;
                    }
                case 8 when model.Length != 0:
                    {
                        Assert.True(source.TryRemoveLast(out var removed, out result));
                        Assert.Equal(model[^1], removed);
                        expected = model[..^1];
                        break;
                    }
                default:
                    result = source.AddLast(-step);
                    expected = [.. model, -step];
                    break;
            }

            AssertState(source, model);
            AssertState(result, expected);
            histories.Add((result, expected));

            if (step % 211 == 0)
            {
                for (var sample = 0; sample < Math.Min(32, histories.Count); sample++)
                {
                    var retained = histories[random.Next(histories.Count)];
                    AssertState(retained.Queue, retained.Model);
                }
            }
        }

        foreach (var retained in histories.Where((_, index) => index % 97 == 0))
            AssertState(retained.Queue, retained.Model);
    }

    /// <summary>
    /// Verifies the shared monotone arena safely accepts concurrent branches while readers continue to observe
    /// one unchanged source and every published child is complete.
    /// </summary>
    [Fact]
    public async Task ConcurrentBranchAppendsAndReads_PublishCompleteIndependentVersions()
    {
        const int branchCount = 256;
        var expected = Enumerable.Range(0, 1_024).ToArray();
        var source = Create(expected);
        var branches = new AncestralSliceQueue<int>[branchCount];
        var failures = new ConcurrentQueue<string>();

        using var start = new ManualResetEventSlim(initialState: false);
        var writers = Enumerable.Range(0, branchCount).Select(branch => Task.Run(() =>
        {
            try
            {
                start.Wait();
                branches[branch] = source.AddLast(-branch - 1);
                Assert.Equal(expected.Length + 1, branches[branch].Count);
                Assert.Equal(-branch - 1, branches[branch].Last);

                // Simultaneously exercise level-ancestor reads while sibling nodes are being published.
                for (var index = branch % 31; index < expected.Length; index += 127)
                    Assert.Equal(index, source[index]);
                Assert.Equal(expected[100..140], source.Slice(100, 40).ToArray());
                if (branch % 17 == 0)
                    Assert.Equal(expected, source.ToArray());
            }
            catch (Exception exception)
            {
                failures.Enqueue(exception.ToString());
            }
        })).ToArray();

        start.Set();
        await Task.WhenAll(writers);

        // Observe each published child through a different work item after the writer join. This is a
        // synchronized stress case, not a claim to exhaust all scheduler or lock interleavings.
        Parallel.For(0, branchCount, branch =>
        {
            try
            {
                var observedBranch = (branch + 1) % branchCount;
                var observed = branches[observedBranch];
                Assert.Equal(expected.Length + 1, observed.Count);
                Assert.Equal(-observedBranch - 1, observed.Last);
            }
            catch (Exception exception)
            {
                failures.Enqueue(exception.ToString());
            }
        });

        Assert.Empty(failures);
        AssertState(source, expected);
        for (var branch = 0; branch < branchCount; branch++)
            AssertState(branches[branch], [.. expected, -branch - 1]);
    }

    /// <summary>Verifies bad indices and ranges are rejected without changing the source.</summary>
    [Fact]
    public void IndexAndRangeFailures_RejectNegativeAndOverflowingBounds()
    {
        var source = Create(new[] { 10, 20, 30 });

        Assert.Throws<ArgumentOutOfRangeException>(() => source[-1]);
        Assert.Throws<ArgumentOutOfRangeException>(() => source[3]);
        Assert.Throws<ArgumentOutOfRangeException>(() => source.Slice(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.Slice(0, -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.Slice(4, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.Slice(2, 2));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.Slice(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.Take(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.Take(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.Drop(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.Drop(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.SplitAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.SplitAt(4));

        AssertState(source, [10, 20, 30]);
    }

    private static AncestralSliceQueue<T> Create<T>(IEnumerable<T> values)
    {
        var result = AncestralSliceQueue<T>.CreateMyers();
        foreach (var value in values)
            result = result.AddLast(value);
        return result;
    }

    private static int IntegerSquareRoot(int value)
    {
        var root = (int)Math.Sqrt(value);
        while ((long)(root + 1) * (root + 1) <= value)
            root++;
        while ((long)root * root > value)
            root--;
        return root;
    }

    private static int CeilingLog2(int value)
    {
        var result = 0;
        var power = 1L;
        while (power < value)
        {
            power <<= 1;
            result++;
        }
        return result;
    }

    private static void AssertArenaDelta(
        MyersIncrementalAncestorArena<int> arena,
        long expectedAdds,
        long expectedQueries,
        Action action)
    {
        var before = arena.GetStatistics();
        action();
        var after = arena.GetStatistics();

        Assert.Equal(before.AddLeafCount + expectedAdds, after.AddLeafCount);
        Assert.Equal(before.PublishedNodeCount + expectedAdds, after.PublishedNodeCount);
        Assert.Equal(before.AncestorQueryCount + expectedQueries, after.AncestorQueryCount);
    }

    private sealed class InvalidBottomArena<T> : IIncrementalAncestorArena<T>
    {
        public int Bottom => 0;
        public int PublishedNodeCount => 0;
        public int AddLeaf(int parent, T value) => throw new NotSupportedException();
        public int GetDepth(int node) => 0;
        public int GetParent(int node) => throw new NotSupportedException();
        public int AncestorAtDepth(int node, int depth) => throw new NotSupportedException();
        public T GetValue(int node) => throw new NotSupportedException();
    }

    private static void AssertState<T>(AncestralSliceQueue<T> queue, IReadOnlyList<T> expected)
    {
        Assert.Equal(expected.Count, queue.Count);
        Assert.Equal(expected.Count == 0, queue.IsEmpty);
        Assert.Equal(expected, queue.ToArray());

        for (var index = 0; index < expected.Count; index++)
            Assert.Equal(expected[index], queue[index]);

        if (expected.Count == 0)
        {
            Assert.Throws<InvalidOperationException>(() => queue.First);
            Assert.Throws<InvalidOperationException>(() => queue.Last);
        }
        else
        {
            Assert.Equal(expected[0], queue.First);
            Assert.Equal(expected[^1], queue.Last);
        }
    }
}
