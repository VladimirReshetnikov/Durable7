namespace Durable7.Hamt;

public sealed partial class MerkleSearchTree<TKey, TValue>
{
    /// <summary>Creates an immutable ordered cursor at a rank gap.</summary>
    /// <param name="position">The number of entries before the gap, in <c>0 .. Count</c>.</param>
    /// <returns>A cursor over this authenticated tree snapshot.</returns>
    public MerkleSearchTreeCursor<TKey, TValue> GetCursor(int position = 0)
    {
        MerkleCursorGuard.ValidatePosition(position, Count);
        return new MerkleSearchTreeCursor<TKey, TValue>(this, position);
    }

    /// <summary>Creates an immutable ordered cursor after the final entry.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> GetCursorAtEnd() => new(this, Count);

    /// <summary>Creates a cursor before the first entry whose key is not less than <paramref name="key"/>.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> GetLowerBoundCursor(TKey key) =>
        new(this, GetLowerBoundRank(key, out _));

    /// <summary>Creates a cursor before the first entry whose key is greater than <paramref name="key"/>.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> GetUpperBoundCursor(TKey key)
    {
        var rank = GetLowerBoundRank(key, out var found);
        return new MerkleSearchTreeCursor<TKey, TValue>(this, found ? checked(rank + 1) : rank);
    }

    /// <summary>Creates a lower-bound cursor and reports whether its next entry has <paramref name="key"/>.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> GetCursorAtKey(TKey key, out bool found)
    {
        var rank = GetLowerBoundRank(key, out found);
        return new MerkleSearchTreeCursor<TKey, TValue>(this, rank);
    }

    internal bool TryGetAtRank(int rank, out KeyValuePair<TKey, TValue> entry)
    {
        if ((uint)rank >= (uint)Count)
        {
            entry = default;
            return false;
        }

        var node = _root;
        while (node is not null)
        {
            for (var index = 0; index < node.Entries.Length; index++)
            {
                var childCount = node.Children[index]?.Count ?? 0;
                if (rank < childCount)
                {
                    node = node.Children[index];
                    goto ContinueDescent;
                }

                rank -= childCount;
                if (rank == 0)
                {
                    var item = node.Entries[index];
                    entry = KeyValuePair.Create(item.Key, item.Value);
                    return true;
                }
                rank--;
            }

            node = node.Children[^1];

        ContinueDescent:
            continue;
        }

        throw new InvalidOperationException("Merkle subtree counts do not contain the requested rank.");
    }

    internal int GetLowerBoundRank(TKey key, out bool found)
    {
        var rank = 0;
        var node = _root;
        while (node is not null)
        {
            var position = FindPosition(node.Entries, key, out found);
            for (var index = 0; index < position; index++)
                rank = checked(rank + (node.Children[index]?.Count ?? 0) + 1);

            if (found)
                return checked(rank + (node.Children[position]?.Count ?? 0));

            node = node.Children[position];
        }

        found = false;
        return rank;
    }
}

/// <summary>
/// An immutable gap cursor over a <see cref="MerkleSearchTree{TKey,TValue}"/> in policy-comparer order.
/// </summary>
/// <remarks>
/// The cursor is a tree-snapshot-plus-rank checkpoint. Navigation and edits return independent cursor
/// values; edits use the tree's canonical persistent operations, so policy identity, block encoding,
/// structural sharing, and content addressing are preserved. Rank peeks and key seeks traverse cached
/// subtree counts and the block separators on their search paths. The default struct value is invalid.
/// </remarks>
public readonly struct MerkleSearchTreeCursor<TKey, TValue>
{
    private readonly MerkleSearchTree<TKey, TValue>? _source;
    private readonly int _position;

    internal MerkleSearchTreeCursor(MerkleSearchTree<TKey, TValue> source, int position)
    {
        _source = source;
        _position = position;
    }

    /// <summary>Gets the entry count.</summary>
    public int Count => Source.Count;

    /// <summary>Gets the number of entries before the cursor gap.</summary>
    public int Position { get { _ = Source; return _position; } }

    /// <summary>Gets whether the gap precedes every entry.</summary>
    public bool IsAtStart => Position == 0;

    /// <summary>Gets whether the gap follows every entry.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the entry immediately before the gap.</summary>
    public bool TryPeekPrevious(out KeyValuePair<TKey, TValue> entry)
    {
        var source = Source;
        if (_position > 0)
            return source.TryGetAtRank(_position - 1, out entry);
        entry = default;
        return false;
    }

    /// <summary>Attempts to read the entry immediately after the gap.</summary>
    public bool TryPeekNext(out KeyValuePair<TKey, TValue> entry) =>
        Source.TryGetAtRank(_position, out entry);

    /// <summary>Moves the gap one entry toward the start.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> MovePrevious()
    {
        var source = Source;
        if (_position == 0)
            throw new InvalidOperationException("The cursor is already at the start.");
        return new MerkleSearchTreeCursor<TKey, TValue>(source, _position - 1);
    }

    /// <summary>Moves the gap one entry toward the end.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> MoveNext()
    {
        var source = Source;
        if (_position == source.Count)
            throw new InvalidOperationException("The cursor is already at the end.");
        return new MerkleSearchTreeCursor<TKey, TValue>(source, _position + 1);
    }

    /// <summary>Moves the gap to a rank in <c>0 .. Count</c>.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> Seek(int position)
    {
        var source = Source;
        MerkleCursorGuard.ValidatePosition(position, source.Count);
        return position == _position ? this : new MerkleSearchTreeCursor<TKey, TValue>(source, position);
    }

    /// <summary>Strictly inserts a missing key at its lower-bound gap and returns the gap after it.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> Insert(TKey key, TValue value)
    {
        var source = Source;
        var rank = source.GetLowerBoundRank(key, out var found);
        if (found)
            throw new ArgumentException("The key is already present.", nameof(key));
        EnsureCurrentGap(rank);
        return new MerkleSearchTreeCursor<TKey, TValue>(source.SetItem(key, value), checked(_position + 1));
    }

    /// <summary>
    /// Sets an exact next entry or inserts a missing key at its lower-bound gap. Updates keep the gap
    /// fixed; insertions return the gap after the new entry.
    /// </summary>
    public MerkleSearchTreeCursor<TKey, TValue> SetItem(TKey key, TValue value)
    {
        var source = Source;
        var rank = source.GetLowerBoundRank(key, out var found);
        EnsureCurrentGap(rank);
        var edited = source.SetItem(key, value);
        if (ReferenceEquals(edited, source))
            return this;
        return new MerkleSearchTreeCursor<TKey, TValue>(edited, found ? _position : checked(_position + 1));
    }

    /// <summary>Replaces the next entry's value while retaining its stored key representative.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> SetNextValue(TValue value)
    {
        var source = Source;
        if (!source.TryGetAtRank(_position, out var next))
            throw new InvalidOperationException("No entry follows the cursor gap.");
        var edited = source.SetItem(next.Key, value);
        return ReferenceEquals(edited, source) ? this : new MerkleSearchTreeCursor<TKey, TValue>(edited, _position);
    }

    /// <summary>Deletes the entry before the gap and moves the gap left.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> DeletePrevious()
    {
        var source = Source;
        if (_position == 0 || !source.TryGetAtRank(_position - 1, out var previous))
            throw new InvalidOperationException("No entry precedes the cursor gap.");
        return new MerkleSearchTreeCursor<TKey, TValue>(source.Remove(previous.Key), _position - 1);
    }

    /// <summary>Deletes the entry after the gap and keeps the gap fixed.</summary>
    public MerkleSearchTreeCursor<TKey, TValue> DeleteNext()
    {
        var source = Source;
        if (!source.TryGetAtRank(_position, out var next))
            throw new InvalidOperationException("No entry follows the cursor gap.");
        return new MerkleSearchTreeCursor<TKey, TValue>(source.Remove(next.Key), _position);
    }

    /// <summary>Returns this cursor version's canonical immutable tree.</summary>
    public MerkleSearchTree<TKey, TValue> Snapshot() => Source;

    private MerkleSearchTree<TKey, TValue> Source => _source ?? throw new InvalidOperationException(
        "The default MerkleSearchTreeCursor<TKey, TValue> value is not initialized. Obtain a cursor from MerkleSearchTree<TKey, TValue>.");

    private void EnsureCurrentGap(int rank)
    {
        if (rank != _position)
            throw new InvalidOperationException($"The key belongs at gap {rank}, not at the current gap {_position}.");
    }
}

internal static class MerkleCursorGuard
{
    internal static void ValidatePosition(int position, int count)
    {
        if ((uint)position > (uint)count)
            throw new ArgumentOutOfRangeException(nameof(position), position, "The cursor position must be between zero and Count.");
    }
}
