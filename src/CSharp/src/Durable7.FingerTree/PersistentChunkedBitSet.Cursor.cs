namespace Durable7.FingerTree;

public sealed partial class PersistentChunkedBitSet
{
    /// <summary>Creates an immutable set-bit gap cursor at a population rank in <c>0..Count</c>.</summary>
    public PersistentChunkedBitSetCursor GetCursor(long position = 0) => new(this, position);

    /// <summary>Creates a cursor before the first set bit at or after the supplied index.</summary>
    public PersistentChunkedBitSetCursor GetCursorAtOrAfter(int bitIndex)
    {
        var position = bitIndex <= 0 ? 0 : Rank(bitIndex - 1);
        return new(this, position);
    }

    /// <summary>Tries to locate a set bit; a miss returns the usable at-or-after cursor.</summary>
    public bool TryGetCursor(int bitIndex, out PersistentChunkedBitSetCursor cursor)
    {
        cursor = GetCursorAtOrAfter(bitIndex);
        return bitIndex >= 0
            && cursor.TryPeekNext(out var candidate)
            && candidate == bitIndex;
    }
}

/// <summary>Immutable root-plus-population-rank cursor over the present bits of a sparse bit set.</summary>
public readonly struct PersistentChunkedBitSetCursor
{
    private readonly PersistentChunkedBitSet? _snapshot;
    private readonly long _position;

    internal PersistentChunkedBitSetCursor(PersistentChunkedBitSet snapshot, long position)
    {
        if (position < 0 || position > snapshot.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        _snapshot = snapshot;
        _position = position;
    }

    private PersistentChunkedBitSet Value => _snapshot ?? throw UninitializedError();

    /// <summary>Gets the set-bit count in this cursor version.</summary>
    public long Count => Value.Count;
    /// <summary>Gets the number of set bits before the gap.</summary>
    public long Position
    {
        get
        {
            _ = Value;
            return _position;
        }
    }
    /// <summary>Gets whether the gap is before every set bit.</summary>
    public bool IsAtStart => Position == 0;
    /// <summary>Gets whether the gap is after every set bit.</summary>
    public bool IsAtEnd => Position == Count;

    /// <summary>Attempts to read the set bit immediately before the gap.</summary>
    public bool TryPeekPrevious(out int bitIndex)
    {
        if (Position == 0) { bitIndex = default; return false; }
        bitIndex = Value.Select(Position - 1);
        return true;
    }

    /// <summary>Attempts to read the set bit immediately after the gap.</summary>
    public bool TryPeekNext(out int bitIndex)
    {
        if (Position == Count) { bitIndex = default; return false; }
        bitIndex = Value.Select(Position);
        return true;
    }

    /// <summary>Moves the gap one set bit toward the start.</summary>
    public PersistentChunkedBitSetCursor MovePrevious() => Position == 0
        ? throw new InvalidOperationException("The chunked-bit-set cursor is already at the start.")
        : new(Value, Position - 1);

    /// <summary>Moves the gap one set bit toward the end.</summary>
    public PersistentChunkedBitSetCursor MoveNext() => Position == Count
        ? throw new InvalidOperationException("The chunked-bit-set cursor is already at the end.")
        : new(Value, Position + 1);

    /// <summary>Moves the gap to an absolute population rank.</summary>
    public PersistentChunkedBitSetCursor SeekRank(long position) =>
        position == Position ? this : new(Value, position);

    /// <summary>Adds a missing bit and returns its following gap; a present bit preserves this cursor.</summary>
    public PersistentChunkedBitSetCursor Add(int bitIndex)
    {
        var snapshot = Value.Add(bitIndex);
        if (ReferenceEquals(snapshot, Value))
            return this;
        var position = bitIndex == 0 ? 0 : Value.Rank(bitIndex - 1);
        return new(snapshot, checked(position + 1));
    }

    /// <summary>Deletes the set bit immediately after the gap.</summary>
    public PersistentChunkedBitSetCursor DeleteNext()
    {
        if (!TryPeekNext(out var bitIndex))
            throw new InvalidOperationException("The chunked-bit-set cursor has no next bit.");
        return new(Value.Remove(bitIndex), Position);
    }

    /// <summary>Deletes the set bit immediately before the gap.</summary>
    public PersistentChunkedBitSetCursor DeletePrevious()
    {
        if (!TryPeekPrevious(out var bitIndex))
            throw new InvalidOperationException("The chunked-bit-set cursor has no previous bit.");
        return new(Value.Remove(bitIndex), Position - 1);
    }

    /// <summary>Returns the exact persistent bit-set version represented by this cursor.</summary>
    public PersistentChunkedBitSet Snapshot() => Value;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized chunked-bit-set cursor.");
}
