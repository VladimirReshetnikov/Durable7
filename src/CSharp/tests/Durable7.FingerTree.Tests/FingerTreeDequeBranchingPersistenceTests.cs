// Tests for the finger tree deque branching persistence.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies branching persistent histories: repeated endpoint and structural operations applied to the same
/// older version must all observe the unchanged snapshot and produce independent, valid results.
/// </summary>
/// <remarks>
/// An <c>AddLast</c>-built tree carries <c>One</c> prefix digits with <c>Node2</c> middle heads along its
/// whole left spine, so every <c>RemoveFirst</c> from the same base version takes the deepest underflow
/// cascade. These tests deliberately replay that worst-case path many times from one shared version, which
/// is exactly the usage pattern excluded from the strict representation's O(1) amortized claim; behavior
/// must stay correct regardless.
/// </remarks>
public sealed class FingerTreeDequeBranchingPersistenceTests
{
    /// <summary>
    /// Verifies repeated endpoint operations branched from one shared base version leave the base unchanged
    /// and produce correct independent results, including the deepest underflow cascade path.
    /// </summary>
    [Fact]
    public void RepeatedEndpointOperations_FromSharedBase_ProduceIndependentResults()
    {
        const int size = 500;
        var expected = Enumerable.Range(0, size).ToArray();

        var bySnoc = FingerTreeDeque<int>.Empty;
        foreach (var item in expected)
            bySnoc = bySnoc.AddLast(item);

        for (var repetition = 0; repetition < 100; repetition++)
        {
            var afterRemoveFirst = bySnoc.RemoveFirst();
            Assert.Equal(size - 1, afterRemoveFirst.Count);
            Assert.Equal(1, afterRemoveFirst.First);
            afterRemoveFirst.ValidateInvariants();

            var afterAddFirst = bySnoc.AddFirst(-repetition);
            Assert.Equal(size + 1, afterAddFirst.Count);
            Assert.Equal(-repetition, afterAddFirst.First);
            afterAddFirst.ValidateInvariants();

            var afterRemoveLast = bySnoc.RemoveLast();
            Assert.Equal(size - 1, afterRemoveLast.Count);
            Assert.Equal(size - 2, afterRemoveLast.Last);
            afterRemoveLast.ValidateInvariants();
        }

        bySnoc.ValidateInvariants();
        FingerTreeDequeAssert.SequenceEqual(expected, bySnoc);
    }

    /// <summary>
    /// Verifies a long chain of versions stays individually intact, including versions branched again from
    /// the middle of the chain.
    /// </summary>
    [Fact]
    public void VersionChains_WithMidChainBranches_AllVersionsStayIntact()
    {
        const int size = 200;
        var versions = new List<FingerTreeDeque<int>> { FingerTreeDeque<int>.Empty };
        for (var item = 0; item < size; item++)
            versions.Add(versions[^1].AddLast(item));

        for (var item = 0; item < size; item++)
        {
            versions[item + 1].ValidateInvariants();
            Assert.Equal(item + 1, versions[item + 1].Count);
            Assert.Equal(0, versions[item + 1].First);
            Assert.Equal(item, versions[item + 1].Last);
        }

        var middle = versions[size / 2];
        var branchA = middle.AddFirst(-1).AddLast(-2);
        var branchB = middle.RemoveFirst().RemoveLast();
        var branchC = middle.Concat(middle);

        branchA.ValidateInvariants();
        branchB.ValidateInvariants();
        branchC.ValidateInvariants();

        var middleExpected = Enumerable.Range(0, size / 2).ToArray();
        FingerTreeDequeAssert.SequenceEqual(middleExpected, middle);
        FingerTreeDequeAssert.SequenceEqual([-1, .. middleExpected, -2], branchA);
        FingerTreeDequeAssert.SequenceEqual(middleExpected[1..^1], branchB);
        FingerTreeDequeAssert.SequenceEqual([.. middleExpected, .. middleExpected], branchC);
    }

    /// <summary>
    /// Verifies that splits, range edits, and concatenations branched from one shared version never disturb
    /// each other or the shared structure.
    /// </summary>
    [Fact]
    public void StructuralOperations_FromSharedBase_DoNotDisturbEachOther()
    {
        const int size = 300;
        var expected = Enumerable.Range(0, size).ToArray();
        var baseVersion = FingerTreeDeque<int>.CreateRange(expected);

        var results = new List<FingerTreeDeque<int>>();
        for (var index = 0; index < size; index += 23)
        {
            results.Add(baseVersion.SetItem(index, -index));
            results.Add(baseVersion.RemoveAt(index));
            results.Add(baseVersion.InsertAt(index, -index));
            results.Add(baseVersion.SplitAt(index).Left.Concat(baseVersion.SplitAt(index).Right));
        }

        foreach (var result in results)
            result.ValidateInvariants();

        baseVersion.ValidateInvariants();
        FingerTreeDequeAssert.SequenceEqual(expected, baseVersion);
    }
}
