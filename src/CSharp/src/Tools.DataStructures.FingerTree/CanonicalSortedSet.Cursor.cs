using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

public sealed partial class CanonicalSortedSet<T>
{
    /// <summary>Creates an immutable comparer-ordered gap cursor at a rank in <c>0..Count</c>.</summary>
    public CanonicalSortedSetCursor<T> GetCursor(int position = 0) => new(this, position);

    /// <summary>Creates a cursor before the first equivalent or greater item.</summary>
    public CanonicalSortedSetCursor<T> GetCursorAtLowerBound(T item) =>
        new(this, CursorBoundRank(item, upper: false));

    /// <summary>Creates a cursor after the equivalent item, when present.</summary>
    public CanonicalSortedSetCursor<T> GetCursorAtUpperBound(T item) =>
        new(this, CursorBoundRank(item, upper: true));

    /// <summary>Tries to locate an item; a miss returns its lower-bound cursor.</summary>
    public bool TryGetCursor(T item, out CanonicalSortedSetCursor<T> cursor)
    {
        var position = CursorBoundRank(item, upper: false);
        cursor = new(this, position);
        return position < Count && Policy.Comparer.Compare(CursorEntryAt(position), item) == 0;
    }

    private int CursorBoundRank(T item, bool upper)
    {
        var rank = 0;
        var node = _root;
        while (node is not null)
        {
            var comparison = Policy.Comparer.Compare(node.Item, item);
            if (comparison < 0 || (upper && comparison == 0))
            {
                rank = checked(rank + (node.Left?.Count ?? 0) + 1);
                node = node.Right;
            }
            else
            {
                node = node.Left;
            }
        }
        return rank;
    }

    internal T CursorEntryAt(int rank)
    {
        if ((uint)rank >= (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(rank));
        var node = _root!;
        while (true)
        {
            var leftCount = node.Left?.Count ?? 0;
            if (rank < leftCount) { node = node.Left!; continue; }
            if (rank == leftCount) return node.Item;
            rank -= leftCount + 1;
            node = node.Right!;
        }
    }
}

/// <summary>Immutable policy-preserving root-plus-rank cursor over a canonical sorted set.</summary>
public readonly struct CanonicalSortedSetCursor<T>
{
    private readonly CanonicalSortedSet<T>? _snapshot;
    private readonly int _position;

    internal CanonicalSortedSetCursor(CanonicalSortedSet<T> snapshot, int position)
    {
        if ((uint)position > (uint)snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private CanonicalSortedSet<T> Value => _snapshot ?? throw UninitializedError();

    /// <summary>Gets the item count in this cursor version.</summary>
    public int Count => Value.Count;
    /// <summary>Gets the number of items before the gap.</summary>
    public int Position
    {
        get
        {
            _ = Value;
            return _position;
        }
    }
    /// <summary>Gets whether the gap is before every item.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is after every item.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the item immediately before the gap.</summary>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out T item)
    {
        if (Position == 0) { item = default; return false; }
        item = Value.CursorEntryAt(Position - 1);
        return true;
    }

    /// <summary>Attempts to read the item immediately after the gap.</summary>
    public bool TryPeekNext([MaybeNullWhen(false)] out T item)
    {
        if (Position == Count) { item = default; return false; }
        item = Value.CursorEntryAt(Position);
        return true;
    }

    /// <summary>Moves the gap one item toward the start.</summary>
    public CanonicalSortedSetCursor<T> MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The canonical-set cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one item toward the end.</summary>
    public CanonicalSortedSetCursor<T> MoveNext() => Position == Count
        ? throw new InvalidOperationException("The canonical-set cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute item rank.</summary>
    public CanonicalSortedSetCursor<T> SeekRank(int position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Uses the ordinary canonical insertion path and returns the gap after the representative.</summary>
    public CanonicalSortedSetCursor<T> Add(T item)
    {
        var location = Value.GetCursorAtLowerBound(item);
        var snapshot = Value.Add(item);
        return new(snapshot, checked(location.Position + 1));
    }

    /// <summary>Deletes the item immediately after the gap through the canonical removal path.</summary>
    public CanonicalSortedSetCursor<T> DeleteNext()
    {
        if (!TryPeekNext(out var item))
            throw new InvalidOperationException("The canonical-set cursor has no next item.");
        return new(Value.Remove(item), Position);
    }

    /// <summary>Deletes the item immediately before the gap through the canonical removal path.</summary>
    public CanonicalSortedSetCursor<T> DeletePrevious()
    {
        if (!TryPeekPrevious(out var item))
            throw new InvalidOperationException("The canonical-set cursor has no previous item.");
        return new(Value.Remove(item), Position - 1);
    }

    /// <summary>Returns the exact policy-preserving canonical-set version represented by this cursor.</summary>
    public CanonicalSortedSet<T> Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized canonical-set cursor.");
}
