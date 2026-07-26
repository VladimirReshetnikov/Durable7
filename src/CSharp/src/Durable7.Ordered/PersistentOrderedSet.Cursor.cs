// Gap cursors over the persistent ordered set.

namespace Durable7.Ordered;

public sealed partial class PersistentOrderedSet<T>
{
    /// <summary>Creates an immutable explicit-order gap cursor at a position in <c>0..Count</c>.</summary>
    public PersistentOrderedSetCursor<T> GetCursor(int position = 0) => new(this, position);

    /// <summary>Tries to focus an equivalence class; a miss returns the append-position cursor.</summary>
    public bool TryGetCursor(T equalValue, out PersistentOrderedSetCursor<T> cursor)
    {
        var position = IndexOf(equalValue);
        cursor = new(this, position < 0 ? Count : position);
        return position >= 0;
    }
}

/// <summary>Immutable root-plus-position gap cursor over a persistent ordered set.</summary>
public readonly struct PersistentOrderedSetCursor<T>
{
    private readonly PersistentOrderedSet<T>? _snapshot;
    private readonly int _position;

    internal PersistentOrderedSetCursor(PersistentOrderedSet<T> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private PersistentOrderedSet<T> Value => _snapshot ?? throw UninitializedError();

    /// <summary>Gets the representative count in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the number of representatives before the gap.</summary>
    public int Position
    {
        get
        {
            _ = Value;
            return _position;
        }
    }
    /// <summary>Gets whether the gap precedes every representative.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap follows every representative.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the representative immediately before the gap.</summary>
    public bool TryPeekPrevious(out T item)
    {
        if (Position == 0) { item = default!; return false; }
        item = Value.GetAt(Position - 1);
        return true;
    }

    /// <summary>Attempts to read the representative immediately after the gap.</summary>
    public bool TryPeekNext(out T item)
    {
        if (Position == Count) { item = default!; return false; }
        item = Value.GetAt(Position);
        return true;
    }

    /// <summary>Moves the gap one representative toward the start.</summary>
    public PersistentOrderedSetCursor<T> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The ordered-set cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one representative toward the end.</summary>
    public PersistentOrderedSetCursor<T> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The ordered-set cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute explicit-order position.</summary>
    public PersistentOrderedSetCursor<T> Seek(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>
    /// Inserts an absent representative at the gap and returns its following gap. An equivalent
    /// stored representative preserves this cursor and its position.
    /// </summary>
    public PersistentOrderedSetCursor<T> Insert(T item)
    {
        var snapshot = Value.Insert(Position, item);
        return ReferenceEquals(snapshot, Value)
            ? this
            : new(snapshot, checked(Position + 1));
    }

    /// <summary>Attempts insertion at the gap without disturbing this cursor on equivalence.</summary>
    public bool TryInsert(T item, out PersistentOrderedSetCursor<T> result)
    {
        result = Insert(item);
        return !ReferenceEquals(result.Value, Value);
    }

    /// <summary>Deletes the representative immediately before the gap.</summary>
    public PersistentOrderedSetCursor<T> DeletePrevious() => Position == 0
        ? throw new InvalidOperationException("The ordered-set cursor has no previous representative.")
        : new(Value.RemoveAt(Position - 1), Position - 1);

    /// <summary>Deletes the representative immediately after the gap.</summary>
    public PersistentOrderedSetCursor<T> DeleteNext() => Position == Count
        ? throw new InvalidOperationException("The ordered-set cursor has no next representative.")
        : new(Value.RemoveAt(Position), Position);

    /// <summary>Returns the exact persistent ordered-set version represented by this cursor.</summary>
    public PersistentOrderedSet<T> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized ordered-set cursor.");
}
