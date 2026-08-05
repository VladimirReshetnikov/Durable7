using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Extension-seam coverage for <see cref="IIncrementalAncestorArena{T}"/>. The seam is public, so a
/// caller-supplied arena that satisfies only the documented contract must drive
/// <see cref="AncestralSliceQueue{T}"/> and <see cref="BilateralAncestralDeque{T}"/> exactly as the
/// shipped <see cref="MyersIncrementalAncestorArena{T}"/> does, including the number of primitive
/// arena operations each collection issues.
/// </summary>
public sealed class IncrementalAncestorArenaSeamTests
{
    /// <summary>
    /// Verifies the naive parent-array arena is itself contract-faithful: it publishes the same
    /// handles and answers the same ancestor queries as the Myers arena on a branching history.
    /// </summary>
    [Fact]
    public void CustomArena_MatchesMyersArenaOnRandomizedBranchingHistory()
    {
        const int nodeCount = 2_048;
        var random = new Random(0x5EA_1);
        var myers = new MyersIncrementalAncestorArena<int>();
        var custom = new ParentArrayAncestorArena<int>();
        var handles = new List<int> { myers.Bottom };

        Assert.Equal(myers.Bottom, custom.Bottom);
        Assert.Equal(myers.PublishedNodeCount, custom.PublishedNodeCount);
        Assert.Equal(myers.GetDepth(myers.Bottom), custom.GetDepth(custom.Bottom));

        for (var value = 0; value < nodeCount; value++)
        {
            var parent = value % 3 == 0
                ? handles[random.Next(handles.Count)]
                : handles[^1];

            var myersHandle = myers.AddLeaf(parent, value);
            var customHandle = custom.AddLeaf(parent, value);
            Assert.Equal(myersHandle, customHandle);
            handles.Add(myersHandle);

            Assert.Equal(myers.PublishedNodeCount, custom.PublishedNodeCount);
            Assert.Equal(myers.GetDepth(myersHandle), custom.GetDepth(customHandle));
            Assert.Equal(myers.GetParent(myersHandle), custom.GetParent(customHandle));
            Assert.Equal(myers.GetValue(myersHandle), custom.GetValue(customHandle));
        }

        for (var query = 0; query < 8_000; query++)
        {
            var node = handles[random.Next(handles.Count)];
            var targetDepth = random.Next(myers.GetDepth(node) + 2) - 1;
            Assert.Equal(
                myers.AncestorAtDepth(node, targetDepth),
                custom.AncestorAtDepth(node, targetDepth));
        }

        // The failure modes must agree as well, so a collection cannot depend on backend-specific
        // rejection behavior.
        Assert.Throws<ArgumentOutOfRangeException>(() => custom.AddLeaf(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => custom.AddLeaf(int.MaxValue, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => custom.GetDepth(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => custom.GetParent(custom.Bottom));
        Assert.Throws<ArgumentOutOfRangeException>(() => custom.GetValue(custom.Bottom));
        Assert.Throws<ArgumentOutOfRangeException>(() => custom.AncestorAtDepth(custom.Bottom, -2));
        Assert.Throws<ArgumentOutOfRangeException>(() => custom.AncestorAtDepth(custom.Bottom, 0));
        Assert.Equal(nodeCount, custom.PublishedNodeCount);
    }

    /// <summary>
    /// Drives one randomized queue history through the shipped arena and a custom arena in lockstep
    /// and requires identical observable values and identical backend workloads at every step.
    /// </summary>
    [Fact]
    public void AncestralSliceQueue_OverCustomArena_MatchesTheMyersBackedQueue()
    {
        const int steps = 700;
        var random = new Random(0xC0FFEE);
        var myersArena = new MyersIncrementalAncestorArena<int>();
        var customArena = new ParentArrayAncestorArena<int>();
        var myers = AncestralSliceQueue<int>.Create(myersArena);
        var custom = AncestralSliceQueue<int>.Create(customArena);
        var model = new List<int>();
        var retained =
            new List<(int[] Model, AncestralSliceQueue<int> Myers, AncestralSliceQueue<int> Custom)>();

        AssertSameQueue(model, myers, custom, myersArena, customArena);

        for (var step = 0; step < steps; step++)
        {
            switch (random.Next(9))
            {
                case 0:
                case 1:
                case 2:
                    {
                        var value = step;
                        myers = myers.AddLast(value);
                        custom = custom.AddLast(value);
                        model.Add(value);
                        break;
                    }

                case 3 when model.Count != 0:
                    myers = myers.RemoveFirst();
                    custom = custom.RemoveFirst();
                    model.RemoveAt(0);
                    break;

                case 4 when model.Count != 0:
                    myers = myers.RemoveLast();
                    custom = custom.RemoveLast();
                    model.RemoveAt(model.Count - 1);
                    break;

                case 5:
                    {
                        var count = random.Next(model.Count + 1);
                        myers = myers.Take(count);
                        custom = custom.Take(count);
                        model.RemoveRange(count, model.Count - count);
                        break;
                    }

                case 6:
                    {
                        var count = random.Next(model.Count + 1);
                        myers = myers.Drop(count);
                        custom = custom.Drop(count);
                        model.RemoveRange(0, count);
                        break;
                    }

                case 7:
                    {
                        var index = random.Next(model.Count + 1);
                        var count = random.Next(model.Count - index + 1);
                        myers = myers.Slice(index, count);
                        custom = custom.Slice(index, count);
                        model = model.GetRange(index, count);
                        break;
                    }

                default:
                    {
                        var boundary = random.Next(model.Count + 1);
                        var myersSplit = myers.SplitAt(boundary);
                        var customSplit = custom.SplitAt(boundary);
                        var leftModel = model.GetRange(0, boundary);
                        var rightModel = model.GetRange(boundary, model.Count - boundary);

                        AssertSameQueue(leftModel, myersSplit.Left, customSplit.Left, myersArena, customArena);
                        AssertSameQueue(rightModel, myersSplit.Right, customSplit.Right, myersArena, customArena);

                        if ((step & 1) == 0)
                        {
                            myers = myersSplit.Left;
                            custom = customSplit.Left;
                            model = leftModel;
                        }
                        else
                        {
                            myers = myersSplit.Right;
                            custom = customSplit.Right;
                            model = rightModel;
                        }

                        break;
                    }
            }

            AssertSameQueue(model, myers, custom, myersArena, customArena);
            if (step % 37 == 0)
                retained.Add(([.. model], myers, custom));
        }

        Assert.NotEmpty(retained);
        Assert.True(customArena.PublishedNodeCount > 0, "the custom arena must have done the work");

        // Every retained version still reads identically after the whole history was published.
        foreach (var version in retained)
            AssertSameQueue(version.Model, version.Myers, version.Custom, myersArena, customArena);
    }

    /// <summary>
    /// Drives one randomized deque history through the shipped arena and a custom arena in lockstep
    /// and requires identical observable values and identical backend workloads at every step.
    /// </summary>
    [Fact]
    public void BilateralAncestralDeque_OverCustomArena_MatchesTheMyersBackedDeque()
    {
        const int steps = 700;
        var random = new Random(0xDEC0DE);
        var myersArena = new MyersIncrementalAncestorArena<int>();
        var customArena = new ParentArrayAncestorArena<int>();
        var myers = BilateralAncestralDeque<int>.Create(myersArena);
        var custom = BilateralAncestralDeque<int>.Create(customArena);
        var model = new List<int>();
        var retained = new List<(int[] Model, BilateralAncestralDeque<int> Myers,
            BilateralAncestralDeque<int> Custom)>();

        AssertSameDeque(model, myers, custom, myersArena, customArena);

        for (var step = 0; step < steps; step++)
        {
            switch (random.Next(11))
            {
                case 0:
                case 1:
                case 2:
                    {
                        var value = -1 - step;
                        myers = myers.AddFirst(value);
                        custom = custom.AddFirst(value);
                        model.Insert(0, value);
                        break;
                    }

                case 3:
                case 4:
                case 5:
                    {
                        var value = step;
                        myers = myers.AddLast(value);
                        custom = custom.AddLast(value);
                        model.Add(value);
                        break;
                    }

                case 6 when model.Count != 0:
                    myers = myers.RemoveFirst();
                    custom = custom.RemoveFirst();
                    model.RemoveAt(0);
                    break;

                case 7 when model.Count != 0:
                    myers = myers.RemoveLast();
                    custom = custom.RemoveLast();
                    model.RemoveAt(model.Count - 1);
                    break;

                case 8:
                    myers = myers.Reverse();
                    custom = custom.Reverse();
                    model.Reverse();
                    break;

                case 9:
                    {
                        var index = random.Next(model.Count + 1);
                        var count = random.Next(model.Count - index + 1);
                        myers = myers.Slice(index, count);
                        custom = custom.Slice(index, count);
                        model = model.GetRange(index, count);
                        break;
                    }

                default:
                    {
                        var boundary = random.Next(model.Count + 1);
                        var myersSplit = myers.SplitAt(boundary);
                        var customSplit = custom.SplitAt(boundary);
                        var leftModel = model.GetRange(0, boundary);
                        var rightModel = model.GetRange(boundary, model.Count - boundary);

                        AssertSameDeque(leftModel, myersSplit.Left, customSplit.Left, myersArena, customArena);
                        AssertSameDeque(rightModel, myersSplit.Right, customSplit.Right, myersArena, customArena);

                        if ((step & 1) == 0)
                        {
                            myers = myersSplit.Left;
                            custom = customSplit.Left;
                            model = leftModel;
                        }
                        else
                        {
                            myers = myersSplit.Right;
                            custom = customSplit.Right;
                            model = rightModel;
                        }

                        break;
                    }
            }

            AssertSameDeque(model, myers, custom, myersArena, customArena);
            if (step % 37 == 0)
                retained.Add(([.. model], myers, custom));
        }

        Assert.NotEmpty(retained);
        Assert.True(customArena.PublishedNodeCount > 0, "the custom arena must have done the work");

        foreach (var version in retained)
            AssertSameDeque(version.Model, version.Myers, version.Custom, myersArena, customArena);
    }

    /// <summary>Verifies both collections' explicit factories accept an arbitrary seam implementation.</summary>
    [Fact]
    public void Factories_AcceptAnArbitrarySeamImplementation()
    {
        var queueArena = new ParentArrayAncestorArena<string>();
        var queue = AncestralSliceQueue<string>.Create(queueArena).AddLast("a").AddLast("b");
        Assert.Equal(new[] { "a", "b" }, queue.ToArray());
        Assert.Equal(2, queueArena.PublishedNodeCount);

        var dequeArena = new ParentArrayAncestorArena<string>();
        var deque = BilateralAncestralDeque<string>.Create(dequeArena).AddLast("b").AddFirst("a");
        Assert.Equal(new[] { "a", "b" }, deque.ToArray());
        Assert.Equal(2, dequeArena.PublishedNodeCount);

        // The shared seam is the only contract either factory validates.
        Assert.Throws<ArgumentException>(() =>
            AncestralSliceQueue<int>.Create(new NonNegativeBottomDepthArena<int>()));
        Assert.Throws<ArgumentException>(() =>
            BilateralAncestralDeque<int>.Create(new NonNegativeBottomDepthArena<int>()));
    }

    private static void AssertSameQueue(
        IReadOnlyList<int> expected,
        AncestralSliceQueue<int> myers,
        AncestralSliceQueue<int> custom,
        MyersIncrementalAncestorArena<int> myersArena,
        ParentArrayAncestorArena<int> customArena)
    {
        Assert.Equal(expected.Count, myers.Count);
        Assert.Equal(expected.Count, custom.Count);
        Assert.Equal(expected.Count == 0, myers.IsEmpty);
        Assert.Equal(expected.Count == 0, custom.IsEmpty);
        Assert.Equal(expected, myers.ToArray());
        Assert.Equal(expected, custom.ToArray());

        for (var index = 0; index < expected.Count; index++)
        {
            Assert.Equal(expected[index], myers[index]);
            Assert.Equal(expected[index], custom[index]);
        }

        if (expected.Count == 0)
        {
            Assert.Throws<InvalidOperationException>(() => myers.First);
            Assert.Throws<InvalidOperationException>(() => custom.First);
            Assert.Throws<InvalidOperationException>(() => myers.Last);
            Assert.Throws<InvalidOperationException>(() => custom.Last);
        }
        else
        {
            Assert.Equal(expected[0], myers.First);
            Assert.Equal(expected[0], custom.First);
            Assert.Equal(expected[^1], myers.Last);
            Assert.Equal(expected[^1], custom.Last);
        }

        AssertSameArenaWorkload(myersArena, customArena);
    }

    private static void AssertSameDeque(
        IReadOnlyList<int> expected,
        BilateralAncestralDeque<int> myers,
        BilateralAncestralDeque<int> custom,
        MyersIncrementalAncestorArena<int> myersArena,
        ParentArrayAncestorArena<int> customArena)
    {
        Assert.Equal(expected.Count, myers.Count);
        Assert.Equal(expected.Count, custom.Count);
        Assert.Equal(expected.Count == 0, myers.IsEmpty);
        Assert.Equal(expected.Count == 0, custom.IsEmpty);
        Assert.Equal(expected, myers.ToArray());
        Assert.Equal(expected, custom.ToArray());
        Assert.Equal(expected, myers.ToList());
        Assert.Equal(expected, custom.ToList());

        for (var index = 0; index < expected.Count; index++)
        {
            Assert.Equal(expected[index], myers[index]);
            Assert.Equal(expected[index], custom[index]);
        }

        if (expected.Count != 0)
        {
            Assert.Equal(expected[0], myers.First);
            Assert.Equal(expected[0], custom.First);
            Assert.Equal(expected[^1], myers.Last);
            Assert.Equal(expected[^1], custom.Last);
        }

        myers.ValidateInvariants();
        custom.ValidateInvariants();
        AssertSameArenaWorkload(myersArena, customArena);
    }

    /// <summary>
    /// Requires both backends to have been asked for exactly the same primitive work, which pins the
    /// collection to the seam rather than to any Myers-specific representation detail.
    /// </summary>
    private static void AssertSameArenaWorkload(
        MyersIncrementalAncestorArena<int> myersArena,
        ParentArrayAncestorArena<int> customArena)
    {
        var statistics = myersArena.GetStatistics();
        Assert.Equal(statistics.PublishedNodeCount, customArena.PublishedNodeCount);
        Assert.Equal(statistics.AddLeafCount, customArena.AddLeafCount);
        Assert.Equal(statistics.AncestorQueryCount, customArena.AncestorQueryCount);
    }

    /// <summary>
    /// A minimal contract-faithful <see cref="IIncrementalAncestorArena{T}"/>: one parent array plus
    /// a naive ancestor walk. Additions are O(1) amortized and queries are O(depth), which the seam
    /// permits; the collections must not depend on anything faster.
    /// </summary>
    /// <typeparam name="T">The value stored in each non-bottom node.</typeparam>
    private sealed class ParentArrayAncestorArena<T> : IIncrementalAncestorArena<T>
    {
        private readonly List<int> _parents = [0];
        private readonly List<int> _depths = [-1];
        private readonly List<T> _values = [default!];

        public int Bottom => 0;

        public int PublishedNodeCount => _parents.Count - 1;

        /// <summary>Gets the number of successful leaf additions.</summary>
        internal long AddLeafCount { get; private set; }

        /// <summary>Gets the number of completed ancestor queries.</summary>
        internal long AncestorQueryCount { get; private set; }

        public int AddLeaf(int parent, T value)
        {
            EnsurePublished(parent);
            var handle = _parents.Count;
            _parents.Add(parent);
            _depths.Add(checked(_depths[parent] + 1));
            _values.Add(value);
            AddLeafCount++;
            return handle;
        }

        public int GetDepth(int node)
        {
            EnsurePublished(node);
            return _depths[node];
        }

        public int GetParent(int node)
        {
            if (node == Bottom)
                throw new ArgumentOutOfRangeException(nameof(node), "The bottom node has no parent.");
            EnsurePublished(node);
            return _parents[node];
        }

        public int AncestorAtDepth(int node, int depth)
        {
            EnsurePublished(node);
            if (depth < -1 || depth > _depths[node])
                throw new ArgumentOutOfRangeException(nameof(depth));

            while (_depths[node] > depth)
                node = _parents[node];

            AncestorQueryCount++;
            return node;
        }

        public T GetValue(int node)
        {
            if (node == Bottom)
                throw new ArgumentOutOfRangeException(nameof(node), "The bottom node has no value.");
            EnsurePublished(node);
            return _values[node];
        }

        private void EnsurePublished(int node)
        {
            if ((uint)node >= (uint)_parents.Count)
                throw new ArgumentOutOfRangeException(nameof(node));
        }
    }

    /// <summary>An arena whose bottom node violates the depth -1 requirement of the seam.</summary>
    /// <typeparam name="T">The value stored in each non-bottom node.</typeparam>
    private sealed class NonNegativeBottomDepthArena<T> : IIncrementalAncestorArena<T>
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
