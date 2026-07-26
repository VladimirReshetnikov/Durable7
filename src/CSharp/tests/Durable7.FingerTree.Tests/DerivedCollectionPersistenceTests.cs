// Tests for the derived collection persistence.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies that the collections built on the general measured tree inherit its persistence: repeatedly
/// branching off a single RETAINED version leaves that version intact and yields correct, independent results.
/// This is the structural-sharing persistence the lazy-memoized core provides for free — the same retained
/// version is reused as the base of every branch, exactly the pattern the
/// <see cref="MeasuredFingerTreePersistenceTests"/> exercise at the core level.
/// </summary>
public sealed class DerivedCollectionPersistenceTests
{
    /// <summary>Verifies a retained SortedSet branches correctly and is itself never mutated.</summary>
    [Fact]
    public void SortedSet_RepeatedBranchingOffRetainedVersion_StaysCorrect()
    {
        var baseSet = SortedSet<int>.CreateRange(Enumerable.Range(0, 5000).Select(i => i * 2));

        for (var i = 0; i < 500; i++)
        {
            var key = -(i + 1);
            var branched = baseSet.Add(key);

            Assert.Equal(5001, branched.Count);
            Assert.True(branched.Contains(key));
            Assert.Equal(0, branched.IndexOf(key));   // a negative key sorts first

            // The retained base is unchanged by any branch off it.
            Assert.Equal(5000, baseSet.Count);
            Assert.False(baseSet.Contains(key));
            Assert.Equal(0, baseSet.Min);
        }
    }

    /// <summary>Verifies a retained SortedDictionary branches correctly and is itself never mutated.</summary>
    [Fact]
    public void SortedDictionary_RepeatedBranchingOffRetainedVersion_StaysCorrect()
    {
        var baseDict = SortedDictionary<int, int>.CreateRange(
            Enumerable.Range(0, 4000).Select(i => new KeyValuePair<int, int>(i * 2, i)));

        for (var i = 0; i < 400; i++)
        {
            var key = -(i + 1);
            var branched = baseDict.SetItem(key, i);

            Assert.Equal(4001, branched.Count);
            Assert.True(branched.TryGetValue(key, out var value) && value == i);

            Assert.Equal(4000, baseDict.Count);
            Assert.False(baseDict.ContainsKey(key));
        }
    }

    /// <summary>Verifies a retained PriorityQueue branches correctly and is itself never mutated.</summary>
    [Fact]
    public void PriorityQueue_RepeatedBranchingOffRetainedVersion_StaysCorrect()
    {
        var baseQueue = PriorityQueue<int, int>.Empty;
        for (var i = 0; i < 3000; i++)
            baseQueue = baseQueue.Enqueue(i, (i * 31) % 1000);
        Assert.True(baseQueue.TryPeekPriority(out var baseMin));

        for (var i = 0; i < 500; i++)
        {
            var branched = baseQueue.Enqueue(-i, -1);   // priority -1 is below every existing entry

            Assert.True(branched.TryPeek(out var element, out var priority));
            Assert.Equal(-1, priority);
            Assert.Equal(-i, element);

            // The retained base still reports its own minimum, unchanged.
            Assert.True(baseQueue.TryPeekPriority(out var stillMin));
            Assert.Equal(baseMin, stillMin);
        }
    }

    /// <summary>Verifies a retained IntervalTree branches correctly and is itself never mutated.</summary>
    [Fact]
    public void IntervalTree_RepeatedBranchingOffRetainedVersion_StaysCorrect()
    {
        var baseTree = IntervalTree<int>.Empty;
        for (var i = 0; i < 2000; i++)
            baseTree = baseTree.Insert(new Interval<int>(i * 10, i * 10 + 3));

        for (var i = 0; i < 300; i++)
        {
            var inserted = new Interval<int>(i * 10 + 5, i * 10 + 6);   // sits in the gaps of the base
            var branched = baseTree.Insert(inserted);

            Assert.True(branched.TryFindOverlap(inserted, out var match) && match.Equals(inserted));

            // The retained base has no interval overlapping that gap.
            Assert.False(baseTree.TryFindOverlap(inserted, out _));
        }
    }
}
