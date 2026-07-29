using Durable7.FingerTree.Experimental;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies the bilateral ancestral deque against sequence and ancestry oracles, including the
/// constant-number-of-level-ancestor-query claims made by its public contract.
/// </summary>
public sealed class BilateralAncestralDequeTests
{
    /// <summary>Verifies empty, nullable-value, factory, and failed-removal contracts.</summary>
    [Fact]
    public void EmptyAndNullableValues_HaveValueSemanticsAndStableFailures()
    {
        var empty = BilateralAncestralDeque<string?>.CreateMyers();

        Assert.True(empty.IsEmpty);
        Assert.Empty(empty);
        Assert.Empty(empty.ToArray());
        Assert.Same(empty, empty.Clear());
        Assert.Same(empty, empty.Take(0));
        Assert.Same(empty, empty.Drop(0));
        Assert.Same(empty, empty.Slice(0, 0));
        Assert.Same(empty, empty.Reverse());
        var emptySplit = empty.SplitAt(0);
        Assert.Same(empty, emptySplit.Left);
        Assert.Same(empty, emptySplit.Right);
        Assert.False(empty.TryRemoveFirst(out var missingFirst, out var afterMissingFirst));
        Assert.Null(missingFirst);
        Assert.Same(empty, afterMissingFirst);
        Assert.False(empty.TryRemoveLast(out var missingLast, out var afterMissingLast));
        Assert.Null(missingLast);
        Assert.Same(empty, afterMissingLast);

        Assert.Throws<InvalidOperationException>(() => empty.First);
        Assert.Throws<InvalidOperationException>(() => empty.Last);
        Assert.Throws<InvalidOperationException>(() => empty.RemoveFirst());
        Assert.Throws<InvalidOperationException>(() => empty.RemoveLast());
        Assert.Throws<ArgumentOutOfRangeException>(() => empty[0]);
        Assert.Throws<ArgumentOutOfRangeException>(() => empty[-1]);

        var nullable = empty.AddFirst(null).AddLast("tail");
        Assert.Null(nullable.First);
        Assert.Equal("tail", nullable.Last);
        Assert.Null(nullable[0]);
        Assert.Equal(new string?[] { null, "tail" }, nullable.ToArray());
        nullable.ValidateInvariants();

        Assert.Throws<ArgumentNullException>(() =>
            BilateralAncestralDeque<int>.Create(null!));
        Assert.Throws<ArgumentNullException>(() =>
            BilateralAncestralDeque<int>.CreateRange(null!));
        Assert.Throws<ArgumentException>(() =>
            BilateralAncestralDeque<int>.Create(new InvalidBottomArena<int>()));

        var existing = BilateralAncestralDeque<int>.CreateRange([1, 2, 3]);
        AssertDeque([1, 2, 3], BilateralAncestralDeque<int>.CreateRange(existing));
    }

    /// <summary>
    /// Verifies endpoint operations, cross-arm removals, and branches retained from every version.
    /// </summary>
    [Fact]
    public void EndpointOperations_RetainEveryPriorBranch()
    {
        var root = BilateralAncestralDeque<int>.CreateMyers();
        var one = root.AddLast(10);
        var two = one.AddLast(20);
        var three = two.AddFirst(5);
        var four = three.AddFirst(1);
        var five = four.AddLast(30);

        AssertDeque([], root);
        AssertDeque([10], one);
        AssertDeque([10, 20], two);
        AssertDeque([5, 10, 20], three);
        AssertDeque([1, 5, 10, 20], four);
        AssertDeque([1, 5, 10, 20, 30], five);

        var withoutFirst = five.RemoveFirst();
        var withoutLast = five.RemoveLast();
        AssertDeque([5, 10, 20, 30], withoutFirst);
        AssertDeque([1, 5, 10, 20], withoutLast);
        AssertDeque([1, 5, 10, 20, 30], five);

        var rightOnly = root.AddLast(1).AddLast(2).AddLast(3);
        AssertDeque([2, 3], rightOnly.RemoveFirst());
        AssertDeque([3], rightOnly.RemoveFirst().RemoveFirst());
        Assert.Empty(rightOnly.RemoveFirst().RemoveFirst().RemoveFirst());

        var leftOnly = root.AddFirst(3).AddFirst(2).AddFirst(1);
        AssertDeque([1, 2], leftOnly.RemoveLast());
        AssertDeque([1], leftOnly.RemoveLast().RemoveLast());
        Assert.Empty(leftOnly.RemoveLast().RemoveLast().RemoveLast());

        Assert.True(five.TryRemoveFirst(out var first, out var firstResult));
        Assert.Equal(1, first);
        AssertDeque([5, 10, 20, 30], firstResult);
        Assert.True(five.TryRemoveLast(out var last, out var lastResult));
        Assert.Equal(30, last);
        AssertDeque([1, 5, 10, 20], lastResult);

        var leftBranch = two.AddFirst(-1);
        var rightBranch = two.AddLast(99);
        AssertDeque([-1, 10, 20], leftBranch);
        AssertDeque([10, 20, 99], rightBranch);
        AssertDeque([10, 20], two);

        var cleared = five.Clear();
        Assert.Empty(cleared);
        AssertDeque([-7, 8], cleared.AddFirst(-7).AddLast(8));
        AssertDeque([1, 5, 10, 20, 30], five);
    }

    /// <summary>Verifies reversal laws and operations continued from reversed handles.</summary>
    [Fact]
    public void Reverse_SwapsOrientationWithoutChangingRetainedVersions()
    {
        var deque = BilateralAncestralDeque<int>.CreateMyers()
            .AddFirst(3)
            .AddFirst(2)
            .AddFirst(1)
            .AddLast(4)
            .AddLast(5)
            .AddLast(6)
            .AddLast(7);
        var reversed = deque.Reverse();
        var restored = reversed.Reverse();

        AssertDeque([1, 2, 3, 4, 5, 6, 7], deque);
        AssertDeque([7, 6, 5, 4, 3, 2, 1], reversed);
        AssertDeque([1, 2, 3, 4, 5, 6, 7], restored);
        AssertDeque([0, 7, 6, 5, 4, 3, 2, 1, 8], reversed.AddFirst(0).AddLast(8));
        AssertDeque([6, 5, 4, 3, 2], reversed.RemoveFirst().RemoveLast());
        AssertDeque([1, 2, 3, 4, 5, 6, 7], deque);

        var singleton = BilateralAncestralDeque<int>.CreateRange([42]);
        Assert.Same(singleton, singleton.Reverse());
    }

    /// <summary>
    /// Checks every slice and split boundary across two nonempty arms, then grows every result at
    /// both ends to prove that sliced endpoint ancestry remains closed under insertion.
    /// </summary>
    [Fact]
    public void SliceAndSplit_EveryBoundaryCrossingBothArms_RemainsGrowable()
    {
        var original = BilateralAncestralDeque<int>.CreateMyers()
            .AddFirst(3)
            .AddFirst(2)
            .AddFirst(1)
            .AddLast(4)
            .AddLast(5)
            .AddLast(6)
            .AddLast(7);
        var expected = Enumerable.Range(1, 7).ToArray();

        for (var start = 0; start <= expected.Length; start++)
        {
            for (var count = 0; count <= expected.Length - start; count++)
            {
                var slice = original.Slice(start, count);
                var sliceModel = expected.Skip(start).Take(count).ToArray();
                AssertDeque(sliceModel, slice);

                var extended = slice.AddFirst(-100 - start).AddLast(100 + count);
                AssertDeque(
                    new[] { -100 - start }.Concat(sliceModel).Append(100 + count),
                    extended);
                AssertDeque(sliceModel, slice);

                var reversed = slice.Reverse().AddFirst(-1).AddLast(-2);
                AssertDeque(new[] { -1 }.Concat(sliceModel.Reverse()).Append(-2), reversed);
            }
        }

        for (var boundary = 0; boundary <= expected.Length; boundary++)
        {
            var split = original.SplitAt(boundary);
            var leftModel = expected.Take(boundary).ToArray();
            var rightModel = expected.Skip(boundary).ToArray();
            AssertDeque(leftModel, split.Left);
            AssertDeque(rightModel, split.Right);
            AssertDeque(leftModel.Append(88), split.Left.AddLast(88));
            AssertDeque(new[] { 77 }.Concat(rightModel), split.Right.AddFirst(77));
        }

        AssertDeque(expected, original);
    }

    /// <summary>
    /// Exhausts every front/back construction word through length eight and every legal slice of
    /// each resulting two-interval representation.
    /// </summary>
    [Fact]
    public void SmallEndpointConstructionWords_AllSlicesMatchSequenceOracle()
    {
        for (var length = 0; length <= 8; length++)
        {
            for (var word = 0; word < 1 << length; word++)
            {
                var deque = BilateralAncestralDeque<int>.CreateMyers();
                var model = new List<int>();
                for (var value = 0; value < length; value++)
                {
                    if ((word & (1 << value)) == 0)
                    {
                        deque = deque.AddFirst(value);
                        model.Insert(0, value);
                    }
                    else
                    {
                        deque = deque.AddLast(value);
                        model.Add(value);
                    }
                }

                AssertDeque(model, deque);
                for (var start = 0; start <= length; start++)
                {
                    for (var count = 0; count <= length - start; count++)
                    {
                        var expected = model.Skip(start).Take(count).ToArray();
                        var slice = deque.Slice(start, count);
                        AssertDeque(expected, slice);
                        AssertDeque(new[] { -1 }.Concat(expected).Append(-2),
                            slice.AddFirst(-1).AddLast(-2));
                    }
                }

                for (var boundary = 0; boundary <= length; boundary++)
                {
                    var split = deque.SplitAt(boundary);
                    AssertDeque(model.Take(boundary), split.Left);
                    AssertDeque(model.Skip(boundary), split.Right);
                }
            }
        }
    }

    /// <summary>
    /// Runs retained, randomly selected branches through the complete restricted algebra and
    /// compares every produced version with an immutable array model.
    /// </summary>
    /// <param name="seed">The deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(17)]
    [InlineData(91)]
    [InlineData(8675309)]
    public void RandomizedRetainedBranches_MatchImmutableArrayModel(int seed)
    {
        var random = new Random(seed);
        var root = BilateralAncestralDeque<int>.CreateMyers();
        var versions = new List<(BilateralAncestralDeque<int> Deque, int[] Model)>
        {
            (root, []),
        };

        for (var step = 0; step < 500; step++)
        {
            var sourceIndex = random.Next(versions.Count);
            var source = versions[sourceIndex];
            AssertDeque(source.Model, source.Deque);

            BilateralAncestralDeque<int> result;
            int[] model;
            var value = checked(seed + step * 31);
            switch (random.Next(11))
            {
                case 0:
                    result = source.Deque.AddFirst(value);
                    model = new[] { value }.Concat(source.Model).ToArray();
                    break;
                case 1:
                    result = source.Deque.AddLast(value);
                    model = source.Model.Append(value).ToArray();
                    break;
                case 2 when source.Model.Length != 0:
                    result = source.Deque.RemoveFirst();
                    model = source.Model[1..];
                    break;
                case 3 when source.Model.Length != 0:
                    result = source.Deque.RemoveLast();
                    model = source.Model[..^1];
                    break;
                case 4:
                    result = source.Deque.Reverse();
                    model = source.Model.Reverse().ToArray();
                    break;
                case 5:
                    {
                        var start = random.Next(source.Model.Length + 1);
                        var count = random.Next(source.Model.Length - start + 1);
                        result = source.Deque.Slice(start, count);
                        model = source.Model[start..(start + count)];
                        break;
                    }
                case 6:
                    {
                        var boundary = random.Next(source.Model.Length + 1);
                        var split = source.Deque.SplitAt(boundary);
                        if (random.Next(2) == 0)
                        {
                            result = split.Left;
                            model = source.Model[..boundary];
                        }
                        else
                        {
                            result = split.Right;
                            model = source.Model[boundary..];
                        }

                        break;
                    }
                case 7:
                    {
                        var count = random.Next(source.Model.Length + 1);
                        result = source.Deque.Take(count);
                        model = source.Model[..count];
                        break;
                    }
                case 8:
                    {
                        var count = random.Next(source.Model.Length + 1);
                        result = source.Deque.Drop(count);
                        model = source.Model[count..];
                        break;
                    }
                case 9:
                    result = source.Deque.Clear();
                    model = [];
                    break;
                default:
                    result = source.Deque.AddFirst(value).Reverse().AddLast(~value);
                    model = new[] { value }.Concat(source.Model).Reverse().Append(~value).ToArray();
                    break;
            }

            AssertDeque(model, result);
            AssertDeque(source.Model, source.Deque);
            versions.Add((result, model));

            if (step % 29 == 0)
            {
                for (var sample = 0; sample < Math.Min(versions.Count, 12); sample++)
                {
                    var retained = versions[random.Next(versions.Count)];
                    AssertDeque(retained.Model, retained.Deque);
                }
            }
        }

        AssertDeque([], root);
    }

    /// <summary>
    /// Pins the advertised arena-query ceilings independently for endpoint reads and edits,
    /// indexing, slicing, splitting, reversal, and enumeration.
    /// </summary>
    [Fact]
    public void PublicOperations_RespectLevelAncestorQueryCeilings()
    {
        var arena = new MyersLevelAncestorArena<int>();
        var root = BilateralAncestralDeque<int>.Create(arena);
        var deque = root
            .AddFirst(4)
            .AddFirst(3)
            .AddFirst(2)
            .AddFirst(1)
            .AddLast(5)
            .AddLast(6)
            .AddLast(7)
            .AddLast(8);

        AssertQueriesAtMost(arena, 0, () => deque.First);
        AssertQueriesAtMost(arena, 0, () => deque.Last);
        AssertQueriesAtMost(arena, 0, () => deque.Reverse());
        AssertQueriesAtMost(arena, 0, () => deque.Clear());
        AssertQueriesAtMost(arena, 0, () => deque.AddFirst(0));
        AssertQueriesAtMost(arena, 0, () => deque.AddLast(9));
        AssertQueriesAtMost(arena, 0, () => deque.ToArray());
        AssertQueriesAtMost(arena, 0, () => deque.ToList());

        for (var index = 0; index < deque.Count; index++)
        {
            var captured = index;
            var value = AssertQueriesAtMost(arena, 1, () => deque[captured]);
            Assert.Equal(index + 1, value);
        }

        for (var start = 0; start <= deque.Count; start++)
        {
            for (var count = 0; count <= deque.Count - start; count++)
            {
                var capturedStart = start;
                var capturedCount = count;
                var slice = AssertQueriesAtMost(
                    arena,
                    2,
                    () => deque.Slice(capturedStart, capturedCount));
                Assert.Equal(Enumerable.Range(1, 8).Skip(start).Take(count), slice);
            }
        }

        for (var boundary = 0; boundary <= deque.Count; boundary++)
        {
            var captured = boundary;
            var split = AssertQueriesAtMost(arena, 2, () => deque.SplitAt(captured));
            Assert.Equal(Enumerable.Range(1, captured), split.Left);
            Assert.Equal(Enumerable.Range(captured + 1, 8 - captured), split.Right);
        }

        var leftOwn = AssertQueriesAtMost(arena, 0, () => deque.RemoveFirst());
        var rightOwn = AssertQueriesAtMost(arena, 0, () => deque.RemoveLast());
        AssertDeque([2, 3, 4, 5, 6, 7, 8], leftOwn);
        AssertDeque([1, 2, 3, 4, 5, 6, 7], rightOwn);

        var rightOnly = root.AddLast(10).AddLast(11).AddLast(12).AddLast(13);
        while (!rightOnly.IsEmpty)
            rightOnly = AssertQueriesAtMost(arena, 1, () => rightOnly.RemoveFirst());
        var leftOnly = root.AddFirst(13).AddFirst(12).AddFirst(11).AddFirst(10);
        while (!leftOnly.IsEmpty)
            leftOnly = AssertQueriesAtMost(arena, 1, () => leftOnly.RemoveLast());
    }

    /// <summary>
    /// Compares Myers ancestor navigation on a deep, irregular, multiply branched tree with a
    /// naive parent-walking oracle at every depth class.
    /// </summary>
    [Fact]
    public void MyersArena_IrregularBranchingMatchesNaiveParentOracle()
    {
        const int nodeCount = 4_096;
        var random = new Random(0x5EED);
        var arena = new MyersLevelAncestorArena<int>();
        var handles = new List<int> { arena.Bottom };
        var parents = new Dictionary<int, int>();
        var depths = new Dictionary<int, int> { [arena.Bottom] = -1 };

        for (var ordinal = 1; ordinal <= nodeCount; ordinal++)
        {
            int parent;
            if (ordinal <= nodeCount / 2)
            {
                parent = handles[^1];
            }
            else
            {
                switch (ordinal % 11)
                {
                    case <= 5:
                        parent = handles[^1];
                        break;
                    case 6:
                        parent = arena.Bottom;
                        break;
                    default:
                        parent = handles[random.Next(handles.Count)];
                        break;
                }
            }

            var handle = arena.AddLeaf(parent, ordinal);
            handles.Add(handle);
            parents.Add(handle, parent);
            depths.Add(handle, depths[parent] + 1);
            Assert.Equal(depths[handle], arena.GetDepth(handle));
            Assert.Equal(parent, arena.GetParent(handle));
            Assert.Equal(ordinal, arena.GetValue(handle));

            var targetDepth = random.Next(depths[handle] + 2) - 1;
            Assert.Equal(
                NaiveAncestor(handle, targetDepth, parents, depths),
                arena.AncestorAtDepth(handle, targetDepth));
        }

        for (var query = 0; query < 12_000; query++)
        {
            var node = handles[random.Next(handles.Count)];
            var targetDepth = random.Next(depths[node] + 2) - 1;
            Assert.Equal(
                NaiveAncestor(node, targetDepth, parents, depths),
                arena.AncestorAtDepth(node, targetDepth));
        }

        Assert.Equal(arena.Bottom, arena.AncestorAtDepth(arena.Bottom, -1));
        var statistics = arena.GetStatistics();
        Assert.Equal(nodeCount, statistics.PublishedNodeCount);
        Assert.Equal(nodeCount, statistics.AddLeafCount);
        Assert.True(statistics.AncestorQueryCount >= nodeCount + 12_001L);
        Assert.InRange(statistics.LastAncestorHopCount, 0, 64);
        Assert.InRange(statistics.MaximumAncestorHopCount, 1, 64);
        Assert.True(statistics.TotalAncestorHopCount > 0);
    }

    /// <summary>Verifies square-boundary storage accounting for every small block transition.</summary>
    [Fact]
    public void MyersArena_OddBlockStatisticsMatchSquareBoundaries()
    {
        const int publishedLimit = 1_024;
        var arena = new MyersLevelAncestorArena<int>();

        for (var published = 0; published <= publishedLimit; published++)
        {
            var statistics = arena.GetStatistics();
            var highestBlock = (int)Math.Sqrt(published);
            var expectedBlockCount = highestBlock + 1;
            var expectedSlots = (long)expectedBlockCount * expectedBlockCount;

            Assert.Equal(published, statistics.PublishedNodeCount);
            Assert.Equal(published, statistics.AddLeafCount);
            Assert.Equal(expectedBlockCount, statistics.BlockCount);
            Assert.Equal(expectedSlots, statistics.AllocatedSlotCount);
            Assert.Equal(0, statistics.AncestorQueryCount);
            Assert.InRange(statistics.AllocatedSlotCount - (published + 1L), 0, 2L * highestBlock);

            if (published != publishedLimit)
                arena.AddLeaf(arena.Bottom, published);
        }
    }

    /// <summary>
    /// Verifies that concurrent additions below retained nodes publish unique immutable branches
    /// while reads of the common version stay stable.
    /// </summary>
    [Fact]
    public async Task ConcurrentBranchingAndReads_PreserveEverySnapshot()
    {
        const int workerCount = 8;
        const int steps = 240;
        var arena = new MyersLevelAncestorArena<int>();
        var root = BilateralAncestralDeque<int>.Create(arena);
        for (var value = 0; value < 96; value++)
            root = root.AddLast(value);
        var rootModel = Enumerable.Range(0, 96).ToArray();
        using var start = new ManualResetEventSlim(false);

        var tasks = Enumerable.Range(0, workerCount).Select(worker => Task.Run(() =>
        {
            start.Wait();
            var deque = root;
            var model = rootModel.ToList();
            var additions = 0;
            for (var step = 0; step < steps; step++)
            {
                switch (step % 8)
                {
                    case 0:
                        {
                            var value = -1 - worker * steps - step;
                            deque = deque.AddFirst(value);
                            model.Insert(0, value);
                            additions++;
                            break;
                        }
                    case 1:
                        {
                            var value = 10_000 + worker * steps + step;
                            deque = deque.AddLast(value);
                            model.Add(value);
                            additions++;
                            break;
                        }
                    case 2:
                        deque = deque.Reverse();
                        model.Reverse();
                        break;
                    case 3 when model.Count != 0:
                        deque = deque.RemoveFirst();
                        model.RemoveAt(0);
                        break;
                    case 4 when model.Count != 0:
                        deque = deque.RemoveLast();
                        model.RemoveAt(model.Count - 1);
                        break;
                    case 5:
                        {
                            var boundary = (worker * 17 + step) % (model.Count + 1);
                            var split = deque.SplitAt(boundary);
                            if (((worker + step) & 1) == 0)
                            {
                                deque = split.Left;
                                model.RemoveRange(boundary, model.Count - boundary);
                            }
                            else
                            {
                                deque = split.Right;
                                model.RemoveRange(0, boundary);
                            }

                            break;
                        }
                    case 6:
                        if (!rootModel.SequenceEqual(root.ToArray()))
                            throw new InvalidOperationException("A retained root changed during publication.");
                        break;
                    default:
                        if (step % 31 == 7)
                        {
                            deque = root;
                            model = rootModel.ToList();
                        }

                        break;
                }
            }

            return (deque, Model: model.ToArray(), additions);
        })).ToArray();

        start.Set();
        var results = await Task.WhenAll(tasks);
        foreach (var result in results)
            AssertDeque(result.Model, result.deque);
        AssertDeque(rootModel, root);

        var expectedPublications = rootModel.Length + results.Sum(result => result.additions);
        Assert.Equal(expectedPublications, arena.GetStatistics().PublishedNodeCount);
    }

    /// <summary>Verifies all public boundary and direct-arena input failures are deterministic.</summary>
    [Fact]
    public void InvalidInputs_ThrowWithoutPublishingNodes()
    {
        var arena = new MyersLevelAncestorArena<int>();
        var deque = BilateralAncestralDeque<int>.Create(arena).AddLast(1).AddLast(2).AddLast(3);

        Assert.Throws<ArgumentOutOfRangeException>(() => deque[-1]);
        Assert.Throws<ArgumentOutOfRangeException>(() => deque[3]);
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Take(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Take(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Drop(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Drop(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.SplitAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.SplitAt(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Slice(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Slice(0, -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Slice(4, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Slice(2, 2));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Slice(int.MaxValue, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => deque.Slice(1, int.MaxValue));

        var before = arena.GetStatistics();
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AddLeaf(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AddLeaf(int.MaxValue, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.GetDepth(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.GetDepth(int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.GetParent(arena.Bottom));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.GetParent(int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.GetValue(arena.Bottom));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.GetValue(int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AncestorAtDepth(-1, -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AncestorAtDepth(arena.Bottom, -2));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AncestorAtDepth(arena.Bottom, 0));
        var node = arena.AddLeaf(arena.Bottom, 4);
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AncestorAtDepth(node, -2));
        Assert.Throws<ArgumentOutOfRangeException>(() => arena.AncestorAtDepth(node, 1));

        var after = arena.GetStatistics();
        Assert.Equal(before.PublishedNodeCount + 1, after.PublishedNodeCount);
        Assert.Equal(before.AddLeafCount + 1, after.AddLeafCount);
        Assert.Equal(before.AncestorQueryCount, after.AncestorQueryCount);
        AssertDeque([1, 2, 3], deque);
    }

    private static void AssertDeque<T>(
        IEnumerable<T> expectedValues,
        BilateralAncestralDeque<T> actual)
    {
        var expected = expectedValues.ToArray();
        Assert.Equal(expected.Length, actual.Count);
        Assert.Equal(expected.Length == 0, actual.IsEmpty);
        Assert.Equal(expected, actual.ToArray());
        Assert.Equal(expected, actual.ToList());
        for (var index = 0; index < expected.Length; index++)
            Assert.Equal(expected[index], actual[index]);

        if (expected.Length != 0)
        {
            Assert.Equal(expected[0], actual.First);
            Assert.Equal(expected[^1], actual.Last);
        }

        actual.ValidateInvariants();
    }

    private static TResult AssertQueriesAtMost<TResult>(
        MyersLevelAncestorArena<int> arena,
        long maximum,
        Func<TResult> operation)
    {
        var before = arena.GetStatistics().AncestorQueryCount;
        var result = operation();
        var after = arena.GetStatistics().AncestorQueryCount;
        Assert.InRange(after - before, 0, maximum);
        return result;
    }

    private static int NaiveAncestor(
        int node,
        int targetDepth,
        IReadOnlyDictionary<int, int> parents,
        IReadOnlyDictionary<int, int> depths)
    {
        while (depths[node] > targetDepth)
            node = parents[node];
        return node;
    }

    private sealed class InvalidBottomArena<T> : IIncrementalLevelAncestorArena<T>
    {
        public int Bottom => 0;

        public int PublishedNodeCount => 0;

        public int AddLeaf(int parent, T value) => throw new NotSupportedException();

        public int GetDepth(int node) => 0;

        public int GetParent(int node) => throw new NotSupportedException();

        public int AncestorAtDepth(int node, int depth) => throw new NotSupportedException();

        public T GetValue(int node) => throw new NotSupportedException();
    }
}
