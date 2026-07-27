// Tests for the rope cursor.

using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Locks the public Axis 2 C1 positional cursor contract.</summary>
public sealed class RopeCursorTests
{
    /// <summary>The default struct is deliberately invalid; an empty cursor comes from the empty rope.</summary>
    [Fact]
    public void DefaultValue_AllMembersRejectItExplicitly()
    {
        var cursor = default(RopeCursor<int>);

        Assert.Throws<InvalidOperationException>(() => _ = cursor.Count);
        Assert.Throws<InvalidOperationException>(() => _ = cursor.Position);
        Assert.Throws<InvalidOperationException>(() => _ = cursor.IsAtStart);
        Assert.Throws<InvalidOperationException>(() => _ = cursor.IsAtEnd);
        Assert.Throws<InvalidOperationException>(() => cursor.TryPeekPrevious(out _));
        Assert.Throws<InvalidOperationException>(() => cursor.TryPeekNext(out _));
        Assert.Throws<InvalidOperationException>(() => cursor.MovePrevious());
        Assert.Throws<InvalidOperationException>(() => cursor.MoveNext());
        Assert.Throws<InvalidOperationException>(() => cursor.Seek(0));
        Assert.Throws<InvalidOperationException>(() => cursor.Insert(1));
        Assert.Throws<InvalidOperationException>(() => cursor.InsertRange(ReadOnlySpan<int>.Empty));
        Assert.Throws<InvalidOperationException>(() => cursor.DeletePrevious());
        Assert.Throws<InvalidOperationException>(() => cursor.DeleteNext());
        Assert.Throws<InvalidOperationException>(() => cursor.ReplaceNext(1));
        Assert.Throws<InvalidOperationException>(() => cursor.Snapshot());
        Assert.Throws<InvalidOperationException>(() => cursor.GetDiagnostics());
        Assert.Throws<InvalidOperationException>(() => cursor.Validate());

        var empty = Rope<int>.Empty.GetCursor();
        Assert.Equal(0, empty.Count);
        Assert.Equal(0, empty.Position);
        Assert.True(empty.IsAtStart);
        Assert.True(empty.IsAtEnd);
        Assert.Same(Rope<int>.Empty, empty.Snapshot());
    }

    /// <summary>The cursor is a boundary: peeks, movement, and endpoint failures use exact gap semantics.</summary>
    [Fact]
    public void GapSemantics_PeeksMovesAndEndpointFailuresAreExact()
    {
        var source = Rope<int>.Create(10, 20, 30, 40, 50);
        var cursor = source.GetCursor(2);

        AssertGap(cursor, source.ToArray(), 2);
        AssertGap(cursor.MovePrevious(), source.ToArray(), 1);
        AssertGap(cursor.MoveNext(), source.ToArray(), 3);

        var start = cursor.Seek(0);
        AssertGap(start, source.ToArray(), 0);
        Assert.Throws<InvalidOperationException>(() => start.MovePrevious());
        Assert.Throws<InvalidOperationException>(() => start.DeletePrevious());

        var end = cursor.Seek(source.Count);
        AssertGap(end, source.ToArray(), source.Count);
        Assert.Throws<InvalidOperationException>(() => end.MoveNext());
        Assert.Throws<InvalidOperationException>(() => end.DeleteNext());
        Assert.Throws<InvalidOperationException>(() => end.ReplaceNext(0));

        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetCursor(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetCursor(source.Count + 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => cursor.Seek(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => cursor.Seek(source.Count + 1));
    }

    /// <summary>All edits apply at the gap, and insertion copies caller-owned range storage.</summary>
    [Fact]
    public void Edits_ApplyAtGapAndPreserveTheReceiver()
    {
        var originalValues = Enumerable.Range(0, 10).ToArray();
        var source = Rope<int>.Create(originalValues);
        var receiver = source.GetCursor(5);
        var model = originalValues.ToList();

        var cursor = receiver.Insert(100);
        model.Insert(5, 100);
        AssertCursor(cursor, model, 6);

        var insertedRange = new[] { 200, 201, 202 };
        cursor = cursor.InsertRange(insertedRange);
        model.InsertRange(6, insertedRange);
        insertedRange.AsSpan().Fill(int.MinValue);
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

        Assert.Equal(originalValues, receiver.Snapshot().ToArray());
        Assert.Same(source, receiver.Snapshot());
        Assert.Equal(5, receiver.Position);
    }

    /// <summary>No-op values preserve exact shared state; navigation shares the version and edits fork it.</summary>
    [Fact]
    public void Identity_NoOpsNavigationEditsAndSnapshotsAreDistinguished()
    {
        var source = Rope<int>.Create(1, 2, 3, 4);
        var cursor = source.GetCursor(1);
        var version = cursor.VersionIdentityForDiagnostics;

        var samePosition = cursor.Seek(cursor.Position);
        var emptyInsert = cursor.InsertRange(ReadOnlySpan<int>.Empty);
        Assert.Same(version, samePosition.VersionIdentityForDiagnostics);
        Assert.Same(version, emptyInsert.VersionIdentityForDiagnostics);
        Assert.True(cursor.HasSameContextStateForDiagnostics(samePosition));
        Assert.True(cursor.HasSameContextStateForDiagnostics(emptyInsert));
        Assert.Same(source, cursor.Snapshot());
        Assert.Same(source, samePosition.Snapshot());
        Assert.Same(source, emptyInsert.Snapshot());

        var moved = cursor.MoveNext();
        Assert.Same(version, moved.VersionIdentityForDiagnostics);
        Assert.False(cursor.HasSameContextStateForDiagnostics(moved));
        Assert.Same(source, moved.Snapshot());

        var inserted = cursor.Insert(9);
        Assert.NotSame(version, inserted.VersionIdentityForDiagnostics);
        Assert.Equal([1, 9, 2, 3, 4], inserted.Snapshot().ToArray());

        var equalReplacement = cursor.ReplaceNext(2);
        Assert.NotSame(version, equalReplacement.VersionIdentityForDiagnostics);
        Assert.Equal(source.ToArray(), equalReplacement.Snapshot().ToArray());
        Assert.NotSame(source, equalReplacement.Snapshot());
    }

    /// <summary>ReplaceNext never calls hidden element equality and always creates an edited version.</summary>
    [Fact]
    public void ReplaceNext_DoesNotConsultElementEquality()
    {
        var value = new ThrowingEquality(1);
        var source = Rope<ThrowingEquality>.Create(value);
        var cursor = source.GetCursor();

        var replaced = cursor.ReplaceNext(value);

        Assert.NotSame(cursor.VersionIdentityForDiagnostics, replaced.VersionIdentityForDiagnostics);
        Assert.Same(value, replaced.Snapshot()[0]);
        Assert.NotSame(source, replaced.Snapshot());
        Assert.Equal(0, replaced.Position);
    }

    /// <summary>Branches from a retained version are independent and leave their ancestor untouched.</summary>
    [Fact]
    public void RetainedBranches_AreIndependent()
    {
        var sourceValues = Enumerable.Range(0, 20).ToArray();
        var source = Rope<int>.Create(sourceValues);
        var ancestor = source.GetCursor(10);

        var inserted = ancestor.Insert(-1);
        var deletedPrevious = ancestor.DeletePrevious();
        var deletedNext = ancestor.DeleteNext();
        var replaced = ancestor.ReplaceNext(-2);

        Assert.Same(source, ancestor.Snapshot());
        Assert.Equal(sourceValues, ancestor.Snapshot().ToArray());
        Assert.Equal(
            sourceValues.Take(10).Append(-1).Concat(sourceValues.Skip(10)),
            inserted.Snapshot().ToArray());
        Assert.Equal(
            sourceValues.Where((_, index) => index != 9),
            deletedPrevious.Snapshot().ToArray());
        Assert.Equal(
            sourceValues.Where((_, index) => index != 10),
            deletedNext.Snapshot().ToArray());
        Assert.Equal(
            sourceValues.Select((value, index) => index == 10 ? -2 : value),
            replaced.Snapshot().ToArray());

        Assert.NotSame(inserted.VersionIdentityForDiagnostics, deletedPrevious.VersionIdentityForDiagnostics);
        Assert.NotSame(inserted.VersionIdentityForDiagnostics, deletedNext.VersionIdentityForDiagnostics);
        Assert.NotSame(inserted.VersionIdentityForDiagnostics, replaced.VersionIdentityForDiagnostics);
    }

    /// <summary>Concurrent first snapshots publish and return one reference across navigation contexts.</summary>
    [Fact]
    public void Snapshot_FirstCallRaceReturnsOneWinner()
    {
        var source = Rope<int>.Create(Enumerable.Range(0, 5000).ToArray());
        var edited = source.GetCursor(2048).Insert(-1);
        var contexts = new[]
        {
            edited,
            edited.MovePrevious(),
            edited.MoveNext(),
            edited.MoveNext().MoveNext(),
        };
        var snapshots = new Rope<int>[64];

        Assert.False(edited.GetDiagnostics().HasCachedSnapshot);
        Parallel.For(0, snapshots.Length, index =>
            snapshots[index] = contexts[index % contexts.Length].Snapshot());

        Assert.All(snapshots, snapshot => Assert.Same(snapshots[0], snapshot));
        Assert.All(contexts, context =>
        {
            Assert.Same(edited.VersionIdentityForDiagnostics, context.VersionIdentityForDiagnostics);
            Assert.True(context.GetDiagnostics().HasCachedSnapshot);
            Assert.Same(snapshots[0], context.Snapshot());
        });
        Assert.Equal(
            Enumerable.Range(0, 2048).Append(-1).Concat(Enumerable.Range(2048, 5000 - 2048)),
            snapshots[0].ToArray());
    }

    /// <summary>A failed candidate is never installed in the version's snapshot memo cell.</summary>
    [Fact]
    public void Snapshot_FailurePublishesNothing()
    {
        var context = Rope<int>.Empty.GetCursor().ContextForDiagnostics;
        var version = new CursorVersionState<int>(count: 1, context, snapshot: null);
        var invalid = new RopeCursor<int>(version, context);

        Assert.False(version.HasCachedSnapshot);
        Assert.Throws<InvalidOperationException>(() => invalid.Snapshot());
        Assert.False(version.HasCachedSnapshot);
        Assert.Throws<InvalidOperationException>(() => invalid.Snapshot());
        Assert.False(version.HasCachedSnapshot);
        Assert.Same(version, invalid.VersionIdentityForDiagnostics);
    }

    /// <summary>Count overflow is detected before any focus, carry, spine, or snapshot work is attempted.</summary>
    [Fact]
    public void InsertOverflow_IsRejectedBeforeAllocationOrStateChange()
    {
        var context = Rope<int>.Empty.GetCursor().ContextForDiagnostics;
        var version = new CursorVersionState<int>(int.MaxValue, context, snapshot: null);
        var cursor = new RopeCursor<int>(version, context);
        using var diagnostics = RopeCursorDiagnostics.BeginSession();

        Assert.Throws<OverflowException>(() => cursor.Insert(1));
        Assert.Throws<OverflowException>(() => cursor.InsertRange(new[] { 1 }));

        var counters = diagnostics.Snapshot;
        Assert.Equal(0, counters.NodeVisits);
        Assert.Equal(0, counters.SpineAllocations);
        Assert.Equal(0, counters.FocusCopies);
        Assert.Equal(0, counters.CarryCopies);
        Assert.Equal(0, counters.SnapshotNormalizations);
        Assert.Same(version, cursor.VersionIdentityForDiagnostics);
        Assert.True(cursor.HasSameContextStateForDiagnostics(new RopeCursor<int>(version, context)));
    }

    /// <summary>Snapshots retain untouched chunk storage while the source remains independently valid.</summary>
    [Fact]
    public void EditedSnapshot_SharesUntouchedStorageWithSource()
    {
        var sourceValues = Enumerable.Range(0, 10_000).ToArray();
        var source = Rope<int>.Create(sourceValues);
        var edited = source.GetCursor(4096).Insert(-1).Snapshot();

        Assert.Equal(sourceValues, source.ToArray());
        Assert.Equal(10_001, edited.Count);
        Assert.Equal(-1, edited[4096]);
        Assert.True(source.CountSharedBackingStoresForDiagnostics(edited) >= 2);
        source.ValidateInvariants();
        edited.ValidateInvariants();
    }

    /// <summary>Independent fan-out is bounded per branch at both small and deep rope boundaries.</summary>
    [Theory]
    [InlineData(257, 8)]
    [InlineData(257, 64)]
    [InlineData(4097, 8)]
    [InlineData(4097, 64)]
    public void BoundaryFanout_ScalesWithBranchCountUnderConservativeBound(int size, int branchCount)
    {
        var values = Enumerable.Range(0, size).ToArray();
        var source = Rope<int>.Create(values);
        var position = Math.Min(256, size);
        var ancestor = source.GetCursor(position);
        using var diagnostics = RopeCursorDiagnostics.BeginSession();

        for (var branch = 0; branch < branchCount; branch++)
        {
            var edited = ancestor.Insert(-branch - 1);
            var snapshot = edited.Snapshot();
            Assert.Equal(size + 1, snapshot.Count);
            Assert.Equal(-branch - 1, snapshot[position]);
            Assert.Equal(values[position], snapshot[position + 1]);
        }

        var counters = diagnostics.Snapshot;
        var conservativePerBranch = 2 + (int)Math.Ceiling(Math.Log2(size + 1));
        Assert.InRange(counters.SpineAllocations, 0, (long)branchCount * conservativePerBranch);
        Assert.InRange(counters.FocusCopies, branchCount, (long)branchCount * 4);
        Assert.Same(source, ancestor.Snapshot());
        Assert.Equal(values, ancestor.Snapshot().ToArray());
    }

    private static void AssertCursor(RopeCursor<int> cursor, IReadOnlyList<int> expected, int position)
    {
        AssertGap(cursor, expected, position);
        cursor.Validate();
        var snapshot = cursor.Snapshot();
        Assert.Same(snapshot, cursor.Snapshot());
        Assert.Equal(expected, snapshot.ToArray());
        snapshot.ValidateInvariants();

        var diagnostics = cursor.GetDiagnostics();
        Assert.Equal(16, diagnostics.FocusCapacity);
        Assert.Equal(256, diagnostics.FlushChunkSize);
        Assert.InRange(diagnostics.ActiveLength, 0, 16);
        Assert.InRange(diagnostics.LeftCarryLength, 0, 255);
        Assert.InRange(diagnostics.RightCarryLength, 0, 255);
    }

    private static void AssertGap(RopeCursor<int> cursor, IReadOnlyList<int> expected, int position)
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

    private sealed class ThrowingEquality(int id)
    {
        /// <summary>Gets the identifier.</summary>
        public int Id { get; } = id;

        public override bool Equals(object? obj) => throw new InvalidOperationException("Equality must not be called.");

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public override int GetHashCode() => Id;
    }
}
