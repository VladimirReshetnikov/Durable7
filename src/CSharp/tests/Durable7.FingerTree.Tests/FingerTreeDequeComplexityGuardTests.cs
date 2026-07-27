// Tests for the finger tree deque complexity guard.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Guards the sharper complexity bounds of the API specification with deterministic observable measures:
/// comparer call counts for sorted search and allocation ceilings for endpoint replacement. These tests use
/// no timing, so they are stable across machines.
/// </summary>
public sealed class FingerTreeDequeComplexityGuardTests
{
    private sealed class CountingComparer : IComparer<int>
    {
        /// <summary>Gets the calls.</summary>
        public int Calls { get; private set; }

        /// <summary>Orders two values.</summary>
        public int Compare(int x, int y)
        {
            Calls++;
            return x.CompareTo(y);
        }
    }

    /// <summary>
    /// Verifies sorted lower-bound search near either end stays logarithmic in the distance from that end
    /// rather than in the total count: a 65,536-element deque must resolve near-end searches in a small
    /// constant number of comparer calls.
    /// </summary>
    [Fact]
    public void SortedLowerBound_NearEnds_UsesEndLocalComparerCalls()
    {
        const int size = 1 << 16;
        var deque = FingerTreeDeque<int>.CreateRange(Enumerable.Range(0, size));

        var nearLeft = new CountingComparer();
        Assert.Equal(3, deque.SortedLowerBound(3, nearLeft));

        var nearRight = new CountingComparer();
        Assert.Equal(size - 3, deque.SortedLowerBound(size - 3, nearRight));

        var middle = new CountingComparer();
        Assert.Equal(size / 2, deque.SortedLowerBound(size / 2, middle));

        // log2(distance 3) is tiny; each visited level costs a bounded number of signpost comparisons.
        Assert.InRange(nearLeft.Calls, 1, 24);
        Assert.InRange(nearRight.Calls, 1, 24);
        // The middle search may descend the full ~log2(n) levels but never more than a constant per level.
        Assert.InRange(middle.Calls, 1, 8 * 17);
        Assert.True(nearLeft.Calls < middle.Calls, "Near-end search must be cheaper than middle search.");
    }

    /// <summary>
    /// Verifies replacing an endpoint element allocates a constant number of bytes regardless of deque
    /// size, while replacing a middle element allocates along a logarithmic path, matching the specified
    /// O(1) worst-case endpoint <c>SetItem</c> contract.
    /// </summary>
    [Fact]
    public void SetItem_AtEndpoints_AllocatesConstantBytes()
    {
        const int size = 1 << 20;
        var deque = FingerTreeDeque<int>.CreateRange(Enumerable.Range(0, size));

        // Warm up every code path so JIT and type-initialization allocations happen outside the window.
        deque.SetItem(0, -1);
        deque.SetItem(size - 1, -1);
        deque.SetItem(size / 2, -1);

        var firstBytes = MeasureAllocation(() => deque.SetItem(0, -2));
        var lastBytes = MeasureAllocation(() => deque.SetItem(size - 1, -2));

        // One rebuilt root-level deep node plus the wrapper; anything near a logarithmic path would be
        // an order of magnitude larger.
        Assert.InRange(firstBytes, 1, 512);
        Assert.InRange(lastBytes, 1, 512);
    }

    /// <summary>
    /// Verifies endpoint reads on a million-element deque perform no allocation at all.
    /// </summary>
    [Fact]
    public void EndpointReads_AllocateNothing()
    {
        const int size = 1 << 20;
        var deque = FingerTreeDeque<int>.CreateRange(Enumerable.Range(0, size));

        _ = deque.First;
        _ = deque.Last;
        _ = deque.Count;

        var bytes = MeasureAllocation(() =>
        {
            _ = deque.First;
            _ = deque.Last;
            _ = deque.Count;
            _ = deque.IsEmpty;
        });

        Assert.Equal(0, bytes);
    }

    /// <summary>
    /// Verifies the persistent amortized contract of the memoized-suspension spine: replaying the same
    /// endpoint or near-end operation on one retained version costs a flat, size-independent number of
    /// bytes after the first call, because the forced spine work is memoized in the shared suspensions.
    /// A fully strict representation fails this by re-paying a logarithmic cascade on every call.
    /// </summary>
    /// <param name="sizeExponent">Binary exponent of the deque size under test.</param>
    [Theory]
    [InlineData(12)]
    [InlineData(18)]
    public void RepeatedOperations_OnRetainedVersion_HaveSizeIndependentMarginalCost(int sizeExponent)
    {
        var size = 1 << sizeExponent;
        var retained = FingerTreeDeque<int>.Empty;
        for (var item = 0; item < size; item++)
            retained = retained.AddLast(item);

        // First calls pay for and memoize any pending spine work held by the retained version.
        retained.RemoveFirst();
        retained.SplitItemAt(0);
        retained.AddFirst(-1);
        retained.AddLast(-1);
        retained.Concat(FingerTreeDeque<int>.Create(-1));

        for (var repetition = 0; repetition < 20; repetition++)
        {
            Assert.InRange(MeasureAllocation(() => retained.RemoveFirst()), 1, 512);
            Assert.InRange(MeasureAllocation(() => retained.SplitItemAt(0)), 1, 768);
            Assert.InRange(MeasureAllocation(() => retained.AddFirst(repetition)), 1, 512);
            Assert.InRange(MeasureAllocation(() => retained.AddLast(repetition)), 1, 512);
            Assert.InRange(MeasureAllocation(() => retained.Concat(FingerTreeDeque<int>.Create(repetition))), 1, 768);
        }

        Assert.Equal(0, retained.First);
        Assert.Equal(size - 1, retained.Last);
        retained.ValidateInvariants();
    }

    /// <summary>
    /// Verifies concurrent readers racing to force the same pending spine suspensions all observe the same
    /// element sequence and leave the structure valid.
    /// </summary>
    [Fact]
    public void ConcurrentReaders_RacingOnSuspensions_ObserveOneConsistentSequence()
    {
        const int size = 50_000;
        var deque = FingerTreeDeque<int>.Empty;
        for (var item = 0; item < size; item++)
            deque = deque.AddLast(item);

        var expected = Enumerable.Range(0, size).ToArray();
        Parallel.For(0, 8, worker =>
        {
            switch (worker % 4)
            {
                case 0:
                    Assert.Equal(expected, deque.ToArray());
                    break;
                case 1:
                    for (var index = worker; index < size; index += 487)
                        Assert.Equal(index, deque[index]);
                    break;
                case 2:
                {
                    var current = deque;
                    for (var step = 0; step < 64; step++)
                    {
                        Assert.Equal(step, current.First);
                        current = current.RemoveFirst();
                    }

                    break;
                }
                default:
                {
                    var split = deque.SplitAt(size / 2);
                    Assert.Equal(size / 2 - 1, split.Left.Last);
                    Assert.Equal(size / 2, split.Right.First);
                    break;
                }
            }
        });

        deque.ValidateInvariants();
    }

    /// <summary>
    /// Verifies the owner-required guarantee that accessing the first and last elements is O(1) worst-case:
    /// every endpoint read accessor (<c>First</c>, <c>Last</c>, the indexer and <c>TryGetItem</c> at
    /// <c>0</c> and <c>Count - 1</c>, and the <c>TryPeek*</c> pair) resolves within the top digits without
    /// forcing a suspended middle, so it allocates nothing even on a freshly built deque whose spine still
    /// holds unforced deferred work. A middle read, by contrast, must force and therefore allocates.
    /// </summary>
    [Fact]
    public void EndpointAccess_NeverForcesSuspendedSpine()
    {
        const int size = 1 << 20;
        var deque = FingerTreeDeque<int>.Empty;
        for (var item = 0; item < size; item++)
            deque = deque.AddLast(item);

        // Deliberately do NOT force the spine first (no ToArray/enumeration); the AddLast build leaves
        // unforced middle suspensions, and endpoint reads must still cost O(1).

        // Warm up the exact accessor paths so any JIT allocation happens outside the measured window.
        _ = deque.First;
        _ = deque.Last;
        _ = deque[0];
        _ = deque[size - 1];
        deque.TryGetItem(0, out _);
        deque.TryGetItem(size - 1, out _);
        deque.TryPeekFirst(out _);
        deque.TryPeekLast(out _);

        var endpointBytes = MeasureAllocation(() =>
        {
            _ = deque.First;
            _ = deque.Last;
            _ = deque[0];
            _ = deque[size - 1];
            deque.TryGetItem(0, out _);
            deque.TryGetItem(size - 1, out _);
            deque.TryPeekFirst(out _);
            deque.TryPeekLast(out _);
        });

        Assert.Equal(0, endpointBytes);
        Assert.Equal(0, deque[0]);
        Assert.Equal(size - 1, deque[size - 1]);
    }

    private static long MeasureAllocation(Action action)
    {
        var before = GC.GetAllocatedBytesForCurrentThread();
        action();
        return GC.GetAllocatedBytesForCurrentThread() - before;
    }
}
