using System.Collections;
using System.Diagnostics;
using System.Numerics;

namespace Tools.DataStructures.FingerTree;

internal readonly record struct BitSetChunk(int WordIndex, ulong Bits);

internal readonly record struct BitSetAnnotation(
    int ChunkCount,
    long PopCount,
    Optional<int> LastWordIndex);

internal readonly struct BitSetMeasure : IMeasure<BitSetChunk, BitSetAnnotation>
{
    public static BitSetAnnotation Empty => new(0, 0, Optional<int>.None);

    public static BitSetAnnotation Measure(BitSetChunk element) =>
        new(1, BitOperations.PopCount(element.Bits), Optional<int>.Some(element.WordIndex));

    public static BitSetAnnotation Combine(BitSetAnnotation left, BitSetAnnotation right) =>
        new(
            checked(left.ChunkCount + right.ChunkCount),
            checked(left.PopCount + right.PopCount),
            right.LastWordIndex.HasValue ? right.LastWordIndex : left.LastWordIndex);
}

/// <summary>
/// Represents an immutable sparse bit set stored as ordered nonzero 64-bit chunks with cached population counts.
/// </summary>
/// <remarks>
/// The domain is every nonnegative <see cref="int"/> value. Point edits and membership, inclusive
/// rank, and zero-based select are logarithmic in the number of nonzero chunks. Enumeration is
/// ascending. Set algebra merges chunk streams and therefore runs in linear time in the represented
/// chunk counts rather than in the largest bit index.
/// </remarks>
[DebuggerDisplay("Count = {Count}, ChunkCount = {ChunkCount}")]
public sealed class PersistentChunkedBitSet : IEnumerable<int>
{
    private static readonly PersistentChunkedBitSet EmptyInstance = new(
        FingerTree<BitSetChunk, BitSetAnnotation, BitSetMeasure>.Empty);

    private readonly FingerTree<BitSetChunk, BitSetAnnotation, BitSetMeasure> _chunks;

    private PersistentChunkedBitSet(
        FingerTree<BitSetChunk, BitSetAnnotation, BitSetMeasure> chunks) =>
        _chunks = chunks;

    private enum SetOperation
    {
        Union,
        Intersect,
        Except,
        SymmetricExcept,
    }

    private readonly struct WordAtLeastPredicate(int wordIndex) : IMeasurePredicate<BitSetAnnotation>
    {
        public bool Invoke(BitSetAnnotation measure) =>
            measure.LastWordIndex.HasValue && measure.LastWordIndex.Value >= wordIndex;
    }

    private readonly struct PopCountAbovePredicate(long rank) : IMeasurePredicate<BitSetAnnotation>
    {
        public bool Invoke(BitSetAnnotation measure) => measure.PopCount > rank;
    }

    /// <summary>Gets the empty bit set.</summary>
    public static PersistentChunkedBitSet Empty => EmptyInstance;

    /// <summary>Gets the number of set bits.</summary>
    public long Count => _chunks.Measure.PopCount;

    /// <summary>Gets the number of represented nonzero 64-bit chunks.</summary>
    public int ChunkCount => _chunks.Measure.ChunkCount;

    /// <summary>Gets whether no bits are set.</summary>
    public bool IsEmpty => _chunks.IsEmpty;

    /// <summary>Creates a bit set from nonnegative bit indexes.</summary>
    /// <param name="bitIndexes">The bit indexes to set.</param>
    /// <returns>A bit set containing the distinct indexes.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="bitIndexes"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentOutOfRangeException">An index is negative.</exception>
    public static PersistentChunkedBitSet CreateRange(IEnumerable<int> bitIndexes)
    {
        ArgumentNullException.ThrowIfNull(bitIndexes);
        var words = new System.Collections.Generic.SortedDictionary<int, ulong>();
        foreach (var bitIndex in bitIndexes)
        {
            ValidateBitIndex(bitIndex);
            var wordIndex = bitIndex >> 6;
            var bit = 1UL << (bitIndex & 63);
            words.TryGetValue(wordIndex, out var bits);
            words[wordIndex] = bits | bit;
        }

        if (words.Count == 0)
            return Empty;

        var chunks = words.Select(pair => new BitSetChunk(pair.Key, pair.Value));
        return new(FingerTree<BitSetChunk, BitSetAnnotation, BitSetMeasure>.CreateRange(chunks));
    }

    /// <summary>Determines whether a bit index is set.</summary>
    /// <param name="bitIndex">The bit index to test.</param>
    /// <returns><see langword="false"/> for a negative or clear index; otherwise, <see langword="true"/>.</returns>
    public bool Contains(int bitIndex)
    {
        if (bitIndex < 0)
            return false;

        var wordIndex = bitIndex >> 6;
        return _chunks.TryLocate(
                new WordAtLeastPredicate(wordIndex),
                out _,
                out var chunk)
            && chunk.WordIndex == wordIndex
            && (chunk.Bits & (1UL << (bitIndex & 63))) != 0;
    }

    /// <summary>Adds a bit index.</summary>
    /// <param name="bitIndex">The nonnegative bit index to set.</param>
    /// <returns>This set when already present; otherwise, a set containing the bit.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="bitIndex"/> is negative.</exception>
    public PersistentChunkedBitSet Add(int bitIndex)
    {
        ValidateBitIndex(bitIndex);
        var wordIndex = bitIndex >> 6;
        var bit = 1UL << (bitIndex & 63);
        var (left, right) = _chunks.Split(new WordAtLeastPredicate(wordIndex));
        if (right.TryViewLeft(out var found, out var tail)
            && found.WordIndex == wordIndex)
        {
            var updatedBits = found.Bits | bit;
            if (updatedBits == found.Bits)
                return this;

            return new(left.Append(new(wordIndex, updatedBits)).Concat(tail));
        }

        return new(left.Append(new(wordIndex, bit)).Concat(right));
    }

    /// <summary>Attempts to add a bit index.</summary>
    /// <param name="bitIndex">The nonnegative bit index to set.</param>
    /// <param name="result">The resulting set, or this set when the bit was already set.</param>
    /// <returns><see langword="true"/> when the bit changed from clear to set.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="bitIndex"/> is negative.</exception>
    public bool TryAdd(int bitIndex, out PersistentChunkedBitSet result)
    {
        result = Add(bitIndex);
        return !ReferenceEquals(result, this);
    }

    /// <summary>Removes a bit index.</summary>
    /// <param name="bitIndex">The bit index to clear.</param>
    /// <returns>This set when negative or already clear; otherwise, a set without the bit.</returns>
    public PersistentChunkedBitSet Remove(int bitIndex)
    {
        if (bitIndex < 0)
            return this;

        var wordIndex = bitIndex >> 6;
        var bit = 1UL << (bitIndex & 63);
        var (left, right) = _chunks.Split(new WordAtLeastPredicate(wordIndex));
        if (!right.TryViewLeft(out var found, out var tail)
            || found.WordIndex != wordIndex
            || (found.Bits & bit) == 0)
        {
            return this;
        }

        var updatedBits = found.Bits & ~bit;
        return updatedBits == 0
            ? new(left.Concat(tail))
            : new(left.Append(new(wordIndex, updatedBits)).Concat(tail));
    }

    /// <summary>Attempts to remove a bit index.</summary>
    /// <param name="bitIndex">The bit index to clear.</param>
    /// <param name="result">The resulting set, or this set when the bit was clear.</param>
    /// <returns><see langword="true"/> when the bit changed from set to clear.</returns>
    public bool TryRemove(int bitIndex, out PersistentChunkedBitSet result)
    {
        result = Remove(bitIndex);
        return !ReferenceEquals(result, this);
    }

    /// <summary>Counts set bits whose indexes are less than or equal to a position.</summary>
    /// <param name="bitIndex">The inclusive upper bit index.</param>
    /// <returns>Zero for a negative position; otherwise, the inclusive rank.</returns>
    public long Rank(int bitIndex)
    {
        if (bitIndex < 0)
            return 0;

        var wordIndex = bitIndex >> 6;
        if (!_chunks.TryLocate(
                new WordAtLeastPredicate(wordIndex),
                out var before,
                out var chunk)
            || chunk.WordIndex != wordIndex)
        {
            return before.PopCount;
        }

        var offset = bitIndex & 63;
        var mask = offset == 63 ? ulong.MaxValue : (1UL << (offset + 1)) - 1;
        return checked(before.PopCount + BitOperations.PopCount(chunk.Bits & mask));
    }

    /// <summary>Gets the bit index at a zero-based population rank.</summary>
    /// <param name="rank">The zero-based rank among set bits.</param>
    /// <returns>The selected bit index.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="rank"/> is outside <c>[0, Count)</c>.</exception>
    public int Select(long rank) =>
        TrySelect(rank, out var bitIndex)
            ? bitIndex
            : throw new ArgumentOutOfRangeException(nameof(rank));

    /// <summary>Tries to get the bit index at a zero-based population rank.</summary>
    /// <param name="rank">The zero-based rank among set bits.</param>
    /// <param name="bitIndex">The selected bit index when the rank is valid.</param>
    /// <returns><see langword="true"/> when <paramref name="rank"/> is in <c>[0, Count)</c>.</returns>
    public bool TrySelect(long rank, out int bitIndex)
    {
        if (rank < 0 || rank >= Count
            || !_chunks.TryLocate(
                new PopCountAbovePredicate(rank),
                out var before,
                out var chunk))
        {
            bitIndex = default;
            return false;
        }

        var localRank = checked((int)(rank - before.PopCount));
        var bits = chunk.Bits;
        for (var index = 0; index < localRank; index++)
            bits &= bits - 1;

        var offset = BitOperations.TrailingZeroCount(bits);
        bitIndex = checked((int)(((long)chunk.WordIndex << 6) + offset));
        return true;
    }

    /// <summary>Returns the union with another bit set.</summary>
    /// <param name="other">The other bit set.</param>
    /// <returns>A set containing bits present in either operand.</returns>
    public PersistentChunkedBitSet Union(PersistentChunkedBitSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return ReferenceEquals(this, other) ? this : Combine(other, SetOperation.Union);
    }

    /// <summary>Returns the intersection with another bit set.</summary>
    /// <param name="other">The other bit set.</param>
    /// <returns>A set containing bits present in both operands.</returns>
    public PersistentChunkedBitSet Intersect(PersistentChunkedBitSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return ReferenceEquals(this, other) ? this : Combine(other, SetOperation.Intersect);
    }

    /// <summary>Returns this bit set without bits present in another bit set.</summary>
    /// <param name="other">The bits to remove.</param>
    /// <returns>The set difference.</returns>
    public PersistentChunkedBitSet Except(PersistentChunkedBitSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return ReferenceEquals(this, other) ? Empty : Combine(other, SetOperation.Except);
    }

    /// <summary>Returns bits present in exactly one operand.</summary>
    /// <param name="other">The other bit set.</param>
    /// <returns>The symmetric difference.</returns>
    public PersistentChunkedBitSet SymmetricExcept(PersistentChunkedBitSet other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return ReferenceEquals(this, other) ? Empty : Combine(other, SetOperation.SymmetricExcept);
    }

    /// <summary>Returns the empty bit set.</summary>
    /// <returns>This set when already empty; otherwise, the shared empty set.</returns>
    public PersistentChunkedBitSet Clear() => IsEmpty ? this : Empty;

    /// <summary>Returns an enumerator over set bit indexes in ascending order.</summary>
    /// <returns>An enumerator over the immutable bit-set snapshot.</returns>
    public IEnumerator<int> GetEnumerator() => EnumerateBits().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal void ValidateInvariants()
    {
        _chunks.ValidateInvariants();
        var chunkCount = 0;
        long popCount = 0;
        var previousWord = -1;
        foreach (var chunk in _chunks)
        {
            if (chunk.WordIndex <= previousWord)
                throw new InvalidOperationException("Bit-set word indexes are not strictly increasing.");
            if (chunk.Bits == 0)
                throw new InvalidOperationException("A bit set stores an empty chunk.");

            previousWord = chunk.WordIndex;
            chunkCount = checked(chunkCount + 1);
            popCount = checked(popCount + BitOperations.PopCount(chunk.Bits));
        }

        if (chunkCount != ChunkCount || popCount != Count)
            throw new InvalidOperationException("The bit-set measure disagrees with its chunks.");
    }

    private static void ValidateBitIndex(int bitIndex)
    {
        if (bitIndex < 0)
            throw new ArgumentOutOfRangeException(nameof(bitIndex), "Bit indexes must be nonnegative.");
    }

    private PersistentChunkedBitSet Combine(
        PersistentChunkedBitSet other,
        SetOperation operation)
    {
        var left = _chunks.GetEnumerator();
        var right = other._chunks.GetEnumerator();
        var hasLeft = left.MoveNext();
        var hasRight = right.MoveNext();
        var chunks = new List<BitSetChunk>();

        while (hasLeft || hasRight)
        {
            if (!hasRight || (hasLeft && left.Current.WordIndex < right.Current.WordIndex))
            {
                if (operation is SetOperation.Union or SetOperation.Except or SetOperation.SymmetricExcept)
                    chunks.Add(left.Current);
                hasLeft = left.MoveNext();
                continue;
            }

            if (!hasLeft || right.Current.WordIndex < left.Current.WordIndex)
            {
                if (operation is SetOperation.Union or SetOperation.SymmetricExcept)
                    chunks.Add(right.Current);
                hasRight = right.MoveNext();
                continue;
            }

            var bits = operation switch
            {
                SetOperation.Union => left.Current.Bits | right.Current.Bits,
                SetOperation.Intersect => left.Current.Bits & right.Current.Bits,
                SetOperation.Except => left.Current.Bits & ~right.Current.Bits,
                _ => left.Current.Bits ^ right.Current.Bits,
            };
            if (bits != 0)
                chunks.Add(new(left.Current.WordIndex, bits));
            hasLeft = left.MoveNext();
            hasRight = right.MoveNext();
        }

        if (chunks.Count == 0)
            return Empty;

        var tree = FingerTree<BitSetChunk, BitSetAnnotation, BitSetMeasure>.CreateRange(chunks);
        return ChunksEqual(tree, _chunks) ? this : new(tree);
    }

    private static bool ChunksEqual(
        FingerTree<BitSetChunk, BitSetAnnotation, BitSetMeasure> left,
        FingerTree<BitSetChunk, BitSetAnnotation, BitSetMeasure> right)
    {
        if (left.Measure.ChunkCount != right.Measure.ChunkCount)
            return false;

        var leftEnumerator = left.GetEnumerator();
        var rightEnumerator = right.GetEnumerator();
        while (leftEnumerator.MoveNext())
        {
            if (!rightEnumerator.MoveNext() || leftEnumerator.Current != rightEnumerator.Current)
                return false;
        }

        return !rightEnumerator.MoveNext();
    }

    private IEnumerable<int> EnumerateBits()
    {
        foreach (var chunk in _chunks)
        {
            var bits = chunk.Bits;
            while (bits != 0)
            {
                var offset = BitOperations.TrailingZeroCount(bits);
                yield return checked((int)(((long)chunk.WordIndex << 6) + offset));
                bits &= bits - 1;
            }
        }
    }
}
