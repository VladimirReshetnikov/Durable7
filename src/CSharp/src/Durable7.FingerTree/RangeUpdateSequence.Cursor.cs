// Gap cursors over the range update sequence.

using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

public sealed partial class RangeUpdateSequence<TElement, TMeasure, TTag, TOps>
{
    /// <summary>Creates an immutable positional and measured cursor at a gap in <c>0..Count</c>.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> GetCursor(int position = 0) =>
        new(this, position);
}

/// <summary>Immutable snapshot-plus-position cursor over a persistent lazy range-update sequence.</summary>
public readonly struct RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps>
    where TOps : IRangeUpdateAlgebra<TElement, TMeasure, TTag>
{
    private readonly RangeUpdateSequence<TElement, TMeasure, TTag, TOps>? _snapshot;
    private readonly int _position;

    internal RangeUpdateSequenceCursor(
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> snapshot,
        int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Value =>
        _snapshot ?? throw UninitializedError();

    /// <summary>Gets the number of logical elements in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the logical gap position in <c>0..Count</c>.</summary>
    public int Position
    {
        get
        {
            _ = Value;
            return _position;
        }
    }
    /// <summary>Gets whether the gap is at the start.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is at the end.</summary>
    public bool IsAtEnd => Position == Count;
    /// <summary>Gets the ordered logical measure before the gap.</summary>
    public TMeasure MeasureBefore => Value.MeasureRange(0, Position);
    /// <summary>Gets the ordered logical measure after the gap.</summary>
    public TMeasure MeasureAfter => Value.MeasureRange(Position, Count - Position);

    /// <summary>Attempts to read the logical element immediately before the gap.</summary>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out TElement value)
    {
        if (Position == 0) { value = default; return false; }
        value = Value[Position - 1];
        return true;
    }

    /// <summary>Attempts to read the logical element immediately after the gap.</summary>
    public bool TryPeekNext([MaybeNullWhen(false)] out TElement value)
    {
        if (Position == Count) { value = default; return false; }
        value = Value[Position];
        return true;
    }

    /// <summary>Moves the gap one logical element toward the start.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The Range cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one logical element toward the end.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The Range cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute logical boundary.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> Seek(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Inserts an untagged element and returns the gap immediately after it.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> Insert(TElement value) =>
        new(Value.Insert(Position, value), checked(Position + 1));

    /// <summary>Deletes the logical previous element and moves the gap left.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> DeletePrevious() => Position == 0
        ? throw new InvalidOperationException("The Range cursor has no previous element.")
        : new(Value.RemoveAt(Position - 1), Position - 1);

    /// <summary>Deletes the logical next element and keeps the gap fixed.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> DeleteNext() => Position == Count
        ? throw new InvalidOperationException("The Range cursor has no next element.")
        : new(Value.RemoveAt(Position), Position);

    /// <summary>Replaces the logical next element without applying its old tags to the replacement.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> ReplaceNext(TElement value) =>
        Position == Count
            ? throw new InvalidOperationException("The Range cursor has no next element.")
            : new(Value.SetItem(Position, value), Position);

    /// <summary>Measures the final <paramref name="count"/> elements before the gap in source order.</summary>
    public TMeasure MeasurePrevious(int count)
    {
        CheckDirectionalCount(count, Position);
        return Value.MeasureRange(Position - count, count);
    }

    /// <summary>Measures the first <paramref name="count"/> elements after the gap in source order.</summary>
    public TMeasure MeasureNext(int count)
    {
        CheckDirectionalCount(count, Count - Position);
        return Value.MeasureRange(Position, count);
    }

    /// <summary>Applies a tag to the final <paramref name="count"/> elements before the gap.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> ApplyPrevious(
        int count,
        TTag tag)
    {
        CheckDirectionalCount(count, Position);
        var snapshot = Value.ApplyRange(Position - count, count, tag);
        return ReferenceEquals(snapshot, Value) ? this : new(snapshot, Position);
    }

    /// <summary>Applies a tag to the first <paramref name="count"/> elements after the gap.</summary>
    public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> ApplyNext(
        int count,
        TTag tag)
    {
        CheckDirectionalCount(count, Count - Position);
        var snapshot = Value.ApplyRange(Position, count, tag);
        return ReferenceEquals(snapshot, Value) ? this : new(snapshot, Position);
    }

    /// <summary>Returns the exact persistent Range version represented by this cursor.</summary>
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Snapshot() => Value;

    private static void CheckDirectionalCount(int count, int available)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (count > available)
            throw new ArgumentOutOfRangeException(nameof(count));
    }

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized Range cursor.");
}
