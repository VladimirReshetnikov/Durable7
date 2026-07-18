using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

public sealed partial class RrbVector<T>
{
    /// <summary>Creates an immutable positional cursor at a gap in <c>0..Count</c>.</summary>
    public RrbVectorCursor<T> GetCursor(int position = 0) => new(this, position);
}

/// <summary>Immutable snapshot-plus-position gap cursor over a persistent RRB vector.</summary>
public readonly struct RrbVectorCursor<T>
{
    private readonly RrbVector<T>? _snapshot;

    internal RrbVectorCursor(RrbVector<T> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        Position = position;
    }

    private RrbVector<T> Value => _snapshot ?? throw UninitializedError();
    /// <summary>Gets the number of elements in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the gap position in <c>0..Count</c>.</summary>
    public int Position { get; }
    /// <summary>Gets whether the gap is at the start.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is at the end.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the element immediately before the gap.</summary>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out T value)
    {
        if (Position == 0) { value = default; return false; }
        value = Value[Position - 1];
        return true;
    }

    /// <summary>Attempts to read the element immediately after the gap.</summary>
    public bool TryPeekNext([MaybeNullWhen(false)] out T value)
    {
        if (Position == Count) { value = default; return false; }
        value = Value[Position];
        return true;
    }

    /// <summary>Moves the gap one element toward the start.</summary>
    public RrbVectorCursor<T> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The RRB cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one element toward the end.</summary>
    public RrbVectorCursor<T> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The RRB cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute boundary position.</summary>
    public RrbVectorCursor<T> Seek(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Inserts one element and returns the gap immediately after it.</summary>
    public RrbVectorCursor<T> Insert(T value) =>
        new(Value.InsertRange(Position, [value]), checked(Position + 1));

    /// <summary>Captures and inserts an enumerable range, then moves after it.</summary>
    public RrbVectorCursor<T> InsertRange(IEnumerable<T> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        var snapshot = Value.InsertRange(Position, values);
        return ReferenceEquals(snapshot, Value)
            ? this
            : new(snapshot, checked(Position + snapshot.Count - Count));
    }

    /// <summary>Splices an existing vector with structural sharing and moves after it.</summary>
    public RrbVectorCursor<T> InsertRange(RrbVector<T> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        if (values.IsEmpty)
            return this;
        var (left, right) = Value.SplitAt(Position);
        var snapshot = left.Concat(values).Concat(right);
        return new(snapshot, checked(Position + values.Count));
    }

    /// <summary>Deletes the previous element and moves the gap left.</summary>
    public RrbVectorCursor<T> DeletePrevious() => Position == 0
        ? throw new InvalidOperationException("The RRB cursor has no previous element.")
        : new(Value.RemoveRange(Position - 1, 1), Position - 1);

    /// <summary>Deletes the next element and keeps the gap fixed.</summary>
    public RrbVectorCursor<T> DeleteNext() => Position == Count
        ? throw new InvalidOperationException("The RRB cursor has no next element.")
        : new(Value.RemoveRange(Position, 1), Position);

    /// <summary>Replaces the next element using the vector's equality no-op rule.</summary>
    public RrbVectorCursor<T> ReplaceNext(T value) => Position == Count
        ? throw new InvalidOperationException("The RRB cursor has no next element.")
        : new(Value.SetItem(Position, value), Position);

    /// <summary>Returns the exact persistent vector version represented by this cursor.</summary>
    public RrbVector<T> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized RRB cursor.");
}
