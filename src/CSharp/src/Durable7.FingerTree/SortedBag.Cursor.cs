// Gap cursors over the sorted bag.

using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

public sealed partial class SortedBag<T>
{
    /// <summary>Creates an immutable ordered gap cursor at a population rank in <c>0..Count</c>.</summary>
    public SortedBagCursor<T> GetCursor(int position = 0) => new(this, position);

    /// <summary>Creates a cursor immediately before the first comparer-equivalent or greater item.</summary>
    public SortedBagCursor<T> GetCursorAtLowerBound(T item) => new(this, CountLessThan(item));

    /// <summary>Creates a cursor immediately after the last comparer-equivalent item.</summary>
    public SortedBagCursor<T> GetCursorAtUpperBound(T item) => new(this, CountAtMost(item));

    /// <summary>Tries to locate the first comparer-equivalent occurrence; a miss returns its lower-bound cursor.</summary>
    public bool TryGetCursor(T item, out SortedBagCursor<T> cursor)
    {
        var position = CountLessThan(item);
        cursor = new(this, position);
        return position < Count && _comparer.Compare(this[position], item) == 0;
    }

    /// <summary>
    /// Removes the element at the given rank, used by the cursors to edit in place of a full descent.
    /// </summary>
    internal SortedBag<T> RemoveAtCursorRank(int rank)
    {
        var (before, atOrAfter) = _tree.Split(new CountAbovePredicate<T>(rank));
        if (!atOrAfter.TryViewLeft(out _, out var after))
            throw new ArgumentOutOfRangeException(nameof(rank));
        return Wrap(before.Concat(after));
    }
}

/// <summary>Immutable root-plus-rank cursor over a persistent sorted bag.</summary>
public readonly struct SortedBagCursor<T>
{
    private readonly SortedBag<T>? _snapshot;
    private readonly int _position;

    /// <summary>Creates a new sorted bag cursor.</summary>
    internal SortedBagCursor(SortedBag<T> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private SortedBag<T> Value => _snapshot ?? throw UninitializedError();

    /// <summary>Gets the occurrence count in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the number of occurrences before the gap.</summary>
    public int Position
    {
        get
        {
            _ = Value;
            return _position;
        }
    }
    /// <summary>Gets whether the gap is before every occurrence.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is after every occurrence.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the occurrence immediately before the gap.</summary>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out T item)
    {
        if (Position == 0) { item = default; return false; }
        item = Value[Position - 1];
        return true;
    }

    /// <summary>Attempts to read the occurrence immediately after the gap.</summary>
    public bool TryPeekNext([MaybeNullWhen(false)] out T item)
    {
        if (Position == Count) { item = default; return false; }
        item = Value[Position];
        return true;
    }

    /// <summary>Moves the gap one occurrence toward the start.</summary>
    public SortedBagCursor<T> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The sorted-bag cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one occurrence toward the end.</summary>
    public SortedBagCursor<T> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The sorted-bag cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute population rank.</summary>
    public SortedBagCursor<T> SeekRank(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Adds after the complete equal run and returns the gap after the new occurrence.</summary>
    public SortedBagCursor<T> Add(T item)
    {
        var insertionRank = Value.CountAtMost(item);
        return new(Value.Add(item), checked(insertionRank + 1));
    }

    /// <summary>Deletes the exact occurrence immediately before the gap.</summary>
    public SortedBagCursor<T> DeletePrevious() => Position == 0
        ? throw new InvalidOperationException("The sorted-bag cursor has no previous occurrence.")
        : new(Value.RemoveAtCursorRank(Position - 1), Position - 1);

    /// <summary>Deletes the exact occurrence immediately after the gap.</summary>
    public SortedBagCursor<T> DeleteNext() => Position == Count
        ? throw new InvalidOperationException("The sorted-bag cursor has no next occurrence.")
        : new(Value.RemoveAtCursorRank(Position), Position);

    /// <summary>Returns the exact persistent bag version represented by this cursor.</summary>
    public SortedBag<T> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized sorted-bag cursor.");
}
