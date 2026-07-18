using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Exercises the Axis 2 positional cursor prototype against deterministic list models over every
/// focus-capacity and carry-flush candidate in the C0 matrix.
/// </summary>
public sealed class RopeCursorPrototypeModelTests
{
    private static readonly int[] FocusCapacities = [16, 32, 64, 128];
    private static readonly int[] FlushChunkSizes = [256, 512, 1024, 2048];

    /// <summary>Mixed command histories preserve sequence, gap, memoization, invariants, and retained versions.</summary>
    [Fact]
    public void DeterministicHistories_AllCandidateConfigurationsMatchListModel()
    {
        foreach (var focusCapacity in FocusCapacities)
        foreach (var flushChunkSize in FlushChunkSizes)
        {
            var seed = unchecked(0x5A17 ^ (focusCapacity * 397) ^ (flushChunkSize * 7919));
            RunHistory(seed, focusCapacity, flushChunkSize);
        }
    }

    /// <summary>Long typing, seam crossings, and backspacing remain exact for representative tuning pairs.</summary>
    [Theory]
    [InlineData(16, 256)]
    [InlineData(32, 512)]
    [InlineData(64, 1024)]
    [InlineData(128, 2048)]
    public void LongTypingBackspaceAndSeamOscillation_MatchLinearModel(
        int focusCapacity,
        int flushChunkSize)
    {
        const int length = 4097;
        var cursor = Rope<int>.Empty.GetClassCursorPrototype(0, focusCapacity, flushChunkSize);

        for (var value = 0; value < length; value++)
        {
            cursor = cursor.Insert(value);
            if ((value & 127) == 127)
                AssertState(cursor, value + 1, value + 1, focusCapacity, flushChunkSize);
        }

        Assert.Equal(Enumerable.Range(0, length), cursor.Snapshot().ToArray());
        cursor.Snapshot().ValidateInvariants();

        var seamPosition = 2048;
        cursor = cursor.Seek(seamPosition);
        var stride = focusCapacity + 1;
        for (var cycle = 0; cycle < 64; cycle++)
        {
            for (var step = 0; step < stride; step++)
                cursor = cursor.MoveNext();
            for (var step = 0; step < stride; step++)
                cursor = cursor.MovePrevious();

            Assert.Equal(seamPosition, cursor.Position);
            Assert.True(cursor.TryPeekPrevious(out var previous));
            Assert.True(cursor.TryPeekNext(out var next));
            Assert.Equal(seamPosition - 1, previous);
            Assert.Equal(seamPosition, next);
            AssertState(cursor, length, seamPosition, focusCapacity, flushChunkSize);
        }

        cursor = cursor.Seek(cursor.Count);
        for (var remaining = length; remaining > 0; remaining--)
        {
            cursor = cursor.DeletePrevious();
            if ((remaining & 127) == 1)
            {
                AssertState(cursor, remaining - 1, remaining - 1, focusCapacity, flushChunkSize);
                Assert.Equal(Enumerable.Range(0, remaining - 1), cursor.Snapshot().ToArray());
            }
        }

        Assert.Equal(0, cursor.Count);
        Assert.Equal(0, cursor.Position);
        Assert.True(cursor.IsAtStart);
        Assert.True(cursor.IsAtEnd);
        Assert.Same(Rope<int>.Empty, cursor.Snapshot());
        AssertState(cursor, 0, 0, focusCapacity, flushChunkSize);
    }

    private static void RunHistory(int seed, int focusCapacity, int flushChunkSize)
    {
        var random = new Random(seed);
        var model = Enumerable.Range(0, 257).ToList();
        var position = random.Next(model.Count + 1);
        var cursor = Rope<int>.Create(model.ToArray())
            .GetClassCursorPrototype(position, focusCapacity, flushChunkSize);
        var retained = new List<(RopeCursorPrototype<int> Cursor, int[] Values, int Position)>();
        var nextValue = -1;

        for (var step = 0; step < 320; step++)
        {
            switch (random.Next(10))
            {
                case 0:
                    if (position == 0)
                    {
                        Assert.Throws<InvalidOperationException>(() => cursor.MovePrevious());
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
                        Assert.Throws<InvalidOperationException>(() => cursor.MoveNext());
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
                    var length = random.Next(13);
                    var values = Enumerable.Range(nextValue - length + 1, length).Reverse().ToArray();
                    nextValue -= length;
                    var before = cursor;
                    cursor = cursor.InsertRange(values);
                    if (length == 0)
                    {
                        Assert.Same(before, cursor);
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

                default:
                {
                    var boundary = new[] { 0, 1, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 257 };
                    position = Math.Min(boundary[random.Next(boundary.Length)], model.Count);
                    cursor = cursor.Seek(position);
                    break;
                }
            }

            AssertCursorMatchesModel(cursor, model, position, focusCapacity, flushChunkSize);
        }

        foreach (var (retainedCursor, values, retainedPosition) in retained)
        {
            Assert.Equal(retainedPosition, retainedCursor.Position);
            var snapshot = retainedCursor.Snapshot();
            Assert.Same(snapshot, retainedCursor.Snapshot());
            Assert.Equal(values, snapshot.ToArray());
            retainedCursor.Validate();
        }
    }

    private static void AssertCursorMatchesModel(
        RopeCursorPrototype<int> cursor,
        IReadOnlyList<int> model,
        int position,
        int focusCapacity,
        int flushChunkSize)
    {
        AssertState(cursor, model.Count, position, focusCapacity, flushChunkSize);

        Assert.Equal(position > 0, cursor.TryPeekPrevious(out var previous));
        if (position > 0)
            Assert.Equal(model[position - 1], previous);

        Assert.Equal(position < model.Count, cursor.TryPeekNext(out var next));
        if (position < model.Count)
            Assert.Equal(model[position], next);

        var snapshot = cursor.Snapshot();
        Assert.Same(snapshot, cursor.Snapshot());
        Assert.Equal(model, snapshot.ToArray());
        snapshot.ValidateInvariants();
    }

    private static void AssertState(
        RopeCursorPrototype<int> cursor,
        int count,
        int position,
        int focusCapacity,
        int flushChunkSize)
    {
        cursor.Validate();
        var diagnostics = cursor.GetDiagnostics();
        Assert.Equal(count, cursor.Count);
        Assert.Equal(position, cursor.Position);
        Assert.Equal(count, diagnostics.Count);
        Assert.Equal(position, diagnostics.Position);
        Assert.Equal(focusCapacity, diagnostics.FocusCapacity);
        Assert.Equal(flushChunkSize, diagnostics.FlushChunkSize);
        Assert.InRange(diagnostics.ActiveLength, 0, focusCapacity);
        Assert.InRange(diagnostics.LeftCarryLength, 0, flushChunkSize - 1);
        Assert.InRange(diagnostics.RightCarryLength, 0, flushChunkSize - 1);
        Assert.True(diagnostics.LeftOrdinaryChunkCount >= 0);
        Assert.True(diagnostics.RightOrdinaryChunkCount >= 0);
    }
}
