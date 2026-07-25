using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Exercises the measured cursor against a mutable list, integer gap, and brute-force measure model.</summary>
public sealed class MeasuredRopeCursorModelTests
{
    private static readonly int[] ExactBoundaries = [0, 1, 15, 16, 255, 256, 257, 2047, 2048, 2049];

    /// <summary>Deterministic mixed histories preserve contents, gap, ordered measures, and retained versions.</summary>
    [Theory]
    [InlineData(0xC200)]
    [InlineData(0xC201)]
    [InlineData(0xC202)]
    public void MixedCommandHistory_MatchesListGapAndBruteMeasureModel(int seed)
    {
        var random = new Random(seed);
        var model = Enumerable.Range(1, 513).ToList();
        var position = Math.Min(ExactBoundaries[seed % ExactBoundaries.Length], model.Count);
        var cursor = MeasuredRope<int, string, OrderedTokenMeasure>.Create(model.ToArray()).GetCursor(position);
        var retained = new List<(MeasuredRopeCursor<int, string, OrderedTokenMeasure> Cursor, int[] Values, int Position)>();
        var nextValue = -1;

        for (var step = 0; step < 700; step++)
        {
            switch (random.Next(13))
            {
                case 0:
                    if (position == 0)
                    {
                        var before = cursor;
                        Assert.Throws<InvalidOperationException>(() => cursor.MovePrevious());
                        Assert.True(before.HasSameContextStateForDiagnostics(cursor));
                    }
                    else
                    {
                        cursor = cursor.MovePrevious();
                        position--;
                    }
                    break;

                case 1:
                    if (position == model.Count)
                    {
                        var before = cursor;
                        Assert.Throws<InvalidOperationException>(() => cursor.MoveNext());
                        Assert.True(before.HasSameContextStateForDiagnostics(cursor));
                    }
                    else
                    {
                        cursor = cursor.MoveNext();
                        position++;
                    }
                    break;

                case 2:
                    position = random.Next(model.Count + 1);
                    cursor = cursor.Seek(position);
                    break;

                case 3:
                    cursor = cursor.Insert(nextValue);
                    model.Insert(position, nextValue--);
                    position++;
                    break;

                case 4:
                {
                    var length = random.Next(9);
                    var values = Enumerable.Range(nextValue - length + 1, length).Reverse().ToArray();
                    nextValue -= length;
                    var before = cursor;
                    cursor = cursor.InsertRange(values);
                    if (length == 0)
                    {
                        Assert.Same(before.VersionIdentityForDiagnostics, cursor.VersionIdentityForDiagnostics);
                        Assert.True(before.HasSameContextStateForDiagnostics(cursor));
                    }
                    else
                    {
                        model.InsertRange(position, values);
                        position += length;
                    }
                    break;
                }

                case 5:
                    if (position == 0)
                    {
                        Assert.Throws<InvalidOperationException>(() => cursor.DeletePrevious());
                    }
                    else
                    {
                        cursor = cursor.DeletePrevious();
                        model.RemoveAt(position - 1);
                        position--;
                    }
                    break;

                case 6:
                    if (position == model.Count)
                    {
                        Assert.Throws<InvalidOperationException>(() => cursor.DeleteNext());
                    }
                    else
                    {
                        cursor = cursor.DeleteNext();
                        model.RemoveAt(position);
                    }
                    break;

                case 7:
                    if (position == model.Count)
                    {
                        Assert.Throws<InvalidOperationException>(() => cursor.ReplaceNext(nextValue));
                    }
                    else
                    {
                        cursor = cursor.ReplaceNext(nextValue);
                        model[position] = nextValue--;
                    }
                    break;

                case 8:
                    retained.Add((cursor, model.ToArray(), position));
                    break;

                case 9:
                {
                    var boundary = ExactBoundaries[random.Next(ExactBoundaries.Length)];
                    position = Math.Min(boundary, model.Count);
                    cursor = cursor.Seek(position);
                    break;
                }

                case 10:
                {
                    var same = cursor.Seek(position);
                    Assert.Same(cursor.VersionIdentityForDiagnostics, same.VersionIdentityForDiagnostics);
                    Assert.True(cursor.HasSameContextStateForDiagnostics(same));
                    cursor = same;
                    break;
                }

                case 11:
                    if (retained.Count > 0)
                    {
                        var retainedState = retained[random.Next(retained.Count)];
                        var insertedValue = nextValue--;
                        var branch = retainedState.Cursor.Insert(insertedValue);
                        Assert.Equal(retainedState.Position + 1, branch.Position);
                        Assert.Equal(
                            retainedState.Values.Take(retainedState.Position)
                                .Append(insertedValue)
                                .Concat(retainedState.Values.Skip(retainedState.Position)),
                            branch.Snapshot().ToArray());
                        Assert.Equal(retainedState.Values, retainedState.Cursor.Snapshot().ToArray());
                    }
                    break;

                default:
                {
                    var snapshot = cursor.Snapshot();
                    Assert.Same(snapshot, cursor.Snapshot());
                    Assert.Equal(model, snapshot.ToArray());
                    break;
                }
            }

            AssertState(cursor, model, position);
            if ((step % 29) == 28)
                Assert.Equal(model, cursor.Snapshot().ToArray());
        }

        Assert.Equal(model, cursor.Snapshot().ToArray());
        foreach (var retainedState in retained)
        {
            Assert.Equal(retainedState.Position, retainedState.Cursor.Position);
            Assert.Equal(retainedState.Values, retainedState.Cursor.Snapshot().ToArray());
            Assert.Equal(Measure(retainedState.Values), retainedState.Cursor.Snapshot().Measure);
            retainedState.Cursor.Validate();
        }
    }

    /// <summary>Every focus, carry, and 2,048-element chunk boundary has exact measured gap behavior.</summary>
    [Theory]
    [MemberData(nameof(Boundaries))]
    public void NamedBoundaries_AllOperationsHaveExactGapAndMeasureBehavior(int position)
    {
        var expected = Enumerable.Range(0, 4098).ToArray();
        var source = MeasuredRope<int, int, CountMeasure>.Create(expected);
        var cursor = source.GetCursor(position);

        AssertCountState(cursor, expected, position);

        var inserted = cursor.Insert(-1);
        Assert.Equal(position + 1, inserted.Position);
        Assert.Equal(expected.Length + 1, inserted.MeasureBefore + inserted.MeasureAfter);
        Assert.Equal(
            expected.Take(position).Append(-1).Concat(expected.Skip(position)),
            inserted.Snapshot().ToArray());
        Assert.Equal(expected, inserted.DeletePrevious().Snapshot().ToArray());

        var range = cursor.InsertRange([-3, -2, -1]);
        Assert.Equal(position + 3, range.Position);
        Assert.Equal(expected.Length + 3, range.MeasureBefore + range.MeasureAfter);
        Assert.Equal(
            expected.Take(position).Concat(new[] { -3, -2, -1 }).Concat(expected.Skip(position)),
            range.Snapshot().ToArray());
        Assert.Equal(expected, range.DeletePrevious().DeletePrevious().DeletePrevious().Snapshot().ToArray());

        if (position > 0)
        {
            var removed = cursor.DeletePrevious();
            Assert.Equal(position - 1, removed.Position);
            Assert.Equal(expected.Length - 1, removed.MeasureBefore + removed.MeasureAfter);
            Assert.Equal(expected.Where((_, index) => index != position - 1), removed.Snapshot().ToArray());
            Assert.Equal(expected, removed.Insert(expected[position - 1]).Snapshot().ToArray());
        }

        if (position < expected.Length)
        {
            var removed = cursor.DeleteNext();
            Assert.Equal(position, removed.Position);
            Assert.Equal(expected.Length - 1, removed.MeasureBefore + removed.MeasureAfter);
            Assert.Equal(expected.Where((_, index) => index != position), removed.Snapshot().ToArray());

            var replaced = cursor.ReplaceNext(-4);
            Assert.Equal(position, replaced.Position);
            Assert.Equal(expected.Length, replaced.MeasureBefore + replaced.MeasureAfter);
            Assert.Equal(
                expected.Select((value, index) => index == position ? -4 : value),
                replaced.Snapshot().ToArray());
        }

        Assert.Same(source, cursor.Snapshot());
    }

    /// <summary>Noncommutative prefixes and suffixes remain in logical order through movement and edits.</summary>
    [Fact]
    public void NoncommutativeMeasure_PreservesOrderAcrossFocusCarryAndChunkTransitions()
    {
        var model = Enumerable.Range(0, 300).ToList();
        var cursor = MeasuredRope<int, string, OrderedTokenMeasure>.Create(model.ToArray()).GetCursor();

        foreach (var position in new[] { 0, 1, 15, 16, 17, 254, 255, 256, 257, 300 })
        {
            cursor = cursor.Seek(position);
            AssertState(cursor, model, position);
        }

        cursor = cursor.Seek(255).Insert(-1).Insert(-2).DeletePrevious().ReplaceNext(-3);
        model.Insert(255, -1);
        model.Insert(256, -2);
        model.RemoveAt(256);
        model[256] = -3;

        AssertState(cursor, model, 256);
        var snapshot = cursor.Snapshot();
        Assert.Equal(Measure(model), snapshot.Measure);
        Assert.Equal(model, snapshot.ToArray());
        snapshot.ValidateInvariants();
    }

    /// <summary>Long typing, seam oscillation, and deletion preserve the selected 16/256 bounds and measure.</summary>
    [Fact]
    public void LongTypingDeletionAndSeamOscillation_PreserveBoundsContentsAndMeasure()
    {
        const int length = 2305;
        var cursor = MeasuredRope<int, int, CountMeasure>.Empty.GetCursor();

        for (var value = 0; value < length; value++)
        {
            cursor = cursor.Insert(value);
            if ((value & 255) == 255)
                AssertRepresentationBounds(cursor);
        }

        Assert.Equal(length, cursor.MeasureBefore);
        Assert.Equal(0, cursor.MeasureAfter);
        Assert.Equal(Enumerable.Range(0, length), cursor.Snapshot().ToArray());
        Assert.True(cursor.GetDiagnostics().LeftOrdinaryChunkCount >= 4);

        cursor = cursor.Seek(2048);
        for (var cycle = 0; cycle < 96; cycle++)
        {
            for (var step = 0; step < 17; step++)
                cursor = cursor.MoveNext();
            for (var step = 0; step < 17; step++)
                cursor = cursor.MovePrevious();

            Assert.Equal(2048, cursor.Position);
            Assert.Equal(2048, cursor.MeasureBefore);
            Assert.Equal(length - 2048, cursor.MeasureAfter);
            AssertRepresentationBounds(cursor);
        }

        cursor = cursor.Seek(cursor.Count);
        for (var remaining = length; remaining > 0; remaining--)
        {
            cursor = cursor.DeletePrevious();
            if ((remaining & 255) == 1)
                AssertRepresentationBounds(cursor);
        }

        Assert.Equal(0, cursor.Count);
        Assert.Equal(0, cursor.Position);
        Assert.Equal(0, cursor.MeasureBefore);
        Assert.Equal(0, cursor.MeasureAfter);
        Assert.Same(MeasuredRope<int, int, CountMeasure>.Empty, cursor.Snapshot());
        AssertRepresentationBounds(cursor);
    }

    /// <summary>Independent measured fan-out stays bounded per branch under C1's conservative proof scope.</summary>
    [Theory]
    [InlineData(257, 8)]
    [InlineData(257, 64)]
    [InlineData(4097, 8)]
    [InlineData(4097, 64)]
    public void BoundaryFanout_ScalesWithBranchCountAndPreservesMeasures(int size, int branchCount)
    {
        var values = Enumerable.Range(0, size).ToArray();
        var source = MeasuredRope<int, int, CountMeasure>.Create(values);
        var position = Math.Min(256, size);
        var ancestor = source.GetCursor(position);
        using var diagnostics = RopeCursorDiagnostics.BeginSession();

        for (var branch = 0; branch < branchCount; branch++)
        {
            var edited = ancestor.Insert(-branch - 1);
            var snapshot = edited.Snapshot();
            Assert.Equal(size + 1, snapshot.Count);
            Assert.Equal(size + 1, snapshot.Measure);
            Assert.Equal(-branch - 1, snapshot[position]);
            Assert.Equal(values[position], snapshot[position + 1]);
        }

        var counters = diagnostics.Snapshot;
        var conservativePerBranch = 2 + (int)Math.Ceiling(Math.Log2(size + 1));
        Assert.InRange(counters.SpineAllocations, 0, (long)branchCount * conservativePerBranch);
        Assert.InRange(counters.FocusCopies, branchCount, (long)branchCount * 4);
        Assert.Same(source, ancestor.Snapshot());
        Assert.Equal(size, ancestor.MeasureBefore + ancestor.MeasureAfter);
        Assert.Equal(values, ancestor.Snapshot().ToArray());
    }

    /// <summary>Gets the exact focus, carry, and ordinary-chunk boundaries used by C2.</summary>
    public static TheoryData<int> Boundaries
    {
        get
        {
            var data = new TheoryData<int>();
            foreach (var boundary in ExactBoundaries)
                data.Add(boundary);
            return data;
        }
    }

    private static void AssertState(
        MeasuredRopeCursor<int, string, OrderedTokenMeasure> cursor,
        IReadOnlyList<int> expected,
        int position)
    {
        Assert.Equal(expected.Count, cursor.Count);
        Assert.Equal(position, cursor.Position);
        Assert.Equal(position == 0, cursor.IsAtStart);
        Assert.Equal(position == expected.Count, cursor.IsAtEnd);
        Assert.Equal(Measure(expected.Take(position)), cursor.MeasureBefore);
        Assert.Equal(Measure(expected.Skip(position)), cursor.MeasureAfter);
        Assert.Equal(Measure(expected), OrderedTokenMeasure.Combine(cursor.MeasureBefore, cursor.MeasureAfter));

        Assert.Equal(position > 0, cursor.TryPeekPrevious(out var previous));
        if (position > 0)
            Assert.Equal(expected[position - 1], previous);

        Assert.Equal(position < expected.Count, cursor.TryPeekNext(out var next));
        if (position < expected.Count)
            Assert.Equal(expected[position], next);

        cursor.Validate();
        AssertRepresentationBounds(cursor);
    }

    private static void AssertCountState(
        MeasuredRopeCursor<int, int, CountMeasure> cursor,
        IReadOnlyList<int> expected,
        int position)
    {
        Assert.Equal(expected.Count, cursor.Count);
        Assert.Equal(position, cursor.Position);
        Assert.Equal(position, cursor.MeasureBefore);
        Assert.Equal(expected.Count - position, cursor.MeasureAfter);
        Assert.Equal(position > 0, cursor.TryPeekPrevious(out var previous));
        if (position > 0)
            Assert.Equal(expected[position - 1], previous);
        Assert.Equal(position < expected.Count, cursor.TryPeekNext(out var next));
        if (position < expected.Count)
            Assert.Equal(expected[position], next);
        cursor.Validate();
        AssertRepresentationBounds(cursor);
    }

    private static void AssertRepresentationBounds<TMeasure, TMeasureOps>(
        MeasuredRopeCursor<int, TMeasure, TMeasureOps> cursor)
        where TMeasureOps : IMeasure<int, TMeasure>
    {
        var diagnostics = cursor.GetDiagnostics();
        Assert.Equal(16, diagnostics.FocusCapacity);
        Assert.Equal(256, diagnostics.FlushChunkSize);
        Assert.InRange(diagnostics.Position, 0, diagnostics.Count);
        Assert.InRange(diagnostics.ActiveLength, 0, 16);
        Assert.InRange(diagnostics.LeftCarryLength, 0, 255);
        Assert.InRange(diagnostics.RightCarryLength, 0, 255);
        Assert.True(diagnostics.LeftOrdinaryChunkCount >= 0);
        Assert.True(diagnostics.RightOrdinaryChunkCount >= 0);
    }

    private static string Measure(IEnumerable<int> values) =>
        string.Concat(values.Select(OrderedTokenMeasure.Measure));

    private readonly struct CountMeasure : IMeasure<int, int>
    {
        public static int Empty => 0;

        public static int Measure(int element) => 1;

        public static int Combine(int left, int right) => left + right;
    }

    private readonly struct OrderedTokenMeasure : IMeasure<int, string>
    {
        public static string Empty => string.Empty;

        public static string Measure(int element) => $"[{element}]";

        public static string Combine(string left, string right) => string.Concat(left, right);
    }
}
