using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Exercises the public positional cursor against a mutable list plus integer-gap model.</summary>
public sealed class RopeCursorModelTests
{
    private static readonly int[] ExactBoundaries = [0, 1, 15, 16, 255, 256, 257, 2047, 2048, 2049];

    /// <summary>Deterministic mixed histories preserve list contents, gap position, and retained versions.</summary>
    [Theory]
    [InlineData(0x51C0)]
    [InlineData(0x51C1)]
    [InlineData(0x51C2)]
    public void MixedCommandHistory_MatchesListGapModel(int seed)
    {
        var random = new Random(seed);
        var model = Enumerable.Range(0, 513).ToList();
        var position = ExactBoundaries[seed % ExactBoundaries.Length];
        position = Math.Min(position, model.Count);
        var cursor = Rope<int>.Create(model.ToArray()).GetCursor(position);
        var retained = new List<(RopeCursor<int> Cursor, int[] Values, int Position)>();
        var nextValue = -1;

        for (var step = 0; step < 700; step++)
        {
            switch (random.Next(12))
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

                default:
                    if (retained.Count > 0)
                    {
                        var retainedState = retained[random.Next(retained.Count)];
                        var branch = retainedState.Cursor.Insert(nextValue--);
                        Assert.Equal(retainedState.Position + 1, branch.Position);
                        Assert.Equal(
                            retainedState.Values.Take(retainedState.Position)
                                .Append(nextValue + 1)
                                .Concat(retainedState.Values.Skip(retainedState.Position)),
                            branch.Snapshot().ToArray());
                        Assert.Equal(retainedState.Values, retainedState.Cursor.Snapshot().ToArray());
                    }
                    break;
            }

            AssertState(cursor, model, position);
            if ((step % 23) == 22)
            {
                var snapshot = cursor.Snapshot();
                Assert.Same(snapshot, cursor.Snapshot());
                Assert.Equal(model, snapshot.ToArray());
            }
        }

        Assert.Equal(model, cursor.Snapshot().ToArray());
        foreach (var retainedState in retained)
        {
            Assert.Equal(retainedState.Position, retainedState.Cursor.Position);
            Assert.Equal(retainedState.Values, retainedState.Cursor.Snapshot().ToArray());
            retainedState.Cursor.Validate();
        }
    }

    /// <summary>Every named C1 boundary supports exact peeks and reversible edits.</summary>
    [Theory]
    [MemberData(nameof(Boundaries))]
    public void NamedBoundaries_AllOperationsHaveExactGapBehavior(int position)
    {
        var expected = Enumerable.Range(0, 4098).ToArray();
        var source = Rope<int>.Create(expected);
        var cursor = source.GetCursor(position);

        AssertState(cursor, expected, position);

        var inserted = cursor.Insert(-1);
        Assert.Equal(position + 1, inserted.Position);
        Assert.Equal(
            expected.Take(position).Append(-1).Concat(expected.Skip(position)),
            inserted.Snapshot().ToArray());
        Assert.Equal(expected, inserted.DeletePrevious().Snapshot().ToArray());

        var range = cursor.InsertRange([-3, -2, -1]);
        Assert.Equal(position + 3, range.Position);
        Assert.Equal(
            expected.Take(position).Concat(new[] { -3, -2, -1 }).Concat(expected.Skip(position)),
            range.Snapshot().ToArray());
        Assert.Equal(expected, range.DeletePrevious().DeletePrevious().DeletePrevious().Snapshot().ToArray());

        if (position > 0)
        {
            var removed = cursor.DeletePrevious();
            Assert.Equal(position - 1, removed.Position);
            Assert.Equal(expected.Where((_, index) => index != position - 1), removed.Snapshot().ToArray());
            Assert.Equal(expected, removed.Insert(expected[position - 1]).Snapshot().ToArray());
        }

        if (position < expected.Length)
        {
            var removed = cursor.DeleteNext();
            Assert.Equal(position, removed.Position);
            Assert.Equal(expected.Where((_, index) => index != position), removed.Snapshot().ToArray());

            var replaced = cursor.ReplaceNext(-4);
            Assert.Equal(position, replaced.Position);
            Assert.Equal(
                expected.Select((value, index) => index == position ? -4 : value),
                replaced.Snapshot().ToArray());
        }

        Assert.Same(source, cursor.Snapshot());
    }

    /// <summary>Named source lengths expose exact endpoint gaps around focus, carry, and rope chunk seams.</summary>
    [Theory]
    [MemberData(nameof(Boundaries))]
    public void NamedLengths_StartAndEndRemainExact(int length)
    {
        var values = Enumerable.Range(0, length).ToArray();
        var source = Rope<int>.Create(values);
        var start = source.GetCursor();
        var end = source.GetCursor(length);

        AssertState(start, values, 0);
        AssertState(end, values, length);
        Assert.Same(source, start.Snapshot());
        Assert.Same(source, end.Snapshot());
    }

    /// <summary>Long typing, seam oscillation, and backspace keep both focus and carries bounded.</summary>
    [Fact]
    public void LongTypingBackspaceAndSeamOscillation_PreserveBoundsAndContents()
    {
        const int length = 4097;
        var cursor = Rope<int>.Empty.GetCursor();

        for (var value = 0; value < length; value++)
        {
            cursor = cursor.Insert(value);
            if ((value & 255) == 255)
                AssertRepresentationBounds(cursor);
        }

        Assert.Equal(Enumerable.Range(0, length), cursor.Snapshot().ToArray());
        Assert.True(cursor.GetDiagnostics().LeftOrdinaryChunkCount >= 8);

        cursor = cursor.Seek(2048);
        for (var cycle = 0; cycle < 96; cycle++)
        {
            for (var step = 0; step < 17; step++)
                cursor = cursor.MoveNext();
            for (var step = 0; step < 17; step++)
                cursor = cursor.MovePrevious();

            Assert.Equal(2048, cursor.Position);
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
        Assert.Same(Rope<int>.Empty, cursor.Snapshot());
        AssertRepresentationBounds(cursor);
    }

    /// <summary>Gets the exact focus, carry, and ordinary-chunk boundaries used by the C1 contract.</summary>
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

    private static void AssertState(RopeCursor<int> cursor, IReadOnlyList<int> expected, int position)
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

        cursor.Validate();
        AssertRepresentationBounds(cursor);
    }

    private static void AssertRepresentationBounds(RopeCursor<int> cursor)
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
}
