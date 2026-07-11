using System.Numerics;
using System.Reflection;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Adversarial policy-coherence, interface-contract, invariant, and persistence coverage for
/// <see cref="CanonicalSortedSet{T}"/>.
/// </summary>
public sealed class CanonicalSortedSetAdversarialTests
{
    /// <summary>
    /// The factory must reject an omitted rank hash for a custom comparer, and operations must
    /// detect a caller-supplied hash that violates comparer equivalence.
    /// </summary>
    [Fact]
    public void OmittedOrIncoherentRankHash_CannotCorruptCustomComparerCanonicality()
    {
        Assert.Throws<ArgumentException>(() => ZipTreeRankPolicy<string>.Create(
            comparer: StringComparer.OrdinalIgnoreCase,
            seed: 0x67d8_2f31_910a_bc45UL));
        Assert.Throws<ArgumentException>(() => ZipTreeRankPolicy<string>.CreateKeyed(
            new byte[32],
            comparer: StringComparer.OrdinalIgnoreCase));

        var inconsistentPolicy = ZipTreeRankPolicy<string>.Create(
            comparer: StringComparer.OrdinalIgnoreCase,
            rankHash: value => char.IsUpper(value[0]) ? 1UL : 2UL,
            seed: 17);
        var set = CanonicalSortedSet<string>.Create(inconsistentPolicy).Add("alpha");

        var incrementalError = Assert.Throws<InvalidOperationException>(() => set.Add("ALPHA"));
        var bulkError = Assert.Throws<InvalidOperationException>(() =>
            CanonicalSortedSet<string>.CreateRange(["alpha", "ALPHA"], inconsistentPolicy));
        Assert.Contains("equivalence classes", incrementalError.Message, StringComparison.Ordinal);
        Assert.Equal(incrementalError.Message, bulkError.Message);
    }

    /// <summary>
    /// Both the concrete and <see cref="IReadOnlySet{T}"/> equality operations must compare the
    /// represented mathematical sets when the rank-policy objects differ.
    /// </summary>
    [Fact]
    public void IReadOnlySet_SetEquals_DoesNotLeakCanonicalRankPolicyIdentity()
    {
        var first = CanonicalSortedSet<int>.CreateRange(
            [1, 2, 3, 5, 8, 13],
            ZipTreeRankPolicy<int>.Create(seed: 11));
        var second = CanonicalSortedSet<int>.CreateRange(
            [13, 8, 5, 3, 2, 1],
            ZipTreeRankPolicy<int>.Create(seed: 97));

        Assert.True(first.SetEquals(second));

        IReadOnlySet<int> firstView = first;
        IReadOnlySet<int> secondView = second;
        Assert.True(firstView.SetEquals(secondView));
        Assert.True(secondView.SetEquals(firstView));
        Assert.True(firstView.IsSubsetOf(secondView));
        Assert.True(firstView.IsSupersetOf(secondView));
        Assert.False(firstView.IsProperSubsetOf(secondView));
        Assert.False(firstView.IsProperSupersetOf(secondView));
    }

    /// <summary>Exercises all read-only set relations against ordinary comparer-compatible sets.</summary>
    [Fact]
    public void IReadOnlySet_Relations_InteroperateWithOrdinarySetsAndDuplicateEnumerables()
    {
        var policy = ZipTreeRankPolicy<string>.Create(
            StringComparer.OrdinalIgnoreCase,
            value => unchecked((ulong)StringComparer.OrdinalIgnoreCase.GetHashCode(value)),
            seed: 23);
        IReadOnlySet<string> canonical = CanonicalSortedSet<string>.CreateRange(
            ["Alpha", "bravo", "CHARLIE"],
            policy);
        IReadOnlySet<string> equalSorted = new System.Collections.Generic.SortedSet<string>(
            ["alpha", "BRAVO", "charlie"],
            StringComparer.OrdinalIgnoreCase);
        IReadOnlySet<string> equalHash = new System.Collections.Generic.HashSet<string>(
            ["ALPHA", "Bravo", "Charlie"],
            StringComparer.OrdinalIgnoreCase);
        IReadOnlySet<string> properSuperset = new System.Collections.Generic.SortedSet<string>(
            ["alpha", "bravo", "charlie", "delta"],
            StringComparer.OrdinalIgnoreCase);
        var duplicateSuperset = new[] { "ALPHA", "alpha", "BRAVO", "charlie", "DELTA", "delta" };

        Assert.True(canonical.SetEquals(equalSorted));
        Assert.True(equalSorted.SetEquals(canonical));
        Assert.True(canonical.SetEquals(equalHash));
        Assert.True(equalHash.SetEquals(canonical));
        Assert.True(canonical.IsSubsetOf(properSuperset));
        Assert.True(canonical.IsProperSubsetOf(properSuperset));
        Assert.True(properSuperset.IsSupersetOf(canonical));
        Assert.True(properSuperset.IsProperSupersetOf(canonical));
        Assert.True(canonical.IsProperSubsetOf(duplicateSuperset));
        Assert.False(canonical.SetEquals(duplicateSuperset));
        Assert.True(canonical.Overlaps(new[] { "absent", "BRAVO" }));
        Assert.False(canonical.Overlaps(new[] { "absent", "missing" }));
    }

    /// <summary>Checks caller-owned HMAC key copying and cross-policy rank reproducibility.</summary>
    [Fact]
    public void CreateKeyed_ReproducesCanonicalShapeDigestAndValidationStatistics()
    {
        var key = Enumerable.Range(0, 32).Select(index => checked((byte)(index * 7 + 3))).ToArray();
        var retainedKey = key.ToArray();
        static ulong RankHash(int value) => unchecked((ulong)(uint)value);

        var firstPolicy = ZipTreeRankPolicy<int>.CreateKeyed(key, rankHash: RankHash);
        Array.Fill(key, byte.MaxValue);
        var secondPolicy = ZipTreeRankPolicy<int>.CreateKeyed(retainedKey, rankHash: RankHash);
        var values = Enumerable.Range(-1_000, 2_001).Where(value => value % 3 != 0).ToArray();
        var first = CanonicalSortedSet<int>.CreateRange(values, firstPolicy);
        var second = CanonicalSortedSet<int>.CreateRange(values.Reverse(), secondPolicy);

        Assert.NotSame(firstPolicy, secondPolicy);
        Assert.True(first.SetEquals(second));
        Assert.Equal(first.ContentHash, second.ContentHash);
        Assert.Equal(first.ShapeForTesting(), second.ShapeForTesting());
        Assert.Equal(first.ValidateStructure(), second.ValidateStructure());
        AssertValid(first);
        AssertValid(second);
        Assert.Throws<ArgumentException>(() => ZipTreeRankPolicy<int>.CreateKeyed(new byte[31]));
    }

    /// <summary>
    /// Constant ranks force the comparer tie-break into a maximally deep tree; all recursive and
    /// iterative operations must nevertheless preserve the canonical representation.
    /// </summary>
    [Fact]
    public void FullyCollidingRanks_SurviveDeepDeleteReinsertAndDigestTraversal()
    {
        const int count = 4_096;
        var policy = ZipTreeRankPolicy<int>.Create(rankHash: _ => 0, seed: 1);
        var set = CanonicalSortedSet<int>.CreateRange(Enumerable.Range(0, count).Reverse(), policy);

        Assert.Equal(count, set.Height);
        AssertValid(set);

        var withoutLast = set.Remove(count - 1);
        Assert.Equal(count - 1, withoutLast.Height);
        AssertValid(withoutLast);

        var restored = withoutLast.Add(count - 1);
        Assert.Equal(set.ContentHash, restored.ContentHash);
        Assert.Equal(set.ShapeForTesting(), restored.ShapeForTesting());
        Assert.True(set.SetEquals(restored));
        AssertValid(restored);
    }

    /// <summary>
    /// Runs a reproducible mixed history against a BCL model, checks every retained snapshot, and
    /// periodically rebuilds the same contents through an independent shuffled history.
    /// </summary>
    [Fact]
    public void RandomizedHistories_PreserveModelInvariantsDepthDigestAndConvergence()
    {
        var policy = ZipTreeRankPolicy<int>.Create(seed: 0x0ddc_0ffe_e15e_beefUL);
        var set = CanonicalSortedSet<int>.Create(policy);
        var model = new System.Collections.Generic.SortedSet<int>();
        var snapshots = new List<(CanonicalSortedSet<int> Set, int[] Contents, ulong Digest)>();
        var random = new Random(0x5eed_711);

        for (var operation = 0; operation < 12_000; operation++)
        {
            var item = random.Next(-2_500, 2_501);
            if (random.Next(5) < 3)
            {
                set = set.Add(item);
                model.Add(item);
            }
            else
            {
                set = set.Remove(item);
                model.Remove(item);
            }

            if (operation % 503 == 0)
            {
                Assert.Equal(model.ToArray(), set.ToArray());
                AssertValid(set);
                var shuffled = model.OrderBy(_ => random.Next()).ToArray();
                var rebuilt = CanonicalSortedSet<int>.CreateRange(shuffled, policy);
                Assert.Equal(set.ShapeForTesting(), rebuilt.ShapeForTesting());
                Assert.Equal(set.ContentHash, rebuilt.ContentHash);
                Assert.True(set.SetEquals(rebuilt));
            }

            if (operation % 997 == 0)
                snapshots.Add((set, [.. model], set.ContentHash));
        }

        Assert.Equal(model.ToArray(), set.ToArray());
        AssertValid(set);
        Assert.InRange(set.Height, 0, (int)(8 * Math.Log2(set.Count + 1) + 16));
        foreach (var snapshot in snapshots)
        {
            Assert.Equal(snapshot.Contents, snapshot.Set);
            Assert.Equal(snapshot.Digest, snapshot.Set.ContentHash);
            AssertValid(snapshot.Set);
        }
    }

    /// <summary>Checks no-op instance identity and sharing of an untouched root branch.</summary>
    [Fact]
    public void NoOpsPreserveIdentity_AndLocalizedEditSharesUntouchedRootBranch()
    {
        var policy = ZipTreeRankPolicy<int>.Create(seed: 0x51a7_1cUL);
        var set = CanonicalSortedSet<int>.CreateRange(Enumerable.Range(0, 1_000), policy);
        var empty = CanonicalSortedSet<int>.Create(policy);

        Assert.Same(set, set.Add(500));
        Assert.Same(set, set.Remove(-1));
        Assert.Same(set, set.Union(set));
        Assert.Same(set, set.Intersect(set));
        Assert.Same(set, set.Except(empty));
        Assert.Same(empty, empty.Clear());
        Assert.Same(empty, empty.Remove(0));

        Assert.NotNull(set.RootIdentity);
        var root = set.RootIdentity!;
        var rootItem = set.ShapeForTesting()[0].Item;
        var left = GetChild(root, "Left");
        var right = GetChild(root, "Right");
        var (candidate, sharedProperty) = FindLocalizedCandidate(set, rootItem, left, right);
        var sharedBefore = GetChild(root, sharedProperty);

        var updated = set.Add(candidate);
        Assert.NotNull(updated.RootIdentity);
        var updatedRoot = updated.RootIdentity!;
        var sharedAfter = GetChild(updatedRoot, sharedProperty);

        Assert.NotSame(set, updated);
        Assert.NotSame(root, updatedRoot);
        Assert.NotNull(sharedBefore);
        Assert.Same(sharedBefore, sharedAfter);
        AssertValid(set);
        AssertValid(updated);
    }

    private static (int Candidate, string SharedProperty) FindLocalizedCandidate(
        CanonicalSortedSet<int> set,
        int rootItem,
        object? left,
        object? right)
    {
        var rootRank = set.Policy.GetRank(rootItem);
        if (right is not null)
        {
            for (var candidate = -1; candidate > -100_000; candidate--)
            {
                if (!Higher(candidate, set.Policy.GetRank(candidate), rootItem, rootRank, set.Policy.Comparer))
                    return (candidate, "Right");
            }
        }

        if (left is not null)
        {
            for (var candidate = 1_000; candidate < 100_000; candidate++)
            {
                if (!Higher(candidate, set.Policy.GetRank(candidate), rootItem, rootRank, set.Policy.Comparer))
                    return (candidate, "Left");
            }
        }

        throw new Xunit.Sdk.XunitException("Could not find a root-localized candidate for the pinned policy.");
    }

    private static object? GetChild(object node, string propertyName) =>
        node.GetType()
            .GetProperty(propertyName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)!
            .GetValue(node);

    private static void AssertValid<T>(CanonicalSortedSet<T> set)
    {
        var shape = set.ShapeForTesting();
        var statistics = set.ValidateStructure();
        Assert.Equal(set.Count, statistics.Count);
        Assert.Equal(set.Height, statistics.Height);
        Assert.Equal(set.Count, shape.Length);

        if (shape.Length != 0)
        {
            var visited = new bool[shape.Length];
            var ranks = new ZipTreeRankPolicy<T>.Rank[shape.Length];
            var pending = new Stack<ShapeValidationFrame<T>>();
            pending.Push(new ShapeValidationFrame<T>(
                Index: 0,
                ExpectedCount: set.Count,
                Depth: 1,
                LowerBound: default,
                HasLowerBound: false,
                UpperBound: default,
                HasUpperBound: false,
                ParentItem: default,
                ParentRank: default,
                HasParent: false));
            var maximumDepth = 0;
            var visitedCount = 0;
            while (pending.TryPop(out var frame))
            {
                Assert.InRange(frame.Index, 0, shape.Length - 1);
                Assert.False(visited[frame.Index]);
                visited[frame.Index] = true;
                visitedCount++;

                var node = shape[frame.Index];
                Assert.Equal(frame.ExpectedCount, checked(1 + node.LeftCount + node.RightCount));
                if (frame.HasLowerBound)
                    Assert.True(set.Policy.Comparer.Compare(frame.LowerBound!, node.Item) < 0);
                if (frame.HasUpperBound)
                    Assert.True(set.Policy.Comparer.Compare(node.Item, frame.UpperBound!) < 0);

                var rank = set.Policy.GetRank(node.Item);
                ranks[frame.Index] = rank;
                if (frame.HasParent)
                    Assert.False(Higher(node.Item, rank, frame.ParentItem!, frame.ParentRank, set.Policy.Comparer));
                maximumDepth = Math.Max(maximumDepth, frame.Depth);

                if (node.RightCount != 0)
                {
                    pending.Push(new ShapeValidationFrame<T>(
                        Index: checked(frame.Index + 1 + node.LeftCount),
                        ExpectedCount: node.RightCount,
                        Depth: checked(frame.Depth + 1),
                        LowerBound: node.Item,
                        HasLowerBound: true,
                        UpperBound: frame.UpperBound,
                        HasUpperBound: frame.HasUpperBound,
                        ParentItem: node.Item,
                        ParentRank: rank,
                        HasParent: true));
                }
                if (node.LeftCount != 0)
                {
                    pending.Push(new ShapeValidationFrame<T>(
                        Index: checked(frame.Index + 1),
                        ExpectedCount: node.LeftCount,
                        Depth: checked(frame.Depth + 1),
                        LowerBound: frame.LowerBound,
                        HasLowerBound: frame.HasLowerBound,
                        UpperBound: node.Item,
                        HasUpperBound: true,
                        ParentItem: node.Item,
                        ParentRank: rank,
                        HasParent: true));
                }
            }

            Assert.Equal(shape.Length, visitedCount);
            Assert.Equal(set.Height, maximumDepth);

            var subtreeCounts = new int[shape.Length];
            var subtreeHeights = new int[shape.Length];
            var digests = new ulong[shape.Length];
            for (var index = shape.Length - 1; index >= 0; index--)
            {
                var node = shape[index];
                var leftIndex = checked(index + 1);
                var rightIndex = checked(leftIndex + node.LeftCount);
                var leftCount = node.LeftCount == 0 ? 0 : subtreeCounts[leftIndex];
                var rightCount = node.RightCount == 0 ? 0 : subtreeCounts[rightIndex];
                Assert.Equal(node.LeftCount, leftCount);
                Assert.Equal(node.RightCount, rightCount);

                var leftHeight = node.LeftCount == 0 ? 0 : subtreeHeights[leftIndex];
                var rightHeight = node.RightCount == 0 ? 0 : subtreeHeights[rightIndex];
                var leftDigest = node.LeftCount == 0 ? 0x243f6a8885a308d3UL : digests[leftIndex];
                var rightDigest = node.RightCount == 0 ? 0x13198a2e03707344UL : digests[rightIndex];
                subtreeCounts[index] = checked(1 + leftCount + rightCount);
                subtreeHeights[index] = checked(1 + Math.Max(leftHeight, rightHeight));
                digests[index] = ZipTreeRankPolicy<T>.Mix(
                    ranks[index].Hash
                    ^ BitOperations.RotateLeft(leftDigest, 17)
                    ^ BitOperations.RotateLeft(rightDigest, 43));
            }

            Assert.Equal(set.Count, subtreeCounts[0]);
            Assert.Equal(set.Height, subtreeHeights[0]);
            Assert.Equal(set.ContentHash, digests[0]);
        }
        else
        {
            Assert.Equal(0UL, set.ContentHash);
        }

        var items = set.ToArray();
        for (var index = 1; index < items.Length; index++)
            Assert.True(set.Policy.Comparer.Compare(items[index - 1], items[index]) < 0);
    }

    private static bool Higher<T>(
        T leftItem,
        ZipTreeRankPolicy<T>.Rank left,
        T rightItem,
        ZipTreeRankPolicy<T>.Rank right,
        IComparer<T> comparer)
    {
        var comparison = left.Geometric.CompareTo(right.Geometric);
        if (comparison != 0)
            return comparison > 0;
        comparison = left.Secondary.CompareTo(right.Secondary);
        if (comparison != 0)
            return comparison > 0;
        return comparer.Compare(leftItem, rightItem) < 0;
    }

    private readonly record struct ShapeValidationFrame<T>(
        int Index,
        int ExpectedCount,
        int Depth,
        T? LowerBound,
        bool HasLowerBound,
        T? UpperBound,
        bool HasUpperBound,
        T? ParentItem,
        ZipTreeRankPolicy<T>.Rank ParentRank,
        bool HasParent);
}
