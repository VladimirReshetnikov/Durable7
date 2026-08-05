using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>
/// Verifies version-tagged union edges, fully branching histories, persistence, and the first-connection query.
/// </summary>
public sealed class PersistentAncestralConnectionForestTests
{
    /// <summary>Verifies an edgeless root uses the fixed universe and root-version semantics.</summary>
    [Fact]
    public void Create_ProducesSingletonComponentsAndRootHistory()
    {
        var forest = PersistentAncestralConnectionForest.Create(5);

        Assert.Equal(5, forest.VertexCount);
        Assert.Equal(5, forest.ComponentCount);
        Assert.Equal(0, forest.Version.Depth);
        Assert.Null(forest.Version.Parent);
        Assert.Same(forest.Version, forest.Version.Root);
        for (var vertex = 0; vertex < forest.VertexCount; vertex++)
        {
            Assert.Equal(vertex, forest.Find(vertex));
            Assert.True(forest.Connected(vertex, vertex));
            Assert.Same(forest.Version, forest.FirstConnected(vertex, vertex));
            Assert.True(forest.TryGetFirstConnected(vertex, vertex, out var self));
            Assert.Same(forest.Version, self);
        }

        Assert.False(forest.Connected(0, 1));
        Assert.Null(forest.FirstConnected(0, 1));
        Assert.False(forest.TryGetFirstConnected(0, 1, out var absent));
        Assert.Null(absent);
        forest.ValidateInvariants();

        var empty = PersistentAncestralConnectionForest.Create(0);
        Assert.Equal(0, empty.VertexCount);
        Assert.Equal(0, empty.ComponentCount);
        empty.ValidateInvariants();
        Assert.Throws<ArgumentOutOfRangeException>(() => PersistentAncestralConnectionForest.Create(-1));
    }

    /// <summary>Verifies successful links retain the exact first version for old and cross-component pairs.</summary>
    [Fact]
    public void Link_RecordsTheFirstConnectionVersion()
    {
        var root = PersistentAncestralConnectionForest.Create(6);
        var first = root.Link(0, 1);
        var second = first.Link(2, 3);
        var bridge = second.Link(0, 2);
        var attach = bridge.Link(4, 0);

        Assert.Equal(2, attach.ComponentCount);
        Assert.Same(first.Version, attach.FirstConnected(0, 1));
        Assert.Same(second.Version, attach.FirstConnected(2, 3));
        Assert.Same(bridge.Version, attach.FirstConnected(0, 3));
        Assert.Same(bridge.Version, attach.FirstConnected(1, 2));
        Assert.Same(attach.Version, attach.FirstConnected(4, 1));
        Assert.Same(root.Version, attach.FirstConnected(4, 4));
        Assert.Null(attach.FirstConnected(0, 5));
        Assert.True(attach.TryGetFirstConnected(1, 2, out var firstCrossing));
        Assert.Same(bridge.Version, firstCrossing);

        Assert.Equal(0, attach.Find(0));
        Assert.Equal(0, attach.Find(4));
        Assert.Equal(6, root.ComponentCount);
        Assert.False(root.Connected(0, 1));
        Assert.False(first.Connected(0, 2));
        root.ValidateInvariants();
        first.ValidateInvariants();
        second.ValidateInvariants();
        bridge.ValidateInvariants();
        attach.ValidateInvariants();
    }

    /// <summary>Verifies component sizes are cached-root reads that agree with the connectivity partition.</summary>
    [Fact]
    public void GetComponentSize_ReportsTheCachedUnionBySizeComponentCount()
    {
        var root = PersistentAncestralConnectionForest.Create(6);

        for (var vertex = 0; vertex < root.VertexCount; vertex++)
            Assert.Equal(1, root.GetComponentSize(vertex));

        var first = root.Link(0, 1);
        Assert.Equal(2, first.GetComponentSize(0));
        Assert.Equal(2, first.GetComponentSize(1));
        Assert.Equal(1, first.GetComponentSize(2));

        var second = first.Link(2, 3);
        var bridge = second.Link(0, 2);
        var attach = bridge.Link(4, 0);
        Assert.Equal(2, second.GetComponentSize(3));
        Assert.Equal(4, bridge.GetComponentSize(3));
        Assert.Equal(5, attach.GetComponentSize(4));
        Assert.Equal(1, attach.GetComponentSize(5));
        for (var vertex = 0; vertex < 5; vertex++)
            Assert.Equal(5, attach.GetComponentSize(vertex));

        // A redundant link changes no size, and retained versions keep their own sizes.
        var redundant = attach.Link(1, 4);
        Assert.Equal(5, redundant.GetComponentSize(1));
        Assert.Equal(1, redundant.GetComponentSize(5));
        Assert.Equal(2, first.GetComponentSize(0));
        Assert.Equal(4, bridge.GetComponentSize(0));

        // Sibling branches over the same ancestor keep independent sizes.
        var sibling = bridge.Link(5, 0);
        Assert.Equal(5, sibling.GetComponentSize(5));
        Assert.Equal(1, attach.GetComponentSize(5));
        Assert.Equal(4, bridge.GetComponentSize(0));

        foreach (var forest in new[] { root, first, second, bridge, attach, redundant, sibling })
        {
            var componentSizes = new Dictionary<int, int>();
            for (var vertex = 0; vertex < forest.VertexCount; vertex++)
                componentSizes[forest.Find(vertex)] = forest.GetComponentSize(vertex);

            Assert.Equal(forest.ComponentCount, componentSizes.Count);
            Assert.Equal(forest.VertexCount, componentSizes.Values.Sum());
            forest.ValidateInvariants();
        }
    }

    /// <summary>Verifies redundant events advance history but retain the exact persistent connectivity root.</summary>
    [Fact]
    public void RedundantLinks_CreateVersionsAndShareConnectivityState()
    {
        var root = PersistentAncestralConnectionForest.Create(4);
        var linked = root.Link(0, 1);
        var reverseDuplicate = linked.Link(1, 0);
        var siblingDuplicate = linked.Link(1, 0);
        var selfDuplicate = reverseDuplicate.Link(0, 0);

        Assert.Equal(1, linked.Version.Depth);
        Assert.Equal(2, reverseDuplicate.Version.Depth);
        Assert.Equal(3, selfDuplicate.Version.Depth);
        Assert.Same(linked.Version, reverseDuplicate.Version.Parent);
        Assert.Same(linked.Version, siblingDuplicate.Version.Parent);
        Assert.Same(reverseDuplicate.Version, selfDuplicate.Version.Parent);
        Assert.NotSame(linked.Version, reverseDuplicate.Version);
        Assert.NotSame(reverseDuplicate.Version, siblingDuplicate.Version);
        Assert.False(reverseDuplicate.Version.Equals(siblingDuplicate.Version));
        Assert.Equal(linked.ComponentCount, reverseDuplicate.ComponentCount);
        Assert.Equal(linked.ComponentCount, selfDuplicate.ComponentCount);
        Assert.True(linked.SharesConnectivityRootWith(reverseDuplicate));
        Assert.True(reverseDuplicate.SharesConnectivityRootWith(selfDuplicate));
        Assert.False(root.SharesConnectivityRootWith(linked));
        Assert.Same(linked.Version, selfDuplicate.FirstConnected(0, 1));
        Assert.Same(root.Version, selfDuplicate.FirstConnected(2, 2));
        selfDuplicate.ValidateInvariants();
    }

    /// <summary>Verifies sibling versions at equal depths never confuse their union-edge witnesses.</summary>
    [Fact]
    public void SiblingBranches_KeepEqualDepthVersionTagsDistinct()
    {
        var root = PersistentAncestralConnectionForest.Create(5);
        var common = root.Link(0, 1);
        var left = common.Link(0, 2);
        var right = common.Link(0, 3);
        var leftLater = left.Link(2, 4);
        var rightLater = right.Link(3, 4);

        Assert.Equal(left.Version.Depth, right.Version.Depth);
        Assert.NotSame(left.Version, right.Version);
        Assert.Same(common.Version, left.FirstConnected(0, 1));
        Assert.Same(common.Version, right.FirstConnected(0, 1));
        Assert.Same(left.Version, leftLater.FirstConnected(1, 2));
        Assert.Same(right.Version, rightLater.FirstConnected(1, 3));
        Assert.Same(leftLater.Version, leftLater.FirstConnected(0, 4));
        Assert.Same(rightLater.Version, rightLater.FirstConnected(0, 4));
        Assert.False(leftLater.Connected(0, 3));
        Assert.False(rightLater.Connected(0, 2));
        Assert.Same(root.Version, leftLater.Version.Root);
        Assert.Same(root.Version, rightLater.Version.Root);
        leftLater.ValidateInvariants();
        rightLater.ValidateInvariants();
    }

    /// <summary>Verifies the pair-specific LCA witness is not overwritten by later component mergers.</summary>
    [Fact]
    public void FirstConnected_UsesPairLcaRatherThanLatestComponentBirth()
    {
        var root = PersistentAncestralConnectionForest.Create(8);
        var ab = root.Link(0, 1);
        var cd = ab.Link(2, 3);
        var abcd = cd.Link(1, 3);
        var ef = abcd.Link(4, 5);
        var gh = ef.Link(6, 7);
        var efgh = gh.Link(4, 6);
        var all = efgh.Link(0, 4);

        Assert.Same(ab.Version, all.FirstConnected(0, 1));
        Assert.Same(cd.Version, all.FirstConnected(2, 3));
        Assert.Same(abcd.Version, all.FirstConnected(0, 2));
        Assert.Same(ef.Version, all.FirstConnected(4, 5));
        Assert.Same(gh.Version, all.FirstConnected(6, 7));
        Assert.Same(efgh.Version, all.FirstConnected(5, 7));
        Assert.Same(all.Version, all.FirstConnected(1, 6));
        all.ValidateInvariants();
    }

    /// <summary>Exercises the LCA boundary where one queried vertex is the pair's forest LCA.</summary>
    [Fact]
    public void FirstConnected_HandlesOneEndpointAtLcaAfterThatLcaBecomesAChild()
    {
        var root = PersistentAncestralConnectionForest.Create(5);
        var pair = root.Link(0, 1);       // 1 -> 0
        var otherPair = pair.Link(2, 3);  // 3 -> 2
        var larger = otherPair.Link(2, 4);
        var merged = larger.Link(2, 0);   // component rooted at 0 becomes a child of root 2

        Assert.Equal(2, merged.Find(0));
        Assert.Same(pair.Version, merged.FirstConnected(0, 1));
        Assert.Same(merged.Version, merged.FirstConnected(0, 2));
        Assert.Same(merged.Version, merged.FirstConnected(1, 4));
        merged.ValidateInvariants();
    }

    /// <summary>Builds a maximum-height balanced union history and validates the logarithmic path invariant.</summary>
    [Fact]
    public void BalancedUnions_RespectUnionBySizeHeightAndRetainWitnesses()
    {
        const int count = 1024;
        var root = PersistentAncestralConnectionForest.Create(count);
        var forest = root;
        AncestralConnectionVersion? firstPairVersion = null;
        AncestralConnectionVersion? finalBridgeVersion = null;

        for (var width = 1; width < count; width *= 2)
        {
            for (var start = 0; start < count; start += width * 2)
            {
                forest = forest.Link(start, start + width);
                if (start == 0 && width == 1)
                    firstPairVersion = forest.Version;
                if (start == 0 && width == count / 2)
                    finalBridgeVersion = forest.Version;
            }
        }

        Assert.Equal(1, forest.ComponentCount);
        Assert.Equal(0, forest.Find(count - 1));
        Assert.Same(firstPairVersion, forest.FirstConnected(0, 1));
        Assert.Same(finalBridgeVersion, forest.FirstConnected(0, count - 1));
        Assert.Equal(count, root.ComponentCount);
        forest.ValidateInvariants();
    }

    /// <summary>Checks fully branching random histories against a naive root-to-version scan.</summary>
    [Fact]
    public void RandomBranchingHistories_MatchNaiveAncestorScan()
    {
        const int vertexCount = 12;
        var random = new Random(0x5EED_2026);
        var rootForest = PersistentAncestralConnectionForest.Create(vertexCount);
        var states = new List<ModelVersion>
        {
            new(rootForest, Enumerable.Range(0, vertexCount).ToArray(), Parent: null),
        };

        for (var operation = 0; operation < 350; operation++)
        {
            var parent = states[random.Next(states.Count)];
            var left = random.Next(vertexCount);
            var right = random.Next(vertexCount);
            var classes = (int[])parent.Classes.Clone();
            var leftClass = classes[left];
            var rightClass = classes[right];
            if (leftClass != rightClass)
            {
                for (var vertex = 0; vertex < classes.Length; vertex++)
                {
                    if (classes[vertex] == rightClass)
                        classes[vertex] = leftClass;
                }
            }

            var actual = parent.Actual.Link(left, right);
            var state = new ModelVersion(actual, classes, parent);
            states.Add(state);

            Assert.Same(parent.Actual.Version, actual.Version.Parent);
            Assert.Equal(parent.Actual.Version.Depth + 1, actual.Version.Depth);
            Assert.Equal(classes.Distinct().Count(), actual.ComponentCount);
            for (var queryLeft = 0; queryLeft < vertexCount; queryLeft++)
            {
                for (var queryRight = 0; queryRight < vertexCount; queryRight++)
                {
                    var expectedConnected = classes[queryLeft] == classes[queryRight];
                    Assert.Equal(expectedConnected, actual.Connected(queryLeft, queryRight));
                    Assert.Same(
                        FirstConnectedByScan(state, queryLeft, queryRight),
                        actual.FirstConnected(queryLeft, queryRight));
                }
            }

            if (operation % 25 == 0)
                actual.ValidateInvariants();
        }

        // Recheck retained versions after all sibling branches have been constructed.
        for (var sample = 0; sample < 80; sample++)
        {
            var state = states[random.Next(states.Count)];
            var left = random.Next(vertexCount);
            var right = random.Next(vertexCount);
            Assert.Same(
                FirstConnectedByScan(state, left, right),
                state.Actual.FirstConnected(left, right));
        }
    }

    /// <summary>Verifies query cost and witnesses are independent of a long tail of redundant history.</summary>
    [Fact]
    public void LongRedundantHistory_DoesNotReplaceTheSuccessfulUnionWitness()
    {
        var root = PersistentAncestralConnectionForest.Create(3);
        var first = root.Link(0, 1);
        var forest = first;
        for (var depth = 0; depth < 20_000; depth++)
            forest = forest.Link(2, 2);

        Assert.Equal(20_001, forest.Version.Depth);
        Assert.Same(first.Version, forest.FirstConnected(0, 1));
        Assert.Null(forest.FirstConnected(0, 2));
        Assert.True(first.SharesConnectivityRootWith(forest));
    }

    /// <summary>Verifies sparse construction and validation do not scan an untouched huge universe.</summary>
    [Fact]
    public void HugeSparseUniverse_StoresOnlyVerticesTouchedByAUnion()
    {
        var root = PersistentAncestralConnectionForest.Create(int.MaxValue);
        root.ValidateInvariants();

        var linked = root.Link(0, int.MaxValue - 1);
        Assert.Equal(int.MaxValue - 1, linked.ComponentCount);
        Assert.True(linked.Connected(0, int.MaxValue - 1));
        Assert.False(linked.Connected(0, int.MaxValue - 2));
        Assert.Equal(int.MaxValue - 2, linked.Find(int.MaxValue - 2));
        Assert.Same(linked.Version, linked.FirstConnected(0, int.MaxValue - 1));
        linked.ValidateInvariants();
    }

    /// <summary>Verifies every vertex-taking operation rejects endpoints outside the fixed universe.</summary>
    [Fact]
    public void Operations_RejectVerticesOutsideTheUniverse()
    {
        var forest = PersistentAncestralConnectionForest.Create(3);

        Assert.Throws<ArgumentOutOfRangeException>(() => forest.Find(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.Find(3));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.Connected(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.Connected(0, 3));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.GetComponentSize(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.GetComponentSize(3));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.Link(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.Link(0, 3));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.FirstConnected(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.FirstConnected(0, 3));
        Assert.Throws<ArgumentOutOfRangeException>(() => forest.TryGetFirstConnected(-1, 0, out _));
        Assert.Same(forest.Version, forest.Version.Root);
        Assert.Equal(3, forest.ComponentCount);
        forest.ValidateInvariants();
    }

    /// <summary>Verifies independent roots remain distinct even for equal-size universes.</summary>
    [Fact]
    public void SeparateForests_HaveSeparateVersionTrees()
    {
        var leftRoot = PersistentAncestralConnectionForest.Create(2);
        var rightRoot = PersistentAncestralConnectionForest.Create(2);
        var left = leftRoot.Link(0, 1);
        var right = rightRoot.Link(0, 1);

        Assert.NotSame(leftRoot.Version, rightRoot.Version);
        Assert.NotSame(left.Version, right.Version);
        Assert.Same(left.Version, left.FirstConnected(0, 1));
        Assert.Same(right.Version, right.FirstConnected(0, 1));
        Assert.NotSame(left.FirstConnected(0, 1), right.FirstConnected(0, 1));
    }

    private static AncestralConnectionVersion? FirstConnectedByScan(
        ModelVersion state,
        int left,
        int right)
    {
        var lineage = new List<ModelVersion>();
        for (ModelVersion? current = state; current is not null; current = current.Parent)
            lineage.Add(current);

        for (var index = lineage.Count - 1; index >= 0; index--)
        {
            var candidate = lineage[index];
            if (candidate.Classes[left] == candidate.Classes[right])
                return candidate.Actual.Version;
        }

        return null;
    }

    private sealed record ModelVersion(
        PersistentAncestralConnectionForest Actual,
        int[] Classes,
        ModelVersion? Parent);
}
