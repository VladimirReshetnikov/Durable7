// Gap cursors over the finger tree deque.

using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

public sealed partial class FingerTreeDeque<T>
{
    /// <summary>Creates an immutable positional cursor at a gap in <c>0..Count</c>.</summary>
    public FingerTreeDequeCursor<T> GetCursor(int position = 0) => new(this, position);
}

/// <summary>Immutable snapshot-plus-position gap cursor over a persistent finger-tree deque.</summary>
public readonly struct FingerTreeDequeCursor<T>
{
    private readonly FingerTreeDeque<T>? _snapshot;
    private readonly int _position;

    internal FingerTreeDequeCursor(FingerTreeDeque<T> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private FingerTreeDeque<T> Value => _snapshot ?? throw UninitializedError();
    /// <summary>Gets the number of elements in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the gap position in <c>0..Count</c>.</summary>
    public int Position
    {
        get
        {
            _ = Value;
            return _position;
        }
    }
    /// <summary>Gets whether the gap is before the first element.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is after the final element.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the element immediately before the gap.</summary>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out T value) =>
        Position == 0 ? Missing(out value) : Value.TryGetItem(Position - 1, out value);

    /// <summary>Attempts to read the element immediately after the gap.</summary>
    public bool TryPeekNext([MaybeNullWhen(false)] out T value) =>
        Value.TryGetItem(Position, out value);

    /// <summary>Moves the gap one element toward the start.</summary>
    public FingerTreeDequeCursor<T> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The deque cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one element toward the end.</summary>
    public FingerTreeDequeCursor<T> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The deque cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute boundary position.</summary>
    public FingerTreeDequeCursor<T> Seek(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Inserts one element and returns the gap immediately after it.</summary>
    public FingerTreeDequeCursor<T> Insert(T value) =>
        new(Value.InsertAt(Position, value), checked(Position + 1));

    /// <summary>Inserts one captured range and returns the gap immediately after it.</summary>
    public FingerTreeDequeCursor<T> InsertRange(IEnumerable<T> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        var snapshot = Value.InsertRange(Position, values);
        return ReferenceEquals(snapshot, Value)
            ? this
            : new(snapshot, checked(Position + snapshot.Count - Count));
    }

    /// <summary>Deletes the previous element and moves the gap left.</summary>
    public FingerTreeDequeCursor<T> DeletePrevious() => Position == 0
        ? throw new InvalidOperationException("The deque cursor has no previous element.")
        : new(Value.RemoveAt(Position - 1), Position - 1);

    /// <summary>Deletes the next element and keeps the gap fixed.</summary>
    public FingerTreeDequeCursor<T> DeleteNext() => Position == Count
        ? throw new InvalidOperationException("The deque cursor has no next element.")
        : new(Value.RemoveAt(Position), Position);

    /// <summary>Replaces the next element unconditionally and keeps the gap fixed.</summary>
    public FingerTreeDequeCursor<T> ReplaceNext(T value) => Position == Count
        ? throw new InvalidOperationException("The deque cursor has no next element.")
        : new(Value.SetItem(Position, value), Position);

    /// <summary>Returns the exact persistent deque version represented by this cursor.</summary>
    public FingerTreeDeque<T> Snapshot() => Value;

    private static bool Missing([MaybeNullWhen(false)] out T value)
    {
        value = default;
        return false;
    }

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized deque cursor.");
}
