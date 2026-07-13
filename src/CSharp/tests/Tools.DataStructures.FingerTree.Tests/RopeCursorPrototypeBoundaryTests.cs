using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Covers every legal positional-rope chunk length and the C0 prototype's focus/carry transition
/// boundaries without multiplying them into separate test processes.
/// </summary>
public sealed class RopeCursorPrototypeBoundaryTests
{
    private static readonly int[] FocusCapacities = [16, 32, 64, 128];
    private static readonly int[] FlushChunkSizes = [256, 512, 1024, 2048];

    /// <summary>Every one-chunk length exposes exact start/end gaps and remains the memoized source snapshot.</summary>
    [Fact]
    public void EveryChunkLengthFromOneThroughMaximum_HasExactEndpointGaps()
    {
        var values = Enumerable.Range(0, RopeChunking.MaxChunkSize).ToArray();

        for (var length = 1; length <= RopeChunking.MaxChunkSize; length++)
        {
            var rope = Rope<int>.Create(values.AsSpan(0, length));
            var start = rope.GetClassCursorPrototype(0, 16, 256);
            var end = rope.GetClassCursorPrototype(length, 16, 256);

            Assert.True(start.IsAtStart);
            Assert.False(start.IsAtEnd);
            Assert.False(start.TryPeekPrevious(out _));
            Assert.True(start.TryPeekNext(out var first));
            Assert.Equal(0, first);

            Assert.False(end.IsAtStart);
            Assert.True(end.IsAtEnd);
            Assert.True(end.TryPeekPrevious(out var last));
            Assert.Equal(length - 1, last);
            Assert.False(end.TryPeekNext(out _));

            var afterFirst = start.MoveNext();
            var beforeLast = end.MovePrevious();
            Assert.Equal(1, afterFirst.Position);
            Assert.Equal(length - 1, beforeLast.Position);
            Assert.True(afterFirst.TryPeekPrevious(out first));
            Assert.Equal(0, first);
            Assert.True(beforeLast.TryPeekNext(out last));
            Assert.Equal(length - 1, last);

            Assert.Same(rope, start.Snapshot());
            Assert.Same(rope, end.Snapshot());
            start.Validate();
            end.Validate();
        }
    }

    /// <summary>Minimum- and maximum-chunk neighbours remain exact under edits at nearby gaps for all tunings.</summary>
    [Theory]
    [InlineData(255)]
    [InlineData(256)]
    [InlineData(257)]
    [InlineData(2047)]
    [InlineData(2048)]
    [InlineData(2049)]
    public void ChunkTransitionLengths_AllConfigurationsRoundTripLocalEdits(int length)
    {
        var expected = Enumerable.Range(0, length).ToArray();
        var rope = Rope<int>.Create(expected);
        var candidatePositions = new[]
        {
            0,
            1,
            Math.Min(15, length),
            Math.Min(16, length),
            Math.Min(255, length),
            Math.Min(256, length),
            Math.Min(2047, length),
            Math.Min(2048, length),
            length - 1,
            length,
        }.Distinct().ToArray();

        foreach (var focusCapacity in FocusCapacities)
        foreach (var flushChunkSize in FlushChunkSizes)
        foreach (var position in candidatePositions)
        {
            var cursor = rope.GetClassCursorPrototype(position, focusCapacity, flushChunkSize);
            AssertGap(cursor, expected, position);

            var withRange = cursor.InsertRange([-2, -1]);
            Assert.Equal(position + 2, withRange.Position);
            Assert.Equal(
                expected.Take(position).Concat(new[] { -2, -1 }).Concat(expected.Skip(position)),
                withRange.Snapshot().ToArray());

            var rangeReverted = withRange.DeletePrevious().DeletePrevious();
            Assert.Equal(position, rangeReverted.Position);
            Assert.Equal(expected, rangeReverted.Snapshot().ToArray());
            AssertBounds(rangeReverted.GetDiagnostics(), focusCapacity, flushChunkSize);
            rangeReverted.Validate();

            if (position < length)
            {
                var removed = cursor.DeleteNext();
                var restored = removed.Insert(expected[position]);
                Assert.Equal(expected, restored.Snapshot().ToArray());
                Assert.Equal(position + 1, restored.Position);

                var replaced = cursor.ReplaceNext(-3);
                Assert.Equal(
                    expected.Select((value, index) => index == position ? -3 : value),
                    replaced.Snapshot().ToArray());
                Assert.Equal(position, replaced.Position);
                AssertBounds(replaced.GetDiagnostics(), focusCapacity, flushChunkSize);
            }

            if (position > 0)
            {
                var removed = cursor.DeletePrevious();
                var restored = removed.Insert(expected[position - 1]);
                Assert.Equal(expected, restored.Snapshot().ToArray());
                Assert.Equal(position, restored.Position);
            }
        }
    }

    /// <summary>Typing and reverse traversal force full carry chunks into both ordinary spines at every tuning.</summary>
    [Fact]
    public void CandidateConfigurations_FlushCarriesInBothDirectionsAndPreserveSequence()
    {
        foreach (var focusCapacity in FocusCapacities)
        foreach (var flushChunkSize in FlushChunkSizes)
        {
            var length = checked((flushChunkSize * 2) + focusCapacity + 3);
            var cursor = Rope<int>.Empty.GetClassCursorPrototype(0, focusCapacity, flushChunkSize);

            for (var value = 0; value < length; value++)
            {
                cursor = cursor.Insert(value);
                if ((value & 255) == 255)
                {
                    cursor.Validate();
                    AssertBounds(cursor.GetDiagnostics(), focusCapacity, flushChunkSize);
                }
            }

            var afterTyping = cursor.GetDiagnostics();
            Assert.True(afterTyping.LeftOrdinaryChunkCount >= 2);
            AssertBounds(afterTyping, focusCapacity, flushChunkSize);

            while (!cursor.IsAtStart)
            {
                cursor = cursor.MovePrevious();
                if ((cursor.Position & 255) == 0)
                {
                    cursor.Validate();
                    AssertBounds(cursor.GetDiagnostics(), focusCapacity, flushChunkSize);
                }
            }

            var afterReverseTraversal = cursor.GetDiagnostics();
            Assert.True(afterReverseTraversal.RightOrdinaryChunkCount >= 2);
            AssertBounds(afterReverseTraversal, focusCapacity, flushChunkSize);
            Assert.True(cursor.TryPeekNext(out var first));
            Assert.Equal(0, first);

            var snapshot = cursor.Snapshot();
            Assert.Same(snapshot, cursor.Snapshot());
            Assert.Equal(Enumerable.Range(0, length), snapshot.ToArray());
            snapshot.ValidateInvariants();
        }
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

    private static void AssertBounds(
        RopeCursorPrototypeStateDiagnostics diagnostics,
        int focusCapacity,
        int flushChunkSize)
    {
        Assert.Equal(focusCapacity, diagnostics.FocusCapacity);
        Assert.Equal(flushChunkSize, diagnostics.FlushChunkSize);
        Assert.InRange(diagnostics.Position, 0, diagnostics.Count);
        Assert.InRange(diagnostics.ActiveLength, 0, focusCapacity);
        Assert.InRange(diagnostics.LeftCarryLength, 0, flushChunkSize - 1);
        Assert.InRange(diagnostics.RightCarryLength, 0, flushChunkSize - 1);
        Assert.True(diagnostics.LeftOrdinaryChunkCount >= 0);
        Assert.True(diagnostics.RightOrdinaryChunkCount >= 0);
    }
}
