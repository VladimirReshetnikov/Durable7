// Tests for the range update complexity guard.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Deterministic counter and sharing guards for the documented structural complexity bounds.</summary>
public sealed class RangeUpdateComplexityGuardTests
{
    /// <summary>Locks callback-free empty updates and one-callback value-distinct identity recognition.</summary>
    [Fact]
    public void NoOpUpdates_AllocateAndTraverseNoNodes()
    {
        var source = RangeUpdateAssert.Create(Enumerable.Range(0, 1024).ToArray());

        var empty = RangeUpdateDiagnosticsAdapter.Observe(() =>
            source.ApplyRange(400, 0, RangeUpdateTag.Assign(99)));
        Assert.Same(source, empty.Result);
        Assert.Equal(default(RangeUpdateOperationSnapshot), empty.Diagnostics);

        var identity = RangeUpdateDiagnosticsAdapter.Observe(() =>
            source.ApplyRange(100, 800, RangeUpdateTag.DistinctIdentity(73)));
        Assert.Same(source, identity.Result);
        Assert.Equal(0, identity.Diagnostics.NodeVisits);
        Assert.Equal(0, identity.Diagnostics.NodeAllocations);
        Assert.Equal(0, identity.Diagnostics.FacadeAllocations);
        Assert.Equal(1, identity.Diagnostics.IdentityTestCallbacks);
        Assert.Equal(1, identity.Diagnostics.PolicyCallbacks);

        var adopted = RangeUpdateDiagnosticsAdapter.Observe(() =>
            RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
                .CreateRange(source));
        Assert.Same(source, adopted.Result);
        Assert.Equal(default(RangeUpdateOperationSnapshot), adopted.Diagnostics);
    }

    /// <summary>Locks the O(1) whole-sequence update path, including exact node and callback counts.</summary>
    [Fact]
    public void WholeRangeUpdate_HasConstantExactDiagnostics()
    {
        var source = RangeUpdateAssert.Create(Enumerable.Range(0, 4095).ToArray());
        var first = RangeUpdateDiagnosticsAdapter.Observe(() =>
            source.ApplyRange(0, source.Count, RangeUpdateTag.Add(3)));

        Assert.Equal(1, first.Diagnostics.NodeVisits);
        Assert.Equal(1, first.Diagnostics.NodeAllocations);
        Assert.Equal(1, first.Diagnostics.FacadeAllocations);
        Assert.Equal(0, first.Diagnostics.Rotations);
        Assert.Equal(0, first.Diagnostics.Pushes);
        Assert.Equal(1, first.Diagnostics.SubtreeApplications);
        Assert.Equal(0, first.Diagnostics.ElementMeasureCallbacks);
        Assert.Equal(0, first.Diagnostics.MeasureCombineCallbacks);
        Assert.Equal(1, first.Diagnostics.IdentityTestCallbacks);
        Assert.Equal(0, first.Diagnostics.TagComposeCallbacks);
        Assert.Equal(1, first.Diagnostics.ElementApplyCallbacks);
        Assert.Equal(1, first.Diagnostics.MeasureApplyCallbacks);

        var second = RangeUpdateDiagnosticsAdapter.Observe(() =>
            first.Result.ApplyRange(0, first.Result.Count, RangeUpdateTag.Assign(8)));
        Assert.Equal(1, second.Diagnostics.NodeVisits);
        Assert.Equal(1, second.Diagnostics.NodeAllocations);
        Assert.Equal(1, second.Diagnostics.FacadeAllocations);
        Assert.Equal(1, second.Diagnostics.SubtreeApplications);
        Assert.Equal(2, second.Diagnostics.IdentityTestCallbacks);
        Assert.Equal(1, second.Diagnostics.TagComposeCallbacks);
        Assert.Equal(1, second.Diagnostics.ElementApplyCallbacks);
        Assert.Equal(1, second.Diagnostics.MeasureApplyCallbacks);

        RangeUpdateAssert.Matches(Enumerable.Range(0, 4095).Select(value => value + 3).ToArray(), first.Result);
        RangeUpdateAssert.Matches(Enumerable.Repeat(8, 4095).ToArray(), second.Result);
        RangeUpdateAssert.Matches(Enumerable.Range(0, 4095).ToArray(), source);
    }

    /// <summary>Locks allocation-free logarithmic indexing and proper-range measure queries.</summary>
    [Fact]
    public void Reads_StayWithinHeightScaledVisitBoundsAndAllocateNoPersistentNodes()
    {
        var source = RangeUpdateAssert.Create(Enumerable.Range(0, 4095).ToArray())
            .ApplyRange(0, 4095, RangeUpdateTag.Add(5));
        var height = RangeUpdateDiagnosticsAdapter.Validate(
            source,
            static (left, right) => left == right).Height;

        foreach (var index in new[] { 0, 1, 1023, 2047, 3071, 4093, 4094 })
        {
            var read = RangeUpdateDiagnosticsAdapter.Observe(() => source[index]);
            Assert.Equal(index + 5, read.Result);
            Assert.InRange(read.Diagnostics.NodeVisits, 1, height);
            AssertNoPersistentMutation(read.Diagnostics);
        }

        foreach (var range in new[]
        {
            (Index: 0, Count: 1),
            (Index: 1, Count: 2047),
            (Index: 1000, Count: 2000),
            (Index: 2047, Count: 1),
            (Index: 3071, Count: 1024),
        })
        {
            var query = RangeUpdateDiagnosticsAdapter.Observe(() =>
                source.MeasureRange(range.Index, range.Count));
            Assert.Equal(
                RangeUpdateAssert.Fold(
                    Enumerable.Range(range.Index, range.Count).Select(value => value + 5)),
                query.Result);
            Assert.InRange(query.Diagnostics.NodeVisits, 1, (8L * height) + 8);
            AssertNoPersistentMutation(query.Diagnostics);
        }
    }

    /// <summary>Guards proper range updates and indexed edits with generous linear-in-height ceilings.</summary>
    [Fact]
    public void Updates_StayWithinHeightScaledAllocationAndVisitCeilingsAndShareMostNodes()
    {
        var source = RangeUpdateAssert.Create(Enumerable.Range(0, 4095).ToArray())
            .ApplyRange(0, 4095, RangeUpdateTag.Add(2));
        var height = RangeUpdateDiagnosticsAdapter.Validate(
            source,
            static (left, right) => left == right).Height;
        var ceiling = (128L * height) + 64;

        var rangeUpdate = RangeUpdateDiagnosticsAdapter.Observe(() =>
            source.ApplyRange(777, 2048, RangeUpdateTag.Assign(-6)));
        Assert.InRange(rangeUpdate.Diagnostics.NodeVisits, 1, ceiling);
        Assert.InRange(rangeUpdate.Diagnostics.NodeAllocations, 1, ceiling);
        Assert.Equal(1, rangeUpdate.Diagnostics.FacadeAllocations);
        AssertHeightScaledMutationCeilings(rangeUpdate.Diagnostics, height);
        Assert.True(
            RangeUpdateDiagnosticsAdapter.CountSharedNodes(source, rangeUpdate.Result)
            >= source.Count - ceiling);

        var inserted = RangeUpdateDiagnosticsAdapter.Observe(() => source.Insert(2047, 99));
        Assert.InRange(inserted.Diagnostics.NodeVisits, 1, ceiling);
        Assert.InRange(inserted.Diagnostics.NodeAllocations, 1, ceiling);
        Assert.Equal(1, inserted.Diagnostics.FacadeAllocations);
        AssertHeightScaledMutationCeilings(inserted.Diagnostics, height);
        Assert.True(
            RangeUpdateDiagnosticsAdapter.CountSharedNodes(source, inserted.Result)
            >= source.Count - ceiling);

        var removed = RangeUpdateDiagnosticsAdapter.Observe(() => source.RemoveAt(2047));
        Assert.InRange(removed.Diagnostics.NodeVisits, 1, ceiling);
        Assert.InRange(removed.Diagnostics.NodeAllocations, 1, ceiling);
        Assert.Equal(1, removed.Diagnostics.FacadeAllocations);
        AssertHeightScaledMutationCeilings(removed.Diagnostics, height);
        Assert.True(
            RangeUpdateDiagnosticsAdapter.CountSharedNodes(source, removed.Result)
            >= source.Count - ceiling);

        RangeUpdateAssert.Matches(Enumerable.Range(0, 4095).Select(value => value + 2).ToArray(), source);
    }

    private static void AssertNoPersistentMutation(RangeUpdateOperationSnapshot diagnostics)
    {
        Assert.Equal(0, diagnostics.NodeAllocations);
        Assert.Equal(0, diagnostics.FacadeAllocations);
        Assert.Equal(0, diagnostics.Rotations);
        Assert.Equal(0, diagnostics.Pushes);
        Assert.Equal(0, diagnostics.SubtreeApplications);
    }

    private static void AssertHeightScaledMutationCeilings(
        RangeUpdateOperationSnapshot diagnostics,
        int height)
    {
        // A proper update follows a constant number of split/join spines; insertion and removal
        // follow fewer. Rebalancing performs at most a constant number of rotations at each spine
        // level. An immutable push can apply one tag to each child, and each subtree application can
        // compose one tag and invoke both action callbacks. These deliberately loose coefficients
        // cover double rotations and wrapper copies while remaining below one full traversal for
        // this test's 4,095-node, height-12 input.
        var rotationCeiling = checked((16L * height) + 16);
        var pushCeiling = checked((32L * height) + 32);
        var subtreeApplicationCeiling = checked((64L * height) + 64);
        var policyCallbackCeiling = checked((256L * height) + 256);

        Assert.InRange(diagnostics.Rotations, 0L, rotationCeiling);
        Assert.InRange(diagnostics.Pushes, 0L, pushCeiling);
        Assert.InRange(diagnostics.SubtreeApplications, 0L, subtreeApplicationCeiling);
        Assert.InRange(diagnostics.TagComposeCallbacks, 0L, subtreeApplicationCeiling);
        Assert.InRange(diagnostics.ElementApplyCallbacks, 0L, subtreeApplicationCeiling);
        Assert.InRange(diagnostics.MeasureApplyCallbacks, 0L, subtreeApplicationCeiling);
        Assert.InRange(diagnostics.PolicyCallbacks, 1L, policyCallbackCeiling);
    }
}
