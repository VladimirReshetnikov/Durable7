using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Locks the semantic contracts of the Axis 2 positional cursor prototype independently of its
/// focus-window and carry-buffer tuning.
/// </summary>
public sealed class RopeCursorPrototypeContractTests
{
    private const int FocusCapacity = 32;
    private const int FlushChunkSize = 256;

    /// <summary>Empty and non-empty cursors expose a gap between the previous and next elements.</summary>
    [Fact]
    public void GapSemantics_PeeksMovesAndEndpointFailuresAreExact()
    {
        var empty = Rope<int>.Empty.GetClassCursorPrototype(0, FocusCapacity, FlushChunkSize);

        Assert.Equal(0, empty.Count);
        Assert.Equal(0, empty.Position);
        Assert.True(empty.IsAtStart);
        Assert.True(empty.IsAtEnd);
        Assert.False(empty.TryPeekPrevious(out var emptyPrevious));
        Assert.False(empty.TryPeekNext(out var emptyNext));
        Assert.Equal(0, emptyPrevious);
        Assert.Equal(0, emptyNext);
        Assert.Same(Rope<int>.Empty, empty.Snapshot());
        Assert.Throws<InvalidOperationException>(() => empty.MovePrevious());
        Assert.Throws<InvalidOperationException>(() => empty.MoveNext());
        Assert.Throws<InvalidOperationException>(() => empty.DeletePrevious());
        Assert.Throws<InvalidOperationException>(() => empty.DeleteNext());
        Assert.Throws<InvalidOperationException>(() => empty.ReplaceNext(0));

        var source = Rope<int>.Create(10, 20, 30, 40, 50);
        var cursor = source.GetClassCursorPrototype(2, FocusCapacity, FlushChunkSize);

        AssertGap(cursor, source.ToArray(), 2);
        var previous = cursor.MovePrevious();
        AssertGap(previous, source.ToArray(), 1);
        var next = cursor.MoveNext();
        AssertGap(next, source.ToArray(), 3);
        Assert.Same(cursor.VersionIdentity, previous.VersionIdentity);
        Assert.Same(cursor.VersionIdentity, next.VersionIdentity);

        var start = cursor.Seek(0);
        AssertGap(start, source.ToArray(), 0);
        Assert.Throws<InvalidOperationException>(() => start.MovePrevious());

        var end = cursor.Seek(source.Count);
        AssertGap(end, source.ToArray(), source.Count);
        Assert.Throws<InvalidOperationException>(() => end.MoveNext());

        Assert.Throws<ArgumentOutOfRangeException>(() => cursor.Seek(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => cursor.Seek(source.Count + 1));
    }

    /// <summary>Every edit applies at the gap, advances it exactly when inserting, and preserves element order.</summary>
    [Fact]
    public void Edits_ApplyAtGapAndSnapshotTheExactLogicalSequence()
    {
        var model = Enumerable.Range(0, 10).ToList();
        var cursor = Rope<int>.Create(model.ToArray())
            .GetClassCursorPrototype(5, FocusCapacity, FlushChunkSize);

        cursor = cursor.Insert(100);
        model.Insert(5, 100);
        AssertCursor(cursor, model, 6);

        var insertedRange = new[] { 200, 201, 202 };
        cursor = cursor.InsertRange(insertedRange);
        model.InsertRange(6, insertedRange);
        insertedRange[0] = int.MinValue;
        AssertCursor(cursor, model, 9);

        cursor = cursor.DeletePrevious();
        model.RemoveAt(8);
        AssertCursor(cursor, model, 8);

        cursor = cursor.DeleteNext();
        model.RemoveAt(8);
        AssertCursor(cursor, model, 8);

        cursor = cursor.ReplaceNext(-1);
        model[8] = -1;
        AssertCursor(cursor, model, 8);

        cursor = cursor.Seek(2);
        for (var i = 0; i < 3; i++)
        {
            cursor = cursor.DeleteNext();
            model.RemoveAt(2);
        }

        AssertCursor(cursor, model, 2);

        cursor = cursor.Seek(cursor.Count);
        for (var i = 0; i < 2; i++)
        {
            cursor = cursor.DeletePrevious();
            model.RemoveAt(model.Count - 1);
        }

        AssertCursor(cursor, model, model.Count);
    }

    /// <summary>No-op class operations retain wrapper identity; navigation retains only version identity.</summary>
    [Fact]
    public void ClassIdentity_NoOpsReuseWrapperNavigationSharesVersionAndEditsForkVersion()
    {
        var source = Rope<int>.Create(1, 2, 3, 4);
        var cursor = source.GetClassCursorPrototype(1, FocusCapacity, FlushChunkSize);

        Assert.Same(cursor, cursor.Seek(cursor.Position));
        Assert.Same(cursor, cursor.InsertRange(ReadOnlySpan<int>.Empty));
        Assert.Same(source, cursor.Snapshot());

        var moved = cursor.MoveNext();
        Assert.NotSame(cursor, moved);
        Assert.Same(cursor.VersionIdentity, moved.VersionIdentity);
        Assert.Same(source, moved.Snapshot());

        var edited = cursor.Insert(9);
        Assert.NotSame(cursor, edited);
        Assert.NotSame(cursor.VersionIdentity, edited.VersionIdentity);
        Assert.Equal([1, 9, 2, 3, 4], edited.Snapshot().ToArray());

        var equalReplacement = cursor.ReplaceNext(2);
        Assert.NotSame(cursor.VersionIdentity, equalReplacement.VersionIdentity);
        Assert.Equal(source.ToArray(), equalReplacement.Snapshot().ToArray());
    }

    /// <summary>Branches retain their own logical versions without changing their common ancestor.</summary>
    [Fact]
    public void RetainedBranches_AreIndependentAndKeepAncestorSnapshotIdentity()
    {
        var sourceValues = Enumerable.Range(0, 20).ToArray();
        var source = Rope<int>.Create(sourceValues);
        var ancestor = source.GetClassCursorPrototype(10, FocusCapacity, FlushChunkSize);

        var inserted = ancestor.Insert(-1);
        var deletedPrevious = ancestor.DeletePrevious();
        var deletedNext = ancestor.DeleteNext();
        var replaced = ancestor.ReplaceNext(-2);

        Assert.Same(source, ancestor.Snapshot());
        Assert.Equal(sourceValues, ancestor.Snapshot().ToArray());

        Assert.Equal(sourceValues.Take(10).Append(-1).Concat(sourceValues.Skip(10)), inserted.Snapshot().ToArray());
        Assert.Equal(sourceValues.Where((_, index) => index != 9), deletedPrevious.Snapshot().ToArray());
        Assert.Equal(sourceValues.Where((_, index) => index != 10), deletedNext.Snapshot().ToArray());
        Assert.Equal(sourceValues.Select((value, index) => index == 10 ? -2 : value), replaced.Snapshot().ToArray());

        Assert.NotSame(inserted.VersionIdentity, deletedPrevious.VersionIdentity);
        Assert.NotSame(inserted.VersionIdentity, deletedNext.VersionIdentity);
        Assert.NotSame(inserted.VersionIdentity, replaced.VersionIdentity);
        Assert.Equal(10, ancestor.Position);
        ancestor.Validate();
    }

    /// <summary>Concurrent first snapshots choose one memoized reference for every navigation context of a version.</summary>
    [Fact]
    public void Snapshot_FirstCallRacePublishesOneReferenceAcrossNavigationContexts()
    {
        var source = Rope<int>.Create(Enumerable.Range(0, 5000).ToArray());
        var edited = source.GetClassCursorPrototype(2048, FocusCapacity, FlushChunkSize).Insert(-1);
        var movedLeft = edited.MovePrevious();
        var movedRight = edited.MoveNext();
        var movedFartherRight = movedRight.MoveNext();
        var contexts = new[] { edited, movedLeft, movedRight, movedFartherRight };
        var snapshots = new Rope<int>[64];

        Assert.False(edited.GetDiagnostics().HasCachedSnapshot);

        Parallel.For(0, snapshots.Length, index =>
            snapshots[index] = contexts[index % contexts.Length].Snapshot());

        Assert.All(snapshots, snapshot => Assert.Same(snapshots[0], snapshot));
        Assert.All(contexts, context =>
        {
            Assert.Same(edited.VersionIdentity, context.VersionIdentity);
            Assert.True(context.GetDiagnostics().HasCachedSnapshot);
            Assert.Same(snapshots[0], context.Snapshot());
        });
        Assert.Equal(
            Enumerable.Range(0, 2048).Append(-1).Concat(Enumerable.Range(2048, 5000 - 2048)),
            snapshots[0].ToArray());
        snapshots[0].ValidateInvariants();
    }

    /// <summary>The struct comparison prototype is persistent, while its default value fails before use.</summary>
    [Fact]
    public void StructPrototype_CopiesBranchAndDefaultValueIsRejected()
    {
        var uninitialized = default(RopeCursorStructPrototype<int>);

        Assert.Equal(0, uninitialized.Position);
        Assert.True(uninitialized.IsAtStart);
        Assert.False(uninitialized.TryPeekPrevious(out var previous));
        Assert.Equal(0, previous);
        Assert.Equal(0, uninitialized.Seek(0).Position);
        Assert.Equal(0, uninitialized.InsertRange(ReadOnlySpan<int>.Empty).Position);
        Assert.Throws<InvalidOperationException>(() => _ = uninitialized.Count);
        Assert.Throws<InvalidOperationException>(() => _ = uninitialized.IsAtEnd);
        Assert.Throws<InvalidOperationException>(() => uninitialized.TryPeekNext(out _));
        Assert.Throws<InvalidOperationException>(() => uninitialized.Seek(1));
        Assert.Throws<InvalidOperationException>(() => uninitialized.MovePrevious());
        Assert.Throws<InvalidOperationException>(() => uninitialized.MoveNext());
        Assert.Throws<InvalidOperationException>(() => uninitialized.Insert(1));
        Assert.Throws<InvalidOperationException>(() => uninitialized.DeletePrevious());
        Assert.Throws<InvalidOperationException>(() => uninitialized.Snapshot());
        Assert.Throws<InvalidOperationException>(() => uninitialized.GetDiagnostics());

        var source = Rope<int>.Create(0, 1, 2, 3, 4);
        var original = source.GetStructCursorPrototype(2, FocusCapacity, FlushChunkSize);
        var copy = original;
        var edited = copy.Insert(9).DeletePrevious();

        Assert.Equal(2, original.Position);
        Assert.Same(source, original.Snapshot());
        Assert.Equal([0, 1, 2, 3, 4], original.Snapshot().ToArray());
        Assert.Equal([0, 1, 2, 3, 4], edited.Snapshot().ToArray());
        Assert.Equal(2, edited.Position);
    }

    /// <summary>The mutable control follows the same model as class and struct cursor versions on their shared API.</summary>
    [Fact]
    public void MutableControl_MatchesPersistentClassAndStructPrototypes()
    {
        var model = Enumerable.Range(0, 600).ToList();
        var source = Rope<int>.Create(model.ToArray());
        var classCursor = source.GetClassCursorPrototype(255, 16, FlushChunkSize);
        var structCursor = source.GetStructCursorPrototype(255, 16, FlushChunkSize);
        var mutableCursor = source.GetMutableCursorPrototype(255, 16, FlushChunkSize);
        var position = 255;
        var nextValue = -1;

        for (var step = 0; step < 480; step++)
        {
            switch (step % 8)
            {
                case 0:
                    classCursor = classCursor.Insert(nextValue);
                    structCursor = structCursor.Insert(nextValue);
                    mutableCursor.Insert(nextValue);
                    model.Insert(position, nextValue--);
                    position++;
                    break;

                case 1 when position > 0:
                    classCursor = classCursor.MovePrevious();
                    structCursor = structCursor.MovePrevious();
                    mutableCursor.MovePrevious();
                    position--;
                    break;

                case 2 when position < model.Count:
                    classCursor = classCursor.MoveNext();
                    structCursor = structCursor.MoveNext();
                    mutableCursor.MoveNext();
                    position++;
                    break;

                case 3 when position > 0:
                    classCursor = classCursor.DeletePrevious();
                    structCursor = structCursor.DeletePrevious();
                    mutableCursor.DeletePrevious();
                    model.RemoveAt(position - 1);
                    position--;
                    break;

                case 4:
                    position = (step * 37) % (model.Count + 1);
                    classCursor = classCursor.Seek(position);
                    structCursor = structCursor.Seek(position);
                    mutableCursor.Seek(position);
                    break;

                case 5:
                {
                    var values = new[] { nextValue--, nextValue--, nextValue-- };
                    classCursor = classCursor.InsertRange(values);
                    structCursor = structCursor.InsertRange(values);
                    mutableCursor.InsertRange(values);
                    model.InsertRange(position, values);
                    position += values.Length;
                    break;
                }

                case 6 when position < model.Count:
                    classCursor = classCursor.DeleteNext();
                    structCursor = structCursor.DeleteNext();
                    mutableCursor.DeleteNext();
                    model.RemoveAt(position);
                    break;

                case 7 when position < model.Count:
                    classCursor = classCursor.ReplaceNext(nextValue);
                    structCursor = structCursor.ReplaceNext(nextValue);
                    mutableCursor.ReplaceNext(nextValue);
                    model[position] = nextValue--;
                    break;
            }

            if ((step & 31) == 31)
            {
                Assert.Equal(model, classCursor.Snapshot().ToArray());
                Assert.Equal(model, structCursor.Snapshot().ToArray());
                Assert.Equal(model, mutableCursor.Snapshot().ToArray());
                Assert.Equal(position, classCursor.Position);
                Assert.Equal(position, structCursor.Position);
                Assert.Equal(position, mutableCursor.Position);
                Assert.Equal(position > 0, classCursor.TryPeekPrevious(out var classPrevious));
                Assert.Equal(position > 0, structCursor.TryPeekPrevious(out var structPrevious));
                Assert.Equal(position > 0, mutableCursor.TryPeekPrevious(out var mutablePrevious));
                Assert.Equal(classPrevious, structPrevious);
                Assert.Equal(classPrevious, mutablePrevious);
                Assert.Equal(position < model.Count, classCursor.TryPeekNext(out var classNext));
                Assert.Equal(position < model.Count, structCursor.TryPeekNext(out var structNext));
                Assert.Equal(position < model.Count, mutableCursor.TryPeekNext(out var mutableNext));
                Assert.Equal(classNext, structNext);
                Assert.Equal(classNext, mutableNext);
                AssertStateBounds(classCursor.GetDiagnostics());
                AssertStateBounds(structCursor.GetDiagnostics());
                AssertStateBounds(mutableCursor.GetDiagnostics());
            }
        }

        Assert.Equal(model, classCursor.Snapshot().ToArray());
        Assert.Equal(model, structCursor.Snapshot().ToArray());
        Assert.Equal(model, mutableCursor.Snapshot().ToArray());
    }

    /// <summary>Configuration validation rejects values outside the prototype's declared tuning domain.</summary>
    [Fact]
    public void Configuration_RejectsUnsupportedFocusAndFlushSizes()
    {
        var source = Rope<int>.Empty;

        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetClassCursorPrototype(0, 1, 256));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetClassCursorPrototype(0, 129, 256));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetClassCursorPrototype(0, 16, 255));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetClassCursorPrototype(0, 16, 2049));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetClassCursorPrototype(-1, 16, 256));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetClassCursorPrototype(1, 16, 256));
    }

    private static void AssertCursor(RopeCursorPrototype<int> cursor, IReadOnlyList<int> expected, int position)
    {
        AssertGap(cursor, expected, position);
        cursor.Validate();
        var snapshot = cursor.Snapshot();
        Assert.Same(snapshot, cursor.Snapshot());
        Assert.Equal(expected, snapshot.ToArray());
        snapshot.ValidateInvariants();
        AssertStateBounds(cursor.GetDiagnostics());
    }

    private static void AssertGap(RopeCursorPrototype<int> cursor, IReadOnlyList<int> expected, int position)
    {
        Assert.Equal(expected.Count, cursor.Count);
        Assert.Equal(position, cursor.Position);
        Assert.Equal(position == 0, cursor.IsAtStart);
        Assert.Equal(position == expected.Count, cursor.IsAtEnd);

        Assert.Equal(position > 0, cursor.TryPeekPrevious(out var previous));
        if (position > 0)
            Assert.Equal(expected[position - 1], previous);

        Assert.Equal(position < expected.Count, cursor.TryPeekNext(out var next));
        if (position < expected.Count)
            Assert.Equal(expected[position], next);
    }

    private static void AssertStateBounds(RopeCursorPrototypeStateDiagnostics diagnostics)
    {
        Assert.InRange(diagnostics.Position, 0, diagnostics.Count);
        Assert.InRange(diagnostics.ActiveLength, 0, diagnostics.FocusCapacity);
        Assert.InRange(diagnostics.LeftCarryLength, 0, diagnostics.FlushChunkSize - 1);
        Assert.InRange(diagnostics.RightCarryLength, 0, diagnostics.FlushChunkSize - 1);
        Assert.True(diagnostics.LeftOrdinaryChunkCount >= 0);
        Assert.True(diagnostics.RightOrdinaryChunkCount >= 0);
    }
}
