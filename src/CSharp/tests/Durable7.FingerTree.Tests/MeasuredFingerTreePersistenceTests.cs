using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies the general measured finger tree's lazy-memoized spine: correctness under fully persistent
/// (branching) histories, stack safety of the suspension cascade on very large trees, memoization of the
/// lazily computed measure, O(1) non-forcing endpoint reads, and thread-safe convergence of concurrent forces.
/// These exercise the machinery that makes the amortized bounds hold when old versions are retained and reused.
/// </summary>
public sealed class MeasuredFingerTreePersistenceTests
{
    private static FingerTree<int, int, SizeMeasure<int>> Seq(IEnumerable<int> values) =>
        FingerTree<int, int, SizeMeasure<int>>.CreateRange(values);

    /// <summary>
    /// Runs a randomized history that repeatedly branches off RETAINED earlier versions — the exact scenario
    /// the lazy spine exists for — and verifies every surviving version still reports the correct elements and
    /// measure. A version is never mutated, so reusing it through shared suspensions must not corrupt it.
    /// </summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    public void BranchingHistory_EveryRetainedVersionStaysCorrect(int seed)
    {
        var random = new Random(seed);
        var versions = new List<(FingerTree<int, int, SizeMeasure<int>> Tree, List<int> Model)>
        {
            (FingerTree<int, int, SizeMeasure<int>>.Empty, []),
        };

        for (var step = 0; step < 400; step++)
        {
            // Branch off an arbitrary retained version, not just the latest.
            var (tree, model) = versions[random.Next(versions.Count)];
            var newModel = new List<int>(model);
            FingerTree<int, int, SizeMeasure<int>> newTree;

            switch (random.Next(6))
            {
                case 0:
                    newTree = tree.Prepend(step);
                    newModel.Insert(0, step);
                    break;
                case 1:
                    newTree = tree.Append(step);
                    newModel.Add(step);
                    break;
                case 2 when !tree.IsEmpty:
                    tree.TryViewLeft(out _, out newTree);
                    newModel.RemoveAt(0);
                    break;
                case 3 when !tree.IsEmpty:
                    tree.TryViewRight(out _, out newTree);
                    newModel.RemoveAt(newModel.Count - 1);
                    break;
                case 4:
                {
                    var index = random.Next(newModel.Count + 1);
                    var (left, right) = tree.Split(m => m > index);
                    newTree = left.Concat(right);   // identity round-trip via a split boundary
                    break;
                }
                default:
                {
                    var other = versions[random.Next(versions.Count)];
                    newTree = tree.Concat(other.Tree);
                    newModel.AddRange(other.Model);
                    break;
                }
            }

            versions.Add((newTree, newModel));
        }

        // Every retained version — original and derived — must still be correct.
        foreach (var (tree, model) in versions)
        {
            Assert.Equal(model.Count, tree.Measure);
            Assert.Equal(model, tree.ToArray());
        }
    }

    /// <summary>
    /// Verifies the suspension cascade stays stack-safe on a very large tree. The pre-force-the-source
    /// discipline bounds the forcing recursion by the tree depth (O(log n)); a violation would build an
    /// O(n)-deep chain and overflow the stack on operations like these.
    /// </summary>
    [Fact]
    public void LargeTree_DeepOperations_StayStackSafe()
    {
        const int size = 1 << 20;   // ~1,048,576 elements
        var tree = Seq(Enumerable.Range(0, size));

        Assert.Equal(size, tree.Measure);
        Assert.Equal(0, tree.First);
        Assert.Equal(size - 1, tree.Last);

        // Drain a chunk from each end (forces deep pop suspensions repeatedly).
        var current = tree;
        for (var i = 0; i < 5000; i++)
        {
            current.TryViewLeft(out var head, out current);
            Assert.Equal(i, head);
        }

        Assert.Equal(size - 5000, current.Measure);

        // A deep split near the far end, then reconcatenate.
        var (left, right) = tree.Split(m => m > size - 3);
        Assert.Equal(size, left.Concat(right).Measure);

        // A long run of endpoint pushes does not overflow.
        for (var i = 0; i < 5000; i++)
            tree = tree.Prepend(-i);
        Assert.Equal(size + 5000, tree.Measure);
    }

    /// <summary>
    /// Verifies the lazily computed measure is memoized: a first read of a fresh tree's measure may force and
    /// allocate, but a second read is served from the cache and allocates essentially nothing.
    /// </summary>
    [Fact]
    public void Measure_IsMemoizedAfterFirstRead()
    {
        var tree = Seq(Enumerable.Range(0, 1 << 16));

        var before = GC.GetAllocatedBytesForCurrentThread();
        var first = tree.Measure;
        var afterFirst = GC.GetAllocatedBytesForCurrentThread();
        var second = tree.Measure;
        var afterSecond = GC.GetAllocatedBytesForCurrentThread();

        Assert.Equal(first, second);
        Assert.Equal(1 << 16, first);
        Assert.InRange(afterSecond - afterFirst, 0, 64);     // memoized: no recomputation/allocation
        Assert.True(afterFirst - before >= afterSecond - afterFirst);   // first read did the work
    }

    /// <summary>
    /// Verifies endpoint reads are O(1) and never force the spine: reading First/Last on a large tree allocates
    /// nothing, because they resolve in the prefix/suffix digits without touching the suspended middle.
    /// </summary>
    [Fact]
    public void EndpointReads_DoNotAllocate()
    {
        var tree = Seq(Enumerable.Range(0, 1 << 16));

        // Warm up the JIT before measuring.
        _ = tree.First;
        _ = tree.Last;

        var before = GC.GetAllocatedBytesForCurrentThread();
        for (var i = 0; i < 1000; i++)
        {
            _ = tree.First;
            _ = tree.Last;
        }

        Assert.InRange(GC.GetAllocatedBytesForCurrentThread() - before, 0, 64);
    }

    /// <summary>
    /// Verifies concurrent measure reads on a shared fresh tree converge: the compare-exchange in the memo
    /// cells means racing forcers may duplicate bounded work but all observe the same single value.
    /// </summary>
    [Fact]
    public void ConcurrentMeasureReads_Converge()
    {
        var tree = Seq(Enumerable.Range(0, 1 << 16));
        var results = new int[16];

        Parallel.For(0, results.Length, i => results[i] = tree.Measure);

        Assert.All(results, m => Assert.Equal(1 << 16, m));
    }

    /// <summary>
    /// Verifies a non-group measure (max, which has no inverse) is computed correctly through the lazy pop path,
    /// where the owning deep node's measure must be recovered by forcing rather than subtraction.
    /// </summary>
    [Fact]
    public void NonGroupMeasure_IsCorrectThroughLazyPops()
    {
        var values = Enumerable.Range(0, 5000).Select(i => (i * 7919) % 10007).ToArray();
        var tree = FingerTree<int, Optional<int>, MaxMeasure<int>>.CreateRange(values);

        var model = new List<int>(values);
        for (var i = 0; i < 4000; i++)
        {
            tree.TryViewLeft(out var head, out tree);
            Assert.Equal(model[i], head);
            var expectedMax = model.Skip(i + 1).DefaultIfEmpty(int.MinValue).Max();
            var measure = tree.Measure;
            if (i + 1 < model.Count)
                Assert.Equal(expectedMax, measure.Value);
        }
    }
}
