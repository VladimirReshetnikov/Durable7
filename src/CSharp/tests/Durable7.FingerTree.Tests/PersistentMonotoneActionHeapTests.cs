using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Algebra, temporal-action, persistence, invariant, and complexity coverage for
/// <see cref="PersistentMonotoneActionHeap{TElement, TPriority, TAction}"/>.
/// </summary>
public sealed class PersistentMonotoneActionHeapTests
{
    /// <summary>Checks the clamp monoid, named composition direction, and collapsed constants.</summary>
    [Fact]
    public void ClampComposition_ObeysIdentityDirectionAssociativityAndMonotonicity()
    {
        var policy = new OrderClampPolicy<int>();
        var actions = new[]
        {
            policy.Identity,
            policy.AtLeast(-3),
            policy.AtLeast(8),
            policy.AtMost(-4),
            policy.AtMost(11),
            policy.Between(-6, 9),
            policy.Between(5, 5),
            policy.Constant(-9),
        };
        var samples = Enumerable.Range(-20, 41).Append(int.MinValue).Append(int.MaxValue).ToArray();

        Assert.True(policy.IsIdentity(policy.Identity));
        Assert.False(policy.IsIdentity(policy.AtLeast(0)));
        foreach (var action in actions)
        {
            AssertClampEquivalent(policy, action, policy.Compose(policy.Identity, action), samples);
            AssertClampEquivalent(policy, action, policy.Compose(action, policy.Identity), samples);
        }

        var floorThenCap = policy.Compose(policy.AtMost(5), policy.AtLeast(10));
        var capThenFloor = policy.Compose(policy.AtLeast(10), policy.AtMost(5));
        Assert.Equal(policy.Constant(5), floorThenCap);
        Assert.Equal(policy.Constant(10), capThenFloor);
        Assert.True(floorThenCap.IsConstant);
        Assert.True(capThenFloor.IsConstant);
        Assert.Equal(5, floorThenCap.ConstantValue);
        Assert.Equal(10, capThenFloor.ConstantValue);
        Assert.All(samples, value => Assert.Equal(5, policy.Apply(floorThenCap, value)));
        Assert.All(samples, value => Assert.Equal(10, policy.Apply(capThenFloor, value)));

        foreach (var inner in actions)
        {
            foreach (var outer in actions)
            {
                var composed = policy.Compose(outer, inner);
                foreach (var value in samples)
                {
                    Assert.Equal(
                        policy.Apply(outer, policy.Apply(inner, value)),
                        policy.Apply(composed, value));
                }
            }
        }

        foreach (var first in actions)
        {
            foreach (var second in actions)
            {
                foreach (var third in actions)
                {
                    AssertClampEquivalent(
                        policy,
                        policy.Compose(third, policy.Compose(second, first)),
                        policy.Compose(policy.Compose(third, second), first),
                        samples);
                }
            }
        }

        foreach (var action in actions)
        {
            for (var lower = -12; lower <= 12; lower++)
            {
                for (var upper = lower; upper <= 12; upper++)
                {
                    Assert.True(
                        policy.Apply(action, lower) <= policy.Apply(action, upper),
                        "A clamp action must be monotone for the retained order.");
                }
            }
        }
    }

    /// <summary>Preserves the exact newer boundary when comparer equality is coarser than value identity.</summary>
    [Fact]
    public void ClampComposition_CoarseComparerPreservesExactBoundaryRepresentatives()
    {
        var comparer = new RepresentativePriorityComparer();
        var policy = new OrderClampPolicy<RepresentativePriority>(comparer);
        var olderLower = new RepresentativePriority(10, "older lower");
        var newerUpper = new RepresentativePriority(5, "newer upper");
        var equalButDistinct = new RepresentativePriority(5, "comparer-equal input");

        var floorThenCap = policy.Compose(
            policy.AtMost(newerUpper),
            policy.AtLeast(olderLower));

        Assert.True(floorThenCap.IsConstant);
        Assert.Same(newerUpper, floorThenCap.ConstantValue);
        Assert.Same(newerUpper, policy.Apply(floorThenCap, equalButDistinct));
        Assert.Same(newerUpper, policy.Apply(
            floorThenCap,
            new RepresentativePriority(-100, "below")));
        Assert.Same(newerUpper, policy.Apply(
            floorThenCap,
            new RepresentativePriority(100, "above")));

        var olderUpper = new RepresentativePriority(5, "older upper");
        var newerLower = new RepresentativePriority(10, "newer lower");
        var capThenFloor = policy.Compose(
            policy.AtLeast(newerLower),
            policy.AtMost(olderUpper));

        Assert.True(capThenFloor.IsConstant);
        Assert.Same(newerLower, capThenFloor.ConstantValue);
        Assert.Same(
            newerLower,
            policy.Apply(capThenFloor, new RepresentativePriority(10, "comparer-equal input")));
        Assert.Same(newerLower, policy.Apply(
            capThenFloor,
            new RepresentativePriority(-100, "below")));
        Assert.Same(newerLower, policy.Apply(
            capThenFloor,
            new RepresentativePriority(100, "above")));

        // Equal order classes overlap rather than collapse to a constant. Sequential application
        // preserves a different exact representative on each side and an input representative in
        // the shared class.
        var olderCap = new RepresentativePriority(7, "older cap");
        var newerFloor = new RepresentativePriority(7, "newer floor");
        var inside = new RepresentativePriority(7, "inside representative");
        var touching = policy.Compose(policy.AtLeast(newerFloor), policy.AtMost(olderCap));
        Assert.False(touching.IsConstant);
        Assert.True(touching.HasLowerBound);
        Assert.True(touching.HasUpperBound);
        Assert.Same(newerFloor, touching.LowerBound);
        Assert.Same(olderCap, touching.UpperBound);
        Assert.Same(
            newerFloor,
            policy.Apply(touching, new RepresentativePriority(6, "below")));
        Assert.Same(inside, policy.Apply(touching, inside));
        Assert.Same(
            olderCap,
            policy.Apply(touching, new RepresentativePriority(8, "above")));
    }

    /// <summary>Proves that a noninvertible historical action does not affect later insertion.</summary>
    [Fact]
    public void Insert_AfterNoninvertibleTransformHasTemporalSemantics()
    {
        var comparer = Comparer<int>.Default;
        var policy = new OrderClampPolicy<int>(comparer);
        var source = StringHeap(policy, comparer)
            .Insert("low", -10)
            .Insert("middle", 5)
            .Insert("high", 20);
        var transformed = source.TransformAll(policy.AtLeast(10));
        var inserted = transformed.Insert("future", 0);

        AssertEntries(source,
            Entry("high", 20), Entry("low", -10), Entry("middle", 5));
        AssertEntries(transformed,
            Entry("high", 20), Entry("low", 10), Entry("middle", 10));
        AssertEntries(inserted,
            Entry("future", 0), Entry("high", 20), Entry("low", 10), Entry("middle", 10));
        Assert.Equal(Entry("future", 0), inserted.Minimum);

        var deleted = inserted.DeleteMinimum();
        AssertEntries(deleted,
            Entry("high", 20), Entry("low", 10), Entry("middle", 10));
        AssertEntries(transformed,
            Entry("high", 20), Entry("low", 10), Entry("middle", 10));
        Validate(source);
        Validate(transformed);
        Validate(inserted);
        Validate(deleted);

        // This is the opposite root-selection case: the transformed root wins. Exposing it before
        // skew-inserting the future child is what prevents the old cap from leaking onto that child.
        var bounded = StringHeap(policy, comparer)
            .Insert("old-low", -10)
            .Insert("old-high", 100)
            .TransformAll(policy.Between(0, 10));
        var laterHigh = bounded.Insert("future-high", 50);
        AssertEntries(bounded, Entry("old-low", 0), Entry("old-high", 10));
        AssertEntries(laterHigh,
            Entry("old-low", 0), Entry("old-high", 10), Entry("future-high", 50));
        var transformedAgain = laterHigh.TransformAll(policy.AtLeast(5));
        var newest = transformedAgain.Insert("newest-low", -20);
        AssertEntries(transformedAgain,
            Entry("old-low", 5), Entry("old-high", 10), Entry("future-high", 50));
        AssertEntries(newest,
            Entry("old-low", 5), Entry("old-high", 10), Entry("future-high", 50),
            Entry("newest-low", -20));
        Validate(bounded);
        Validate(laterHigh);
        Validate(transformedAgain);
        Validate(newest);
    }

    /// <summary>Ensures tags created independently remain local when their heaps are melded.</summary>
    [Fact]
    public void Meld_CombinesIndependentlyTransformedHeapsWithoutCrossApplyingActions()
    {
        var comparer = Comparer<int>.Default;
        var policy = new OrderClampPolicy<int>(comparer);
        var rawLeft = StringHeap(policy, comparer).Insert("L0", -20).Insert("L1", 3);
        var rawRight = StringHeap(policy, comparer).Insert("R0", -4).Insert("R1", 30);
        var left = rawLeft.TransformAll(policy.AtLeast(10));
        var right = rawRight.TransformAll(policy.AtMost(-10));
        var melded = left.Meld(right);

        AssertEntries(rawLeft, Entry("L0", -20), Entry("L1", 3));
        AssertEntries(rawRight, Entry("R0", -4), Entry("R1", 30));
        AssertEntries(left, Entry("L0", 10), Entry("L1", 10));
        AssertEntries(right, Entry("R0", -10), Entry("R1", -10));
        AssertEntries(melded,
            Entry("L0", 10), Entry("L1", 10), Entry("R0", -10), Entry("R1", -10));
        Assert.Equal(-10, melded.Minimum.Priority);
        Validate(left);
        Validate(right);
        Validate(melded);

        const int count = 257;
        var manyLeft = IntHeap(policy, comparer);
        var manyRight = IntHeap(policy, comparer);
        var expectedPriorities = new List<int>(2 * count);
        for (var index = 0; index < count; index++)
        {
            var leftPriority = ((index * 37) % 401) - 200;
            var rightPriority = ((index * 53) % 401) - 200;
            manyLeft = manyLeft.Insert(index, leftPriority);
            manyRight = manyRight.Insert(index + count, rightPriority);
            expectedPriorities.Add(policy.Apply(policy.Between(-30, 20), leftPriority));
            expectedPriorities.Add(policy.Apply(policy.Between(40, 70), rightPriority));
        }
        manyLeft = manyLeft.TransformAll(policy.Between(-30, 20));
        manyRight = manyRight.TransformAll(policy.Between(40, 70));
        var manyMelded = manyLeft.Meld(manyRight);
        var drainedPriorities = Drain(manyMelded).Select(entry => entry.Priority).ToArray();
        Assert.Equal(expectedPriorities.Order().ToArray(), drainedPriorities);
        Assert.Equal(drainedPriorities.Order().ToArray(), drainedPriorities);
        Validate(manyLeft);
        Validate(manyRight);
        Validate(manyMelded);
    }

    /// <summary>Retains a branching family through transforms, inserts, delete-min, and meld.</summary>
    [Fact]
    public void Updates_AreFullyPersistentAcrossDivergentBranches()
    {
        var comparer = Comparer<int>.Default;
        var policy = new OrderClampPolicy<int>(comparer);
        var original = StringHeap(policy, comparer)
            .Insert("a", 1)
            .Insert("b", 4)
            .Insert("c", 9);
        var originalRoot = original.RootIdentity;
        var floored = original.TransformAll(policy.AtLeast(5));
        var withFuture = floored.Insert("future", 0);
        var deleted = withFuture.DeleteMinimum();
        var capped = original.TransformAll(policy.AtMost(3));
        var melded = deleted.Meld(capped);

        Assert.Same(originalRoot, original.RootIdentity);
        Assert.NotSame(original.RootIdentity, floored.RootIdentity);
        Assert.Same(original, original.TransformAll(policy.Identity));
        AssertEntries(original, Entry("a", 1), Entry("b", 4), Entry("c", 9));
        AssertEntries(floored, Entry("a", 5), Entry("b", 5), Entry("c", 9));
        AssertEntries(withFuture,
            Entry("a", 5), Entry("b", 5), Entry("c", 9), Entry("future", 0));
        AssertEntries(deleted, Entry("a", 5), Entry("b", 5), Entry("c", 9));
        AssertEntries(capped, Entry("a", 1), Entry("b", 3), Entry("c", 3));
        AssertEntries(melded,
            Entry("a", 1), Entry("a", 5), Entry("b", 3), Entry("b", 5),
            Entry("c", 3), Entry("c", 9));

        foreach (var heap in new[] { original, floored, withFuture, deleted, capped, melded })
            Validate(heap);
    }

    /// <summary>Compares thousands of operations from retained historical branches with an immutable model.</summary>
    [Fact]
    public void RetainedBranches_RandomizedModelMatchesInsertDeleteMeldAndTransform()
    {
        var random = new Random(0x5A17_2026);
        var comparer = Comparer<int>.Default;
        var policy = new OrderClampPolicy<int>(comparer);
        var empty = IntHeap(policy, comparer);
        var versions = new List<ModelVersion> { new(empty, [], empty.RootIdentity) };
        var nextPayload = 0;
        var inserts = 0;
        var deletes = 0;
        var melds = 0;
        var transforms = 0;

        const int steps = 3_000;
        for (var step = 0; step < steps; step++)
        {
            var source = versions[random.Next(versions.Count)];
            var operation = random.Next(100);
            ModelVersion result;
            if (source.Model.Length == 0 || (source.Model.Length < 384 && operation < 30))
            {
                var entry = new ModelEntry(nextPayload++, random.Next(-100, 101));
                var heap = source.Heap.Insert(entry.Element, entry.Priority);
                result = new ModelVersion(heap, [.. source.Model, entry], heap.RootIdentity);
                inserts++;
            }
            else if (operation < 50)
            {
                Assert.True(source.Heap.TryDeleteMinimum(out var minimum, out var heap));
                Assert.Equal(source.Model.Min(entry => entry.Priority), minimum.Priority);
                var model = RemoveOne(source.Model, new ModelEntry(minimum.Element, minimum.Priority));
                result = new ModelVersion(heap, model, heap.RootIdentity);
                deletes++;
            }
            else if (operation < 75)
            {
                var action = RandomClamp(policy, random);
                var heap = source.Heap.TransformAll(action);
                var model = source.Model
                    .Select(entry => entry with { Priority = policy.Apply(action, entry.Priority) })
                    .ToArray();
                result = new ModelVersion(heap, model, heap.RootIdentity);
                transforms++;
            }
            else
            {
                ModelVersion? partner = null;
                for (var attempt = 0; attempt < 24; attempt++)
                {
                    var candidate = versions[random.Next(versions.Count)];
                    if (source.Model.Length + candidate.Model.Length <= 384)
                    {
                        partner = candidate;
                        break;
                    }
                }

                if (partner is null)
                {
                    var action = RandomClamp(policy, random);
                    var heap = source.Heap.TransformAll(action);
                    var model = source.Model
                        .Select(entry => entry with { Priority = policy.Apply(action, entry.Priority) })
                        .ToArray();
                    result = new ModelVersion(heap, model, heap.RootIdentity);
                    transforms++;
                }
                else
                {
                    var heap = source.Heap.Meld(partner.Heap);
                    result = new ModelVersion(
                        heap,
                        [.. source.Model, .. partner.Model],
                        heap.RootIdentity);
                    Assert.Same(partner.Root, partner.Heap.RootIdentity);
                    melds++;
                }
            }

            Assert.Same(source.Root, source.Heap.RootIdentity);
            versions.Add(result);
            if ((step & 63) == 0)
                AssertMatchesModel(result);
        }

        Assert.True(inserts > 300);
        Assert.True(deletes > 150);
        Assert.True(melds > 100);
        Assert.True(transforms > 300);
        foreach (var version in versions.Where((_, index) => index % 97 == 0).Append(versions[^1]))
        {
            Assert.Same(version.Root, version.Heap.RootIdentity);
            AssertMatchesModel(version);
        }
    }

    /// <summary>Locks exact constant policy work and fixed allocation for whole-heap transforms.</summary>
    [Fact]
    public void TransformAll_HasStrictConstantPolicyCallsAndAllocation()
    {
        var comparer = Comparer<int>.Default;
        var policy = new CountingClampPolicy(comparer);
        var action = policy.AtLeast(17);
        var heaps = new List<PersistentMonotoneActionHeap<int, int, OrderClamp<int>>>();
        foreach (var count in new[] { 1, 1_024, 65_536 })
        {
            var heap = IntHeap(policy, comparer);
            for (var index = 0; index < count; index++)
                heap = heap.Insert(index, index);
            heaps.Add(heap);
        }

        var bytesPerTransform = new List<long>();
        const int repetitions = 256;
        foreach (var source in heaps)
        {
            _ = source.TransformAll(action);
            policy.ResetCounts();
            var before = GC.GetAllocatedBytesForCurrentThread();
            PersistentMonotoneActionHeap<int, int, OrderClamp<int>>? result = null;
            for (var repetition = 0; repetition < repetitions; repetition++)
                result = source.TransformAll(action);
            var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
            GC.KeepAlive(result);

            Assert.Equal(0, policy.IdentityGets);
            Assert.Equal(2 * repetitions, policy.IdentityTests);
            Assert.Equal(repetitions, policy.Compositions);
            Assert.Equal(0, policy.Applications);
            Assert.Equal(0L, allocated % repetitions);
            var perTransform = allocated / repetitions;
            Assert.InRange(perTransform, 64L, 512L);
            bytesPerTransform.Add(perTransform);
        }

        Assert.All(bytesPerTransform, allocated => Assert.Equal(bytesPerTransform[0], allocated));

        var nonempty = heaps[1];
        policy.ResetCounts();
        Assert.Same(nonempty, nonempty.TransformAll(policy.Identity));
        Assert.Equal(1, policy.IdentityGets);
        Assert.Equal(1, policy.IdentityTests);
        Assert.Equal(0, policy.Compositions);
        Assert.Equal(0, policy.Applications);

        var empty = IntHeap(policy, comparer);
        policy.ResetCounts();
        Assert.Same(empty, empty.TransformAll(action));
        Assert.Equal(0, policy.PolicyCalls);
    }

    /// <summary>Exercises pending tags on tree roots and forest spines, then validates the fused shape.</summary>
    [Fact]
    public void ValidateStructure_AcceptsDeeplyTaggedAdversarialShapes()
    {
        var comparer = Comparer<int>.Default;
        var policy = new OrderClampPolicy<int>(comparer);
        var left = IntHeap(policy, comparer);
        var right = IntHeap(policy, comparer);
        for (var index = 0; index < 2_048; index++)
        {
            left = left.Insert(index, (index % 137) - 68);
            right = right.Insert(index + 2_048, 90 - (index % 181));
        }

        left = left.TransformAll(policy.AtLeast(-20)).TransformAll(policy.AtMost(30));
        right = right.TransformAll(policy.AtMost(25)).TransformAll(policy.AtLeast(-35));
        var heap = left.Meld(right).TransformAll(policy.Between(-12, 18));
        for (var index = 0; index < 96; index++)
        {
            heap = heap.DeleteMinimum();
            if ((index & 7) == 0)
                heap = heap.TransformAll(policy.AtLeast(-10 + (index / 8)));
        }
        heap = heap.Insert(-1, -100).TransformAll(policy.Between(-9, 11));

        var statistics = heap.ValidateStructure();
        Assert.Equal(heap.Count, statistics.Count);
        Assert.Equal(4_001, statistics.Count);
        Assert.True(statistics.RootForestLength > 0);
        Assert.True(statistics.MaximumRank > 0);
        Assert.True(statistics.MaximumDepth > 1);
        Assert.True(statistics.TaggedComponentCount > 0);
        Assert.Equal(heap.Count, heap.Count());
        Assert.All(heap, entry => Assert.InRange(entry.Priority, -9, 11));

        var leftStatistics = left.ValidateStructure();
        var rightStatistics = right.ValidateStructure();
        Assert.Equal(2_048, leftStatistics.Count);
        Assert.Equal(2_048, rightStatistics.Count);
    }

    /// <summary>Ensures priority collapse never deduplicates or substitutes payload representatives.</summary>
    [Fact]
    public void CollapsedPriorities_PreserveEveryPayload()
    {
        var comparer = Comparer<int>.Default;
        var policy = new OrderClampPolicy<int>(comparer);
        var source = StringHeap(policy, comparer);
        var expectedPayloads = Enumerable.Range(0, 2_048).Select(index => $"payload-{index:D4}").ToArray();
        foreach (var (payload, index) in expectedPayloads.Select((payload, index) => (payload, index)))
            source = source.Insert(payload, (index % 257) - 128);

        var collapsed = source.TransformAll(policy.Constant(7));
        var drained = Drain(collapsed);

        Assert.Equal(expectedPayloads.Length, drained.Length);
        Assert.All(drained, entry => Assert.Equal(7, entry.Priority));
        Assert.Equal(expectedPayloads, drained.Select(entry => entry.Element).Order().ToArray());
        Assert.Equal(expectedPayloads.Length, source.Count);
        Assert.Contains(source, entry => entry.Priority != 7);
        Validate(source);
        Validate(collapsed);
    }

    /// <summary>Checks compatibility failures and a throwing policy leave every source version unchanged.</summary>
    [Fact]
    public void MismatchAndPolicyFailures_AreAtomic()
    {
        var comparer = Comparer<int>.Create(static (left, right) => left.CompareTo(right));
        var otherComparer = Comparer<int>.Create(static (left, right) => left.CompareTo(right));
        var policy = new OrderClampPolicy<int>(comparer);
        var otherPolicy = new OrderClampPolicy<int>(comparer);
        var left = IntHeap(policy, comparer).Insert(1, 5).Insert(2, -1);
        var policyMismatch = IntHeap(otherPolicy, comparer).Insert(3, 0);
        var comparerPolicy = new ThrowingAddPolicy();
        var comparerLeft = PersistentMonotoneActionHeap<int, int, int>
            .Create(comparerPolicy, comparer)
            .Insert(4, -2);
        var comparerMismatch = PersistentMonotoneActionHeap<int, int, int>
            .Create(comparerPolicy, otherComparer)
            .Insert(5, 2);
        var leftRoot = left.RootIdentity;
        var leftEntries = OrderedEntries(left);
        var policyRoot = policyMismatch.RootIdentity;
        var comparerLeftRoot = comparerLeft.RootIdentity;
        var comparerRoot = comparerMismatch.RootIdentity;

        Assert.Throws<ArgumentException>(() => left.Meld(policyMismatch));
        Assert.Throws<ArgumentException>(() => IntHeap(policy, otherComparer));
        Assert.Throws<ArgumentException>(() => comparerLeft.Meld(comparerMismatch));
        Assert.Throws<ArgumentNullException>(() => left.Meld(null!));
        Assert.Throws<ArgumentException>(() => policy.Between(2, 1));
        Assert.Same(leftRoot, left.RootIdentity);
        Assert.Same(policyRoot, policyMismatch.RootIdentity);
        Assert.Same(comparerLeftRoot, comparerLeft.RootIdentity);
        Assert.Same(comparerRoot, comparerMismatch.RootIdentity);
        Assert.Equal(leftEntries, OrderedEntries(left));

        Assert.Throws<ArgumentNullException>(() =>
            PersistentMonotoneActionHeap<int, int, OrderClamp<int>>.Create(null!, comparer));
        Assert.Throws<ArgumentNullException>(() =>
            PersistentMonotoneActionHeap<int, int, OrderClamp<int>>.CreateRange(null!, policy, comparer));

        var throwingPolicy = new ThrowingAddPolicy();
        var faultSource = PersistentMonotoneActionHeap<int, int, int>
            .Create(throwingPolicy, comparer)
            .Insert(10, 10)
            .Insert(20, 20);
        var faultRoot = faultSource.RootIdentity;
        var faultEntries = OrderedEntries(faultSource);
        throwingPolicy.ThrowOnCompose = true;
        Assert.Throws<InvalidOperationException>(() => faultSource.TransformAll(1));
        throwingPolicy.ThrowOnCompose = false;

        Assert.Same(faultRoot, faultSource.RootIdentity);
        Assert.Equal(faultEntries, OrderedEntries(faultSource));
        Validate(faultSource);
        Validate(left);
        Validate(policyMismatch);
        Validate(comparerLeft);
        Validate(comparerMismatch);
    }

    /// <summary>Locks empty, singleton, identity, missing-bound, and integer-boundary behavior.</summary>
    [Fact]
    public void EmptySingletonAndClampBoundaries_HaveDefinedBehavior()
    {
        var comparer = Comparer<int>.Default;
        var policy = new OrderClampPolicy<int>(comparer);
        var empty = IntHeap(policy, comparer);

        Assert.True(empty.IsEmpty);
        Assert.Equal(0, empty.Count);
        Assert.False(empty.TryGetMinimum(out _));
        Assert.False(empty.TryDeleteMinimum(out _, out var unchanged));
        Assert.Same(empty, unchanged);
        Assert.Throws<InvalidOperationException>(() => empty.Minimum);
        Assert.Throws<InvalidOperationException>(empty.DeleteMinimum);
        Assert.Same(empty, empty.TransformAll(policy.AtLeast(3)));
        Assert.Same(empty, empty.TransformAll(policy.Identity));
        Assert.Same(empty, empty.Meld(empty));
        Assert.Equal(new PersistentMonotoneActionHeapStatistics(0, 0, 0, 0, 0), empty.ValidateStructure());
        Assert.Empty(empty);

        var singleton = empty.Insert(7, int.MaxValue);
        Assert.Equal(Entry(7, int.MaxValue), singleton.Minimum);
        Assert.True(singleton.TryDeleteMinimum(out var removed, out var afterRemoval));
        Assert.Equal(Entry(7, int.MaxValue), removed);
        Assert.True(afterRemoval.IsEmpty);
        Assert.Equal(1, singleton.Count);

        var identity = policy.Identity;
        Assert.False(identity.IsConstant);
        Assert.False(identity.HasLowerBound);
        Assert.False(identity.HasUpperBound);
        Assert.Throws<InvalidOperationException>(() => identity.ConstantValue);
        Assert.Throws<InvalidOperationException>(() => identity.LowerBound);
        Assert.Throws<InvalidOperationException>(() => identity.UpperBound);
        var floor = policy.AtLeast(int.MinValue);
        var cap = policy.AtMost(int.MaxValue);
        var constant = policy.Constant(42);
        Assert.Equal(int.MinValue, floor.LowerBound);
        Assert.False(floor.IsConstant);
        Assert.False(floor.HasUpperBound);
        Assert.Throws<InvalidOperationException>(() => floor.ConstantValue);
        Assert.Throws<InvalidOperationException>(() => floor.UpperBound);
        Assert.Equal(int.MaxValue, cap.UpperBound);
        Assert.False(cap.IsConstant);
        Assert.False(cap.HasLowerBound);
        Assert.Throws<InvalidOperationException>(() => cap.ConstantValue);
        Assert.Throws<InvalidOperationException>(() => cap.LowerBound);
        Assert.True(constant.IsConstant);
        Assert.Equal(42, constant.ConstantValue);
        Assert.False(constant.HasLowerBound);
        Assert.False(constant.HasUpperBound);
        Assert.Throws<InvalidOperationException>(() => constant.LowerBound);
        Assert.Throws<InvalidOperationException>(() => constant.UpperBound);

        var extremes = empty.Insert(1, int.MinValue).Insert(2, int.MaxValue);
        var bounded = extremes.TransformAll(policy.Between(-10, 10));
        AssertEntries(bounded, Entry(1, -10), Entry(2, 10));
        Assert.Same(extremes, extremes.TransformAll(default));
        Assert.Same(extremes, extremes.Meld(empty));
        Assert.Same(extremes, empty.Meld(extremes));
        Validate(singleton);
        Validate(afterRemoval);
        Validate(extremes);
        Validate(bounded);
    }

    private static PersistentMonotoneActionHeap<int, int, OrderClamp<int>> IntHeap(
        IMonotoneHeapAction<int, OrderClamp<int>> policy,
        IComparer<int> comparer) =>
        PersistentMonotoneActionHeap<int, int, OrderClamp<int>>.Create(policy, comparer);

    private static PersistentMonotoneActionHeap<string, int, OrderClamp<int>> StringHeap(
        IMonotoneHeapAction<int, OrderClamp<int>> policy,
        IComparer<int> comparer) =>
        PersistentMonotoneActionHeap<string, int, OrderClamp<int>>.Create(policy, comparer);

    private static MonotoneHeapEntry<TElement, int> Entry<TElement>(TElement element, int priority) =>
        new(element, priority);

    private static void AssertClampEquivalent(
        OrderClampPolicy<int> policy,
        OrderClamp<int> expected,
        OrderClamp<int> actual,
        IEnumerable<int> samples)
    {
        foreach (var sample in samples)
            Assert.Equal(policy.Apply(expected, sample), policy.Apply(actual, sample));
    }

    private static void AssertEntries<TElement, TAction>(
        PersistentMonotoneActionHeap<TElement, int, TAction> heap,
        params MonotoneHeapEntry<TElement, int>[] expected)
    {
        Assert.Equal(
            expected.OrderBy(entry => entry.Element, Comparer<TElement>.Default)
                .ThenBy(entry => entry.Priority)
                .ToArray(),
            OrderedEntries(heap));
    }

    private static MonotoneHeapEntry<TElement, int>[] OrderedEntries<TElement, TAction>(
        PersistentMonotoneActionHeap<TElement, int, TAction> heap) =>
        heap.OrderBy(entry => entry.Element, Comparer<TElement>.Default)
            .ThenBy(entry => entry.Priority)
            .ToArray();

    private static MonotoneHeapEntry<TElement, int>[] Drain<TElement, TAction>(
        PersistentMonotoneActionHeap<TElement, int, TAction> heap)
    {
        var result = new MonotoneHeapEntry<TElement, int>[heap.Count];
        for (var index = 0; index < result.Length; index++)
        {
            Assert.True(heap.TryDeleteMinimum(out result[index], out heap));
            if ((index & 255) == 0)
                Validate(heap);
        }
        Assert.True(heap.IsEmpty);
        return result;
    }

    private static void Validate<TElement, TAction>(
        PersistentMonotoneActionHeap<TElement, int, TAction> heap)
    {
        var statistics = heap.ValidateStructure();
        Assert.Equal(heap.Count, statistics.Count);
    }

    private static OrderClamp<int> RandomClamp(OrderClampPolicy<int> policy, Random random)
    {
        var first = random.Next(-100, 101);
        var second = random.Next(-100, 101);
        return random.Next(6) switch
        {
            0 => policy.Identity,
            1 => policy.AtLeast(first),
            2 => policy.AtMost(first),
            3 => policy.Between(Math.Min(first, second), Math.Max(first, second)),
            4 => policy.Between(first, first),
            _ => policy.Constant(first),
        };
    }

    private static ModelEntry[] RemoveOne(ModelEntry[] source, ModelEntry target)
    {
        var index = Array.FindIndex(source, entry => entry == target);
        Assert.True(index >= 0, "Delete-min returned an entry absent from the model.");
        var result = new ModelEntry[source.Length - 1];
        Array.Copy(source, 0, result, 0, index);
        Array.Copy(source, index + 1, result, index, source.Length - index - 1);
        return result;
    }

    private static void AssertMatchesModel(ModelVersion version)
    {
        var statistics = version.Heap.ValidateStructure();
        Assert.Equal(version.Model.Length, version.Heap.Count);
        Assert.Equal(version.Model.Length, statistics.Count);
        Assert.Equal(
            version.Model.OrderBy(entry => entry.Element).ThenBy(entry => entry.Priority).ToArray(),
            version.Heap.Select(entry => new ModelEntry(entry.Element, entry.Priority))
                .OrderBy(entry => entry.Element)
                .ThenBy(entry => entry.Priority)
                .ToArray());
        if (version.Model.Length == 0)
        {
            Assert.True(version.Heap.IsEmpty);
            Assert.False(version.Heap.TryGetMinimum(out _));
        }
        else
        {
            Assert.False(version.Heap.IsEmpty);
            Assert.Equal(version.Model.Min(entry => entry.Priority), version.Heap.Minimum.Priority);
        }
    }

    private readonly record struct ModelEntry(int Element, int Priority);

    private sealed record ModelVersion(
        PersistentMonotoneActionHeap<int, int, OrderClamp<int>> Heap,
        ModelEntry[] Model,
        object? Root);

    private sealed record RepresentativePriority(int Key, string Label);

    private sealed class RepresentativePriorityComparer : IComparer<RepresentativePriority>
    {
        public int Compare(RepresentativePriority? left, RepresentativePriority? right) =>
            Comparer<int>.Default.Compare(left!.Key, right!.Key);
    }

    private sealed class CountingClampPolicy(IComparer<int> comparer)
        : IMonotoneHeapAction<int, OrderClamp<int>>
    {
        private readonly OrderClampPolicy<int> _inner = new(comparer);

        internal int IdentityGets { get; private set; }
        internal int IdentityTests { get; private set; }
        internal int Compositions { get; private set; }
        internal int Applications { get; private set; }
        internal int PolicyCalls => IdentityGets + IdentityTests + Compositions + Applications;

        public OrderClamp<int> Identity
        {
            get
            {
                IdentityGets++;
                return _inner.Identity;
            }
        }

        public bool IsIdentity(OrderClamp<int> action)
        {
            IdentityTests++;
            return _inner.IsIdentity(action);
        }

        public OrderClamp<int> Compose(OrderClamp<int> outer, OrderClamp<int> inner)
        {
            Compositions++;
            return _inner.Compose(outer, inner);
        }

        public int Apply(OrderClamp<int> action, int priority)
        {
            Applications++;
            return _inner.Apply(action, priority);
        }

        internal OrderClamp<int> AtLeast(int lowerBound) => _inner.AtLeast(lowerBound);

        internal void ResetCounts()
        {
            IdentityGets = 0;
            IdentityTests = 0;
            Compositions = 0;
            Applications = 0;
        }
    }

    private sealed class ThrowingAddPolicy : IMonotoneHeapAction<int, int>
    {
        internal bool ThrowOnCompose { get; set; }

        public int Identity => 0;

        public bool IsIdentity(int action) => action == 0;

        public int Compose(int outer, int inner)
        {
            if (ThrowOnCompose)
                throw new InvalidOperationException("Injected composition failure.");
            return checked(outer + inner);
        }

        public int Apply(int action, int priority) => checked(action + priority);
    }
}
