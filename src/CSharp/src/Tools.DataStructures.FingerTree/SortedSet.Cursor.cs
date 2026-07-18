using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

public sealed partial class SortedSet<T>
{
    /// <summary>Creates an immutable ordered gap cursor at a rank in <c>0..Count</c>.</summary>
    public SortedSetCursor<T> GetCursor(int position = 0) => new(this, position);

    /// <summary>Creates a cursor before the first comparer-equivalent or greater item.</summary>
    public SortedSetCursor<T> GetCursorAtLowerBound(T item) =>
        new(this, CountBelow(new KeyAtLeastPredicate<T>(_comparer, item)));

    /// <summary>Creates a cursor after the comparer-equivalent item, when present.</summary>
    public SortedSetCursor<T> GetCursorAtUpperBound(T item) =>
        new(this, CountBelow(new KeyAbovePredicate<T>(_comparer, item)));

    /// <summary>Tries to locate an equivalent item; a miss returns its lower-bound cursor.</summary>
    public bool TryGetCursor(T item, out SortedSetCursor<T> cursor)
    {
        var position = CountBelow(new KeyAtLeastPredicate<T>(_comparer, item));
        cursor = new(this, position);
        return position < Count && _comparer.Compare(this[position], item) == 0;
    }
}

/// <summary>Immutable root-plus-rank cursor over a persistent sorted set.</summary>
public readonly struct SortedSetCursor<T>
{
    private readonly SortedSet<T>? _snapshot;

    internal SortedSetCursor(SortedSet<T> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        Position = position;
    }

    private SortedSet<T> Value => _snapshot ?? throw UninitializedError();

    /// <summary>Gets the item count in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the number of items before the gap.</summary>
    public int Position { get; }
    /// <summary>Gets whether the gap is before every item.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is after every item.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the item immediately before the gap.</summary>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out T item)
    {
        if (Position == 0) { item = default; return false; }
        item = Value[Position - 1];
        return true;
    }

    /// <summary>Attempts to read the item immediately after the gap.</summary>
    public bool TryPeekNext([MaybeNullWhen(false)] out T item)
    {
        if (Position == Count) { item = default; return false; }
        item = Value[Position];
        return true;
    }

    /// <summary>Moves the gap one item toward the start.</summary>
    public SortedSetCursor<T> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The sorted-set cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one item toward the end.</summary>
    public SortedSetCursor<T> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The sorted-set cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute item rank.</summary>
    public SortedSetCursor<T> SeekRank(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Adds by comparer order and returns the gap after the stored representative.</summary>
    public SortedSetCursor<T> Add(T item)
    {
        var location = Value.GetCursorAtLowerBound(item);
        if (location.TryPeekNext(out var found) && Value.Comparer.Compare(found, item) == 0)
            return new(Value, checked(location.Position + 1));
        return new(Value.Add(item), checked(location.Position + 1));
    }

    /// <summary>Deletes the item immediately after the gap.</summary>
    public SortedSetCursor<T> DeleteNext()
    {
        if (!TryPeekNext(out var item))
            throw new InvalidOperationException("The sorted-set cursor has no next item.");
        return new(Value.Remove(item), Position);
    }

    /// <summary>Deletes the item immediately before the gap.</summary>
    public SortedSetCursor<T> DeletePrevious()
    {
        if (!TryPeekPrevious(out var item))
            throw new InvalidOperationException("The sorted-set cursor has no previous item.");
        return new(Value.Remove(item), Position - 1);
    }

    /// <summary>Returns the exact persistent set version represented by this cursor.</summary>
    public SortedSet<T> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized sorted-set cursor.");
}
