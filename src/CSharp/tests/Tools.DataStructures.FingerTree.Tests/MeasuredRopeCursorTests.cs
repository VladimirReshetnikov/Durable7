using System.Text;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Locks the public Axis 2 C2 measured-cursor lifecycle, gap, identity, and text contracts.</summary>
public sealed class MeasuredRopeCursorTests
{
    /// <summary>The default struct is invalid; the initialized empty cursor is obtained from the empty rope.</summary>
    [Fact]
    public void DefaultValue_AllMembersRejectItExplicitly()
    {
        var cursor = default(MeasuredRopeCursor<int, int, SumMeasure<int>>);

        Assert.Throws<InvalidOperationException>(() => _ = cursor.Count);
        Assert.Throws<InvalidOperationException>(() => _ = cursor.Position);
        Assert.Throws<InvalidOperationException>(() => _ = cursor.IsAtStart);
        Assert.Throws<InvalidOperationException>(() => _ = cursor.IsAtEnd);
        Assert.Throws<InvalidOperationException>(() => _ = cursor.MeasureBefore);
        Assert.Throws<InvalidOperationException>(() => _ = cursor.MeasureAfter);
        Assert.Throws<InvalidOperationException>(() => cursor.TryPeekPrevious(out _));
        Assert.Throws<InvalidOperationException>(() => cursor.TryPeekNext(out _));
        Assert.Throws<InvalidOperationException>(() => cursor.MovePrevious());
        Assert.Throws<InvalidOperationException>(() => cursor.MoveNext());
        Assert.Throws<InvalidOperationException>(() => cursor.Seek(0));
        Assert.Throws<InvalidOperationException>(() => cursor.TrySeekByMeasure(static sum => sum > 0, out _));
        Assert.Throws<InvalidOperationException>(() => cursor.TrySeekByMeasure(new SumAbove(0), out _));
        Assert.Throws<InvalidOperationException>(() => cursor.Insert(1));
        Assert.Throws<InvalidOperationException>(() => cursor.InsertRange(ReadOnlySpan<int>.Empty));
        Assert.Throws<InvalidOperationException>(() => cursor.DeletePrevious());
        Assert.Throws<InvalidOperationException>(() => cursor.DeleteNext());
        Assert.Throws<InvalidOperationException>(() => cursor.ReplaceNext(1));
        Assert.Throws<InvalidOperationException>(() => cursor.Snapshot());
        Assert.Throws<InvalidOperationException>(() => cursor.GetDiagnostics());
        Assert.Throws<InvalidOperationException>(() => cursor.Validate());

        var textCursor = default(MeasuredRopeCursor<char, int, NewlineMeasure>);
        Assert.Throws<InvalidOperationException>(() => textCursor.LineColumnOf());

        var empty = MeasuredRope<int, int, SumMeasure<int>>.Empty.GetCursor();
        Assert.Equal(0, empty.Count);
        Assert.Equal(0, empty.Position);
        Assert.True(empty.IsAtStart);
        Assert.True(empty.IsAtEnd);
        Assert.Equal(0, empty.MeasureBefore);
        Assert.Equal(0, empty.MeasureAfter);
        Assert.Same(MeasuredRope<int, int, SumMeasure<int>>.Empty, empty.Snapshot());
    }

    /// <summary>Peeks, movement, measures, and endpoint failures use the exact gap contract.</summary>
    [Fact]
    public void GapSemantics_PeeksMovesMeasuresAndEndpointFailuresAreExact()
    {
        var values = new[] { 10, 20, 30, 40, 50 };
        var source = MeasuredRope<int, int, SumMeasure<int>>.Create(values);
        var cursor = source.GetCursor(2);

        AssertGap(cursor, values, 2);
        AssertGap(cursor.MovePrevious(), values, 1);
        AssertGap(cursor.MoveNext(), values, 3);

        var start = cursor.Seek(0);
        AssertGap(start, values, 0);
        Assert.Throws<InvalidOperationException>(() => start.MovePrevious());
        Assert.Throws<InvalidOperationException>(() => start.DeletePrevious());

        var end = cursor.Seek(source.Count);
        AssertGap(end, values, source.Count);
        Assert.Throws<InvalidOperationException>(() => end.MoveNext());
        Assert.Throws<InvalidOperationException>(() => end.DeleteNext());
        Assert.Throws<InvalidOperationException>(() => end.ReplaceNext(0));

        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetCursor(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => source.GetCursor(source.Count + 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => cursor.Seek(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => cursor.Seek(source.Count + 1));
    }

    /// <summary>All edits apply at the gap, update the measure, copy range storage, and preserve the receiver.</summary>
    [Fact]
    public void Edits_ApplyAtGapAndPreserveReceiverAndMeasure()
    {
        var originalValues = Enumerable.Range(1, 10).ToArray();
        var source = MeasuredRope<int, int, SumMeasure<int>>.Create(originalValues);
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
        Assert.Equal(originalValues.Sum(), receiver.MeasureBefore + receiver.MeasureAfter);
    }

    /// <summary>No-op values preserve exact state; navigation shares a version and every edit forks it.</summary>
    [Fact]
    public void Identity_NoOpsNavigationEditsAndSnapshotsAreDistinguished()
    {
        var source = MeasuredRope<int, int, SumMeasure<int>>.Create(1, 2, 3, 4);
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
        Assert.Equal(19, inserted.Snapshot().Measure);

        var equalReplacement = cursor.ReplaceNext(2);
        Assert.NotSame(version, equalReplacement.VersionIdentityForDiagnostics);
        Assert.Equal(source.ToArray(), equalReplacement.Snapshot().ToArray());
        Assert.NotSame(source, equalReplacement.Snapshot());
    }

    /// <summary>Replacement never calls hidden element equality and always creates an edited measured version.</summary>
    [Fact]
    public void ReplaceNext_DoesNotConsultElementEquality()
    {
        var value = new ThrowingEquality(7);
        var source = MeasuredRope<ThrowingEquality, int, ThrowingEqualityMeasure>.Create(value);
        var cursor = source.GetCursor();

        var replaced = cursor.ReplaceNext(value);

        Assert.NotSame(cursor.VersionIdentityForDiagnostics, replaced.VersionIdentityForDiagnostics);
        Assert.Same(value, replaced.Snapshot()[0]);
        Assert.Equal(7, replaced.MeasureAfter);
        Assert.NotSame(source, replaced.Snapshot());
        Assert.Equal(0, replaced.Position);
    }

    /// <summary>Branches from one retained measured version are independent and leave their ancestor untouched.</summary>
    [Fact]
    public void RetainedBranches_AreIndependentAndMeasured()
    {
        var sourceValues = Enumerable.Range(1, 20).ToArray();
        var source = MeasuredRope<int, int, SumMeasure<int>>.Create(sourceValues);
        var ancestor = source.GetCursor(10);

        var inserted = ancestor.Insert(100);
        var deletedPrevious = ancestor.DeletePrevious();
        var deletedNext = ancestor.DeleteNext();
        var replaced = ancestor.ReplaceNext(200);

        Assert.Same(source, ancestor.Snapshot());
        Assert.Equal(sourceValues, ancestor.Snapshot().ToArray());
        Assert.Equal(
            sourceValues.Take(10).Append(100).Concat(sourceValues.Skip(10)),
            inserted.Snapshot().ToArray());
        Assert.Equal(sourceValues.Sum() + 100, inserted.Snapshot().Measure);
        Assert.Equal(
            sourceValues.Where((_, index) => index != 9),
            deletedPrevious.Snapshot().ToArray());
        Assert.Equal(
            sourceValues.Where((_, index) => index != 10),
            deletedNext.Snapshot().ToArray());
        Assert.Equal(
            sourceValues.Select((value, index) => index == 10 ? 200 : value),
            replaced.Snapshot().ToArray());

        Assert.NotSame(inserted.VersionIdentityForDiagnostics, deletedPrevious.VersionIdentityForDiagnostics);
        Assert.NotSame(inserted.VersionIdentityForDiagnostics, deletedNext.VersionIdentityForDiagnostics);
        Assert.NotSame(inserted.VersionIdentityForDiagnostics, replaced.VersionIdentityForDiagnostics);
    }

    /// <summary>Overflow is rejected before focus, carry, measure, combine, spine, or snapshot work.</summary>
    [Fact]
    public void InsertOverflow_IsRejectedBeforeCallbacksAllocationOrStateChange()
    {
        var context = MeasuredRope<int, int, SumMeasure<int>>.Empty.GetCursor().ContextForDiagnostics;
        var version = new MeasuredCursorVersionState<int, int, SumMeasure<int>>(
            int.MaxValue,
            context,
            snapshot: null);
        var cursor = new MeasuredRopeCursor<int, int, SumMeasure<int>>(version, context);
        using var diagnostics = RopeCursorDiagnostics.BeginSession();

        Assert.Throws<OverflowException>(() => cursor.Insert(1));
        Assert.Throws<OverflowException>(() => cursor.InsertRange(new[] { 1 }));

        var counters = diagnostics.Snapshot;
        Assert.Equal(0, counters.NodeVisits);
        Assert.Equal(0, counters.SpineAllocations);
        Assert.Equal(0, counters.FocusCopies);
        Assert.Equal(0, counters.CarryCopies);
        Assert.Equal(0, counters.SnapshotNormalizations);
        Assert.Equal(0, counters.ElementMeasureCallbacks);
        Assert.Equal(0, counters.MeasureCombineCallbacks);
        Assert.Same(version, cursor.VersionIdentityForDiagnostics);
        Assert.True(cursor.HasSameContextStateForDiagnostics(
            new MeasuredRopeCursor<int, int, SumMeasure<int>>(version, context)));
    }

    /// <summary>An invalid snapshot candidate is never installed in a measured version's memo cell.</summary>
    [Fact]
    public void Snapshot_InvalidCandidatePublishesNothing()
    {
        var context = MeasuredRope<int, int, SumMeasure<int>>.Empty.GetCursor().ContextForDiagnostics;
        var version = new MeasuredCursorVersionState<int, int, SumMeasure<int>>(
            count: 1,
            context,
            snapshot: null);
        var invalid = new MeasuredRopeCursor<int, int, SumMeasure<int>>(version, context);

        Assert.False(version.HasCachedSnapshot);
        Assert.Throws<InvalidOperationException>(() => invalid.Snapshot());
        Assert.False(version.HasCachedSnapshot);
        Assert.Throws<InvalidOperationException>(() => invalid.Snapshot());
        Assert.False(version.HasCachedSnapshot);
        Assert.Same(version, invalid.VersionIdentityForDiagnostics);
    }

    /// <summary>Edited snapshots retain untouched measured chunks and remain canonical measured ropes.</summary>
    [Fact]
    public void EditedSnapshot_SharesUntouchedStorageAndPreservesCanonicality()
    {
        var sourceValues = Enumerable.Range(1, 10_000).ToArray();
        var source = MeasuredRope<int, int, SumMeasure<int>>.Create(sourceValues);
        var edited = source.GetCursor(4096).Insert(-1).Snapshot();

        Assert.Equal(sourceValues, source.ToArray());
        Assert.Equal(10_001, edited.Count);
        Assert.Equal(-1, edited[4096]);
        Assert.Equal(sourceValues.Sum() - 1, edited.Measure);
        Assert.True(source.CountSharedBackingStoresForDiagnostics(edited) >= 2);
        source.ValidateInvariants();
        edited.ValidateInvariants();
    }

    /// <summary>The newline-measured cursor composes with all existing text and Unicode helpers.</summary>
    [Fact]
    public void TextCursor_LineColumnAndUnicodeHelpersRemainCompatible()
    {
        const string original = "alpha\r\nHi \ud83d\ude00 cafe\u0301\nlast";
        var source = original.ToTextRope();
        var cursor = source.GetCursor(original.IndexOf("cafe", StringComparison.Ordinal));
        cursor = cursor.InsertRange("bright ".AsSpan());
        var editedText = original.Insert(original.IndexOf("cafe", StringComparison.Ordinal), "bright ");

        for (var position = 0; position <= cursor.Count; position++)
        {
            var at = cursor.Seek(position);
            Assert.Equal(at.Snapshot().LineColumnOf(position), at.LineColumnOf());
            Assert.Equal(at.MeasureBefore, at.LineColumnOf().Line);
        }

        var snapshot = cursor.Snapshot();
        Assert.Equal(editedText, snapshot.AsString());
        Assert.Equal(editedText.EnumerateRunes().Count(), snapshot.CodePointCount());
        Assert.Equal(original.ToTextRope().GraphemeCount() + "bright ".ToTextRope().GraphemeCount(), snapshot.GraphemeCount());
        Assert.Equal(NewlineStyle.Mixed, snapshot.DetectNewlineStyle());
        Assert.Equal(snapshot.LineCount(), snapshot.Lines().Count());
        Assert.Equal(snapshot.LineCount(), snapshot.LinesText().Count());
    }

    private static void AssertCursor(
        MeasuredRopeCursor<int, int, SumMeasure<int>> cursor,
        IReadOnlyList<int> expected,
        int position)
    {
        AssertGap(cursor, expected, position);
        cursor.Validate();
        var snapshot = cursor.Snapshot();
        Assert.Same(snapshot, cursor.Snapshot());
        Assert.Equal(expected, snapshot.ToArray());
        Assert.Equal(expected.Sum(), snapshot.Measure);
        snapshot.ValidateInvariants();

        var diagnostics = cursor.GetDiagnostics();
        Assert.Equal(16, diagnostics.FocusCapacity);
        Assert.Equal(256, diagnostics.FlushChunkSize);
        Assert.InRange(diagnostics.ActiveLength, 0, 16);
        Assert.InRange(diagnostics.LeftCarryLength, 0, 255);
        Assert.InRange(diagnostics.RightCarryLength, 0, 255);
    }

    private static void AssertGap(
        MeasuredRopeCursor<int, int, SumMeasure<int>> cursor,
        IReadOnlyList<int> expected,
        int position)
    {
        Assert.Equal(expected.Count, cursor.Count);
        Assert.Equal(position, cursor.Position);
        Assert.Equal(position == 0, cursor.IsAtStart);
        Assert.Equal(position == expected.Count, cursor.IsAtEnd);
        Assert.Equal(expected.Take(position).Sum(), cursor.MeasureBefore);
        Assert.Equal(expected.Skip(position).Sum(), cursor.MeasureAfter);
        Assert.Equal(expected.Sum(), cursor.MeasureBefore + cursor.MeasureAfter);

        Assert.Equal(position > 0, cursor.TryPeekPrevious(out var previous));
        if (position > 0)
            Assert.Equal(expected[position - 1], previous);

        Assert.Equal(position < expected.Count, cursor.TryPeekNext(out var next));
        if (position < expected.Count)
            Assert.Equal(expected[position], next);
    }

    private readonly struct SumAbove(int threshold) : IMeasurePredicate<int>
    {
        public bool Invoke(int measure) => measure > threshold;
    }

    private sealed class ThrowingEquality(int id)
    {
        public int Id { get; } = id;

        public override bool Equals(object? obj) => throw new InvalidOperationException("Equality must not be called.");

        public override int GetHashCode() => Id;
    }

    private readonly struct ThrowingEqualityMeasure : IMeasure<ThrowingEquality, int>
    {
        public static int Empty => 0;

        public static int Measure(ThrowingEquality element) => element.Id;

        public static int Combine(int left, int right) => left + right;
    }
}
