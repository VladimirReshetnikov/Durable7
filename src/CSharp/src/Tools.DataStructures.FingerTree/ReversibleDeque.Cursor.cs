using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

public sealed partial class ReversibleDeque<T>
{
    /// <summary>Creates an immutable positional cursor at a logical gap in <c>0..Count</c>.</summary>
    public ReversibleDequeCursor<T> GetCursor(int position = 0) => new(this, position);
}

/// <summary>Immutable logical-order gap cursor over a persistent reversible deque.</summary>
public readonly struct ReversibleDequeCursor<T>
{
    private readonly ReversibleDeque<T>? _snapshot;

    internal ReversibleDequeCursor(ReversibleDeque<T> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        Position = position;
    }

    private ReversibleDeque<T> Value => _snapshot ?? throw UninitializedError();
    /// <summary>Gets the number of elements in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the logical gap position in <c>0..Count</c>.</summary>
    public int Position { get; }
    /// <summary>Gets whether the logical gap is at the start.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the logical gap is at the end.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the logical element immediately before the gap.</summary>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out T value)
    {
        if (Position == 0) { value = default; return false; }
        value = Value[Position - 1];
        return true;
    }

    /// <summary>Attempts to read the logical element immediately after the gap.</summary>
    public bool TryPeekNext([MaybeNullWhen(false)] out T value)
    {
        if (Position == Count) { value = default; return false; }
        value = Value[Position];
        return true;
    }

    /// <summary>Moves the logical gap one element toward the start.</summary>
    public ReversibleDequeCursor<T> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The reversible-deque cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the logical gap one element toward the end.</summary>
    public ReversibleDequeCursor<T> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The reversible-deque cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the logical gap to an absolute position.</summary>
    public ReversibleDequeCursor<T> Seek(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Inserts one element in logical order and moves after it.</summary>
    public ReversibleDequeCursor<T> Insert(T value) =>
        new(Value.InsertAt(Position, value), checked(Position + 1));

    /// <summary>Captures and inserts a range in logical order, then moves after it.</summary>
    public ReversibleDequeCursor<T> InsertRange(IEnumerable<T> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        var items = values as IReadOnlyCollection<T> ?? values.ToArray();
        if (items.Count == 0)
            return this;
        _ = checked(Count + items.Count);
        var snapshot = Value;
        var position = Position;
        foreach (var value in items)
            snapshot = snapshot.InsertAt(position++, value);
        return new(snapshot, position);
    }

    /// <summary>Deletes the logical previous element and moves the gap left.</summary>
    public ReversibleDequeCursor<T> DeletePrevious() => Position == 0
        ? throw new InvalidOperationException("The reversible-deque cursor has no previous element.")
        : new(Value.RemoveAt(Position - 1), Position - 1);

    /// <summary>Deletes the logical next element and keeps the gap fixed.</summary>
    public ReversibleDequeCursor<T> DeleteNext() => Position == Count
        ? throw new InvalidOperationException("The reversible-deque cursor has no next element.")
        : new(Value.RemoveAt(Position), Position);

    /// <summary>Replaces the logical next element and keeps the gap fixed.</summary>
    public ReversibleDequeCursor<T> ReplaceNext(T value) => Position == Count
        ? throw new InvalidOperationException("The reversible-deque cursor has no next element.")
        : new(Value.SetItem(Position, value), Position);

    /// <summary>Reverses the logical version and maps this gap to <c>Count - Position</c>.</summary>
    public ReversibleDequeCursor<T> Reverse() => new(Value.Reverse(), Count - Position);

    /// <summary>Returns the exact persistent reversible-deque version represented by this cursor.</summary>
    public ReversibleDeque<T> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized reversible-deque cursor.");
}
