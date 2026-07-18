using Xunit;

namespace Tools.DataStructures.Ordered.Tests;

/// <summary>Verifies explicit-order and grouped-pair cursor contracts.</summary>
public sealed class PersistentOrderedCursorTests
{
    /// <summary>Verifies positional insertion, duplicate identity, deletion, and retained branches.</summary>
    [Fact]
    public void SetCursor_InsertsAtGapAndRetainsBranches()
    {
        var source = PersistentOrderedSet<string>
            .CreateRange(["a", "b", "c"], StringComparer.OrdinalIgnoreCase);
        var cursor = source.GetCursor(1);

        Assert.True(cursor.TryPeekPrevious(out var previous));
        Assert.Equal("a", previous);
        Assert.True(cursor.TryPeekNext(out var next));
        Assert.Equal("b", next);

        cursor = cursor.Insert("x");
        Assert.Equal(2, cursor.Position);
        Assert.Equal(["a", "x", "b", "c"], cursor.Snapshot().ToArray());
        var unchanged = cursor.Insert("B");
        Assert.Equal(cursor.Position, unchanged.Position);
        Assert.Same(cursor.Snapshot(), unchanged.Snapshot());

        cursor = cursor.DeletePrevious();
        Assert.Equal(1, cursor.Position);
        Assert.Equal(["a", "b", "c"], cursor.Snapshot().ToArray());
        Assert.Equal(["a", "b", "c"], source.ToArray());
        Assert.True(source.TryGetCursor("B", out var focused));
        Assert.Equal(1, focused.Position);
    }

    /// <summary>Verifies positional map edits retain key representatives and source values.</summary>
    [Fact]
    public void MapCursor_PreservesRepresentativesAndUpdatesNextValue()
    {
        var source = PersistentOrderedMap<string, int>
            .Create(StringComparer.OrdinalIgnoreCase)
            .Add("a", 1)
            .Add("b", 2)
            .Add("c", 3);
        var cursor = source.GetCursor(1).Insert("x", 9);

        Assert.Equal(2, cursor.Position);
        Assert.Equal(["a", "x", "b", "c"], cursor.Snapshot().Keys);
        cursor = cursor.SetNextValue(20);
        Assert.True(cursor.TryPeekNext(out var next));
        Assert.Equal("b", next.Key);
        Assert.Equal(20, next.Value);
        Assert.Equal("b", cursor.Snapshot().EntryAt(2).Key);

        Assert.False(cursor.TryInsert("B", 200, out var duplicate));
        Assert.Equal(2, duplicate.Position);
        cursor = cursor.DeletePrevious();
        Assert.Equal(1, cursor.Position);
        Assert.Equal(["a", "b", "c"], cursor.Snapshot().Keys);
        Assert.Equal(2, source["b"]);
    }

    /// <summary>Verifies multimap pair ranks follow flattened key-grouped enumeration.</summary>
    [Fact]
    public void MultimapCursor_UsesFlattenedGroupedPairOrder()
    {
        var source = PersistentOrderedMultimap<string, int>.Empty
            .Add("b", 2)
            .Add("a", 9)
            .Add("b", 1)
            .Add("c", 7);
        Assert.True(source.TryGetCursor("b", 1, out var cursor));
        Assert.Equal(1, cursor.Position);

        cursor = cursor.Add("b", 3);
        Assert.Equal(3, cursor.Position);
        Assert.Equal(
            [
                KeyValuePair.Create("b", 2),
                KeyValuePair.Create("b", 1),
                KeyValuePair.Create("b", 3),
                KeyValuePair.Create("a", 9),
                KeyValuePair.Create("c", 7),
            ],
            cursor.Snapshot().ToArray());
        var unchanged = cursor.Add("b", 3);
        Assert.Equal(3, unchanged.Position);
        Assert.Same(cursor.Snapshot(), unchanged.Snapshot());

        cursor = cursor.DeletePrevious().DeleteNext();
        Assert.Equal(2, cursor.Position);
        Assert.Equal(
            [
                KeyValuePair.Create("b", 2),
                KeyValuePair.Create("b", 1),
                KeyValuePair.Create("c", 7),
            ],
            cursor.Snapshot().ToArray());
        Assert.True(cursor.TryPeekNext(out var next));
        Assert.Equal(KeyValuePair.Create("c", 7), next);
        Assert.Equal(4, source.PairCount);
        Assert.True(source.TryGetGroupCursor("a", out var group));
        Assert.Equal(2, group.Position);
    }
}
