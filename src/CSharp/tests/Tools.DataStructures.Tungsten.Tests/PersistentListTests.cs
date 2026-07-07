using Xunit;

namespace Tools.DataStructures.Tungsten.Tests;

/// <summary>
/// Example-based tests for <see cref="PersistentList{T}"/> covering the Tungsten-List operation
/// surface, persistence, no-op identity, and argument validation.
/// </summary>
public sealed class PersistentListTests
{
    /// <summary>Verifies the canonical empty list is shared and empty.</summary>
    [Fact]
    public void Empty_IsSharedAndEmpty()
    {
        Assert.Same(PersistentList<int>.Empty, PersistentList<int>.Empty);
        Assert.True(PersistentList<int>.Empty.IsEmpty);
        Assert.Empty(PersistentList<int>.Empty);
        Assert.Same(PersistentList<int>.Empty, PersistentList.Create<int>());
        Assert.Same(PersistentList<int>.Empty, PersistentList.CreateRange(Enumerable.Empty<int>()));
    }

    /// <summary>Verifies span construction preserves order and end accessors.</summary>
    [Fact]
    public void Create_PreservesOrder()
    {
        var list = PersistentList.Create(1, 2, 3);
        Assert.Equal([1, 2, 3], list.ToArray());
        Assert.Equal(3, list.Count);
        Assert.Equal(1, list.First);
        Assert.Equal(3, list.Last);
        Assert.Equal(2, list[1]);
    }

    /// <summary>Verifies CreateRange returns an existing persistent list unchanged.</summary>
    [Fact]
    public void CreateRange_ReusesExistingPersistentList()
    {
        var list = PersistentList.Create(1, 2, 3);
        Assert.Same(list, PersistentList.CreateRange(list));
        Assert.Same(list, PersistentList<int>.CreateRange(list));
    }

    /// <summary>Verifies end insertions produce new versions and never disturb the source.</summary>
    [Fact]
    public void AppendPrepend_LeaveSourceUnchanged()
    {
        var source = PersistentList.Create(2, 3);
        var appended = source.Append(4);
        var prepended = source.Prepend(1);

        Assert.Equal([2, 3], source.ToArray());
        Assert.Equal([2, 3, 4], appended.ToArray());
        Assert.Equal([1, 2, 3], prepended.ToArray());
    }

    /// <summary>Verifies Join concatenates and short-circuits empty operands to the other instance.</summary>
    [Fact]
    public void Join_ConcatenatesAndReturnsOperandForEmpty()
    {
        var left = PersistentList.Create(1, 2);
        var right = PersistentList.Create(3, 4);

        Assert.Equal([1, 2, 3, 4], left.Join(right).ToArray());
        Assert.Same(left, left.Join(PersistentList<int>.Empty));
        Assert.Same(right, PersistentList<int>.Empty.Join(right));
    }

    /// <summary>Verifies AddRange handles plain sequences, persistent lists, and empty input.</summary>
    [Fact]
    public void AddRange_AppendsSequencesAndLists()
    {
        var list = PersistentList.Create(1);
        Assert.Equal([1, 2, 3], list.AddRange([2, 3]).ToArray());
        Assert.Equal([1, 2, 3], list.AddRange(PersistentList.Create(2, 3)).ToArray());
        Assert.Same(list, list.AddRange([]));
    }

    /// <summary>Verifies single and range insertion place elements before the index.</summary>
    [Fact]
    public void InsertAndInsertRange_PlaceBeforeIndex()
    {
        var list = PersistentList.Create(1, 4);
        Assert.Equal([0, 1, 4], list.Insert(0, 0).ToArray());
        Assert.Equal([1, 2, 4], list.Insert(1, 2).ToArray());
        Assert.Equal([1, 4, 5], list.Insert(2, 5).ToArray());
        Assert.Equal([1, 2, 3, 4], list.InsertRange(1, [2, 3]).ToArray());
        Assert.Same(list, list.InsertRange(1, []));
    }

    /// <summary>Verifies positional, range, and end removal.</summary>
    [Fact]
    public void RemoveOperations_MatchPositions()
    {
        var list = PersistentList.Create(1, 2, 3, 4);
        Assert.Equal([2, 3, 4], list.RemoveAt(0).ToArray());
        Assert.Equal([1, 2, 4], list.RemoveAt(2).ToArray());
        Assert.Equal([1, 4], list.RemoveRange(1, 2).ToArray());
        Assert.Equal([2, 3, 4], list.RemoveFirst().ToArray());
        Assert.Equal([1, 2, 3], list.RemoveLast().ToArray());
        Assert.Same(list, list.RemoveRange(1, 0));
    }

    /// <summary>Verifies one-element replacement by value and by updater function.</summary>
    [Fact]
    public void SetItemAndUpdateAt_ReplaceOneElement()
    {
        var list = PersistentList.Create(1, 2, 3);
        Assert.Equal([1, 9, 3], list.SetItem(1, 9).ToArray());
        Assert.Equal([1, 20, 3], list.UpdateAt(1, value => value * 10).ToArray());
        Assert.Equal([1, 2, 3], list.ToArray());
    }

    /// <summary>Verifies Take/Drop/GetRange slicing and full-slice no-op identity.</summary>
    [Fact]
    public void TakeDropAndRanges_ShareStructure()
    {
        var list = PersistentList.CreateRange(Enumerable.Range(0, 10));
        Assert.Equal([0, 1, 2], list.Take(3).ToArray());
        Assert.Equal([7, 8, 9], list.TakeLast(3).ToArray());
        Assert.Equal(Enumerable.Range(3, 7), list.Drop(3).ToArray());
        Assert.Equal(Enumerable.Range(0, 7), list.DropLast(3).ToArray());
        Assert.Equal([4, 5], list.GetRange(4, 2).ToArray());
        Assert.Same(list, list.Take(10));
        Assert.Same(list, list.Drop(0));
    }

    /// <summary>Verifies SplitAt partitions the list and collapses empty halves to the shared empty.</summary>
    [Fact]
    public void SplitAt_PartitionsList()
    {
        var (left, right) = PersistentList.Create(1, 2, 3, 4).SplitAt(1);
        Assert.Equal([1], left.ToArray());
        Assert.Equal([2, 3, 4], right.ToArray());

        var (allLeft, emptyRight) = PersistentList.Create(1).SplitAt(1);
        Assert.Equal([1], allLeft.ToArray());
        Assert.Same(PersistentList<int>.Empty, emptyRight);
    }

    /// <summary>Verifies Reverse reverses order and returns trivially reversed lists unchanged.</summary>
    [Fact]
    public void Reverse_ReversesAndKeepsShortListsIdentical()
    {
        var list = PersistentList.Create(1, 2, 3);
        Assert.Equal([3, 2, 1], list.Reverse().ToArray());
        Assert.Equal([1, 2, 3], list.ToArray());

        var single = PersistentList.Create(7);
        Assert.Same(single, single.Reverse());
        Assert.Same(PersistentList<int>.Empty, PersistentList<int>.Empty.Reverse());
    }

    /// <summary>Verifies Map transforms every element in order and preserves the shared empty.</summary>
    [Fact]
    public void Map_TransformsEveryElementInOrder()
    {
        var list = PersistentList.Create(1, 2, 3);
        Assert.Equal(["1!", "2!", "3!"], list.Map(value => $"{value}!").ToArray());
        Assert.Same(PersistentList<string>.Empty, PersistentList<int>.Empty.Map(value => $"{value}"));
    }

    /// <summary>Verifies IndexOf/Contains honor the supplied equality comparer.</summary>
    [Fact]
    public void IndexOfAndContains_UseSuppliedComparer()
    {
        var list = PersistentList.Create("a", "B", "c");
        Assert.Equal(1, list.IndexOf("B"));
        Assert.Equal(-1, list.IndexOf("b"));
        Assert.Equal(1, list.IndexOf("b", StringComparer.OrdinalIgnoreCase));
        Assert.True(list.Contains("c"));
        Assert.False(list.Contains("d"));
    }

    /// <summary>Verifies CopyTo writes the elements at the requested offset.</summary>
    [Fact]
    public void CopyTo_WritesAtOffset()
    {
        var array = new int[5];
        PersistentList.Create(1, 2, 3).CopyTo(array, 1);
        Assert.Equal([0, 1, 2, 3, 0], array);
    }

    /// <summary>Verifies pattern-based and interface enumeration yield the snapshot in order.</summary>
    [Fact]
    public void Enumeration_YieldsSnapshotInOrder()
    {
        var list = PersistentList.Create(1, 2, 3);
        var seen = new List<int>();
        foreach (var item in list)
            seen.Add(item);
        Assert.Equal([1, 2, 3], seen);
        Assert.Equal([1, 2, 3], ((IEnumerable<int>)list).ToArray());
    }

    /// <summary>Verifies every retained version stays readable after later updates.</summary>
    [Fact]
    public void PersistentVersions_RemainReadableAfterManyUpdates()
    {
        var versions = new List<PersistentList<int>> { PersistentList<int>.Empty };
        for (var i = 0; i < 100; i++)
            versions.Add(versions[^1].Append(i));

        for (var i = 0; i < versions.Count; i++)
        {
            Assert.Equal(i, versions[i].Count);
            Assert.Equal(Enumerable.Range(0, i), versions[i].ToArray());
        }
    }

    /// <summary>Verifies retained immutable list snapshots are safe for concurrent readers.</summary>
    [Fact]
    public void ConcurrentReaders_ObserveConsistentRetainedSnapshot()
    {
        var list = PersistentList.CreateRange(Enumerable.Range(0, 512));

        Parallel.For(0, Environment.ProcessorCount * 4, _ =>
        {
            for (var pass = 0; pass < 64; pass++)
                AssertContiguousSnapshot(list, 512);
        });
    }

    /// <summary>Verifies lock-free publication of immutable list versions exposes only valid snapshots.</summary>
    [Fact]
    public async Task ConcurrentPublication_ReadersSeeValidSnapshots()
    {
        var published = PersistentList<int>.Empty;
        var done = 0;
        var readers = Enumerable.Range(0, Environment.ProcessorCount * 2)
            .Select(_ => Task.Run(() =>
            {
                while (Volatile.Read(ref done) == 0)
                    AssertContiguousSnapshot(Volatile.Read(ref published), 256);

                AssertContiguousSnapshot(Volatile.Read(ref published), 256);
            }))
            .ToArray();

        var writer = Task.Run(() =>
        {
            var list = PersistentList<int>.Empty;
            for (var value = 0; value < 256; value++)
            {
                list = list.Append(value);
                Volatile.Write(ref published, list);
            }

            Volatile.Write(ref done, 1);
        });

        await Task.WhenAll(readers.Append(writer));
    }

    /// <summary>Verifies range, null, and empty-state failures throw the documented exceptions.</summary>
    [Fact]
    public void OutOfRangeAndNullArguments_Throw()
    {
        var list = PersistentList.Create(1, 2, 3);
        Assert.Throws<ArgumentOutOfRangeException>(() => list[3]);
        Assert.Throws<ArgumentOutOfRangeException>(() => list[-1]);
        Assert.Throws<ArgumentOutOfRangeException>(() => list.Insert(4, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => list.RemoveAt(3));
        Assert.Throws<ArgumentOutOfRangeException>(() => list.Take(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => list.Drop(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => list.TakeLast(4));
        Assert.Throws<ArgumentOutOfRangeException>(() => list.DropLast(4));
        Assert.Throws<ArgumentNullException>(() => list.AddRange(null!));
        Assert.Throws<ArgumentNullException>(() => list.Join(null!));
        Assert.Throws<ArgumentNullException>(() => list.Map<int>(null!));
        Assert.Throws<ArgumentNullException>(() => PersistentList<int>.CreateRange(null!));
        Assert.Throws<InvalidOperationException>(() => PersistentList<int>.Empty.First);
        Assert.Throws<InvalidOperationException>(() => PersistentList<int>.Empty.RemoveLast());
    }

    private static void AssertContiguousSnapshot(PersistentList<int> list, int maxCount)
    {
        Assert.InRange(list.Count, 0, maxCount);
        for (var index = 0; index < list.Count; index++)
            Assert.Equal(index, list[index]);

        Assert.Equal(Enumerable.Range(0, list.Count), list.ToArray());
    }
}
