/*
 * Persistent sparse bit set stored as measured fixed-width chunks.
 *
 * Only chunks containing a set bit are stored, and the cached population counts are what make point
 * lookup, inclusive rank, and select logarithmic rather than linear in the highest index ever set.
 * Indices are confined to the cross-language nonnegative signed 32-bit domain.
 */
package durable7.fingertree

private data class BitSetChunk(val wordIndex: Int, val bits: Long)

private data class BitSetMeasure(val chunkCount: Int, val popCount: Long, val lastWordIndex: Int?)

private object BitSetMeasurePolicy : MeasurePolicy<BitSetChunk, BitSetMeasure> {
    override val empty: BitSetMeasure = BitSetMeasure(0, 0L, null)
    override fun measure(element: BitSetChunk): BitSetMeasure =
        BitSetMeasure(1, java.lang.Long.bitCount(element.bits).toLong(), element.wordIndex)
    override fun combine(left: BitSetMeasure, right: BitSetMeasure): BitSetMeasure = BitSetMeasure(
        Math.addExact(left.chunkCount, right.chunkCount),
        Math.addExact(left.popCount, right.popCount),
        right.lastWordIndex ?: left.lastWordIndex,
    )
}

/** Chunk and population measurements returned by a successful structural audit. */
public data class PersistentChunkedBitSetStatistics(
    public val count: Long,
    public val chunkCount: Int,
)

/** Immutable sparse set of nonnegative Int bit indexes stored in measured nonzero 64-bit chunks. */
public class PersistentChunkedBitSet private constructor(
    private val chunks: PersistentMeasuredTree<BitSetChunk, BitSetMeasure>,
) : Iterable<Int> {
    public companion object {
        /** An empty bit set. */
        public fun empty(): PersistentChunkedBitSet =
            PersistentChunkedBitSet(PersistentMeasuredTree.empty(BitSetMeasurePolicy))

        /**
         * A bit set with every index in [bitIndexes] set.
         *
         * @throws IllegalArgumentException if any index is negative.
         */
        public fun from(bitIndexes: Iterable<Int>): PersistentChunkedBitSet {
            var result = empty()
            for (bitIndex in bitIndexes) result = result.add(bitIndex)
            return result
        }
    }

    private enum class Operation { UNION, INTERSECT, EXCEPT, SYMMETRIC_EXCEPT }

    /**
     * Number of set bits. O(1), from the cached population count. A [Long], because the domain is the whole
     * nonnegative [Int] range.
     */
    public val count: Long get() = chunks.measure().popCount
    /**
     * Number of stored 64-bit chunks. A shape diagnostic: a set holding a few very distant bits has few chunks,
     * which is what makes the representation sparse.
     */
    public val chunkCount: Int get() = chunks.measure().chunkCount
    /** Whether no bit is set. */
    public val isEmpty: Boolean get() = chunks.isEmpty

    /**
     * Whether [bitIndex] is set. A negative index is simply absent rather than an error, so membership tests need
     * no guard.
     */
    public fun contains(bitIndex: Int): Boolean {
        if (bitIndex < 0) return false
        val wordIndex = bitIndex ushr 6
        val located = locateWord(wordIndex)
        val chunk = located.value
        return located.found && chunk != null && chunk.wordIndex == wordIndex &&
            (chunk.bits and (1L shl (bitIndex and 63))) != 0L
    }

    /**
     * A bit set with [bitIndex] set. Returns the receiver unchanged when it already is.
     *
     * @throws IllegalArgumentException if [bitIndex] is negative. Unlike [contains], adding a negative index is a
     * caller error rather than a no-op, because there is no such bit to represent.
     */
    public fun add(bitIndex: Int): PersistentChunkedBitSet {
        require(bitIndex >= 0) { "Bit index must be nonnegative." }
        val wordIndex = bitIndex ushr 6
        val bit = 1L shl (bitIndex and 63)
        val located = locateWord(wordIndex)
        val found = located.value
        if (located.found && found != null && found.wordIndex == wordIndex) {
            val updated = found.bits or bit
            if (updated == found.bits) return this
            return PersistentChunkedBitSet(checkNotNull(chunks.setItem(located.index, BitSetChunk(wordIndex, updated))))
        }
        return PersistentChunkedBitSet(checkNotNull(chunks.insertAt(located.index, BitSetChunk(wordIndex, bit))))
    }

    /**
     * A bit set with [bitIndex] cleared. Returns the receiver unchanged when it is not set; a negative index is a
     * no-op rather than an error.
     */
    public fun remove(bitIndex: Int): PersistentChunkedBitSet {
        if (bitIndex < 0) return this
        val wordIndex = bitIndex ushr 6
        val bit = 1L shl (bitIndex and 63)
        val located = locateWord(wordIndex)
        val found = located.value
        if (!located.found || found == null || found.wordIndex != wordIndex || (found.bits and bit) == 0L) return this
        val updated = found.bits and bit.inv()
        val next = if (updated == 0L) chunks.removeAt(located.index)
        else chunks.setItem(located.index, BitSetChunk(wordIndex, updated))
        return PersistentChunkedBitSet(checkNotNull(next))
    }

    /** Inclusive population rank. */
    public fun rank(bitIndex: Int): Long {
        if (bitIndex < 0) return 0L
        val wordIndex = bitIndex ushr 6
        val offset = bitIndex and 63
        val located = locateWord(wordIndex)
        val found = located.value
        if (!located.found || found == null || found.wordIndex != wordIndex) return located.measureBefore.popCount
        val mask = if (offset == 63) -1L else (1L shl (offset + 1)) - 1L
        return Math.addExact(located.measureBefore.popCount, java.lang.Long.bitCount(found.bits and mask).toLong())
    }

    /** Selects the bit at a zero-based population rank, or null when the rank is outside the set. */
    public fun select(rank: Long): Int? {
        if (rank < 0 || rank >= count) return null
        val located = chunks.locate { it.popCount > rank }
        val chunk = checkNotNull(located.value)
        var bits = chunk.bits
        repeat(Math.toIntExact(rank - located.measureBefore.popCount)) { bits = bits and (bits - 1L) }
        return Math.addExact(Math.multiplyExact(chunk.wordIndex, 64), java.lang.Long.numberOfTrailingZeros(bits))
    }

    /** The bits set in either set. */
    public fun union(other: PersistentChunkedBitSet): PersistentChunkedBitSet = combine(other, Operation.UNION)
    /** The bits set in both sets. */
    public fun intersect(other: PersistentChunkedBitSet): PersistentChunkedBitSet = combine(other, Operation.INTERSECT)
    /** The bits set in this one and not in [other]. */
    public fun except(other: PersistentChunkedBitSet): PersistentChunkedBitSet = combine(other, Operation.EXCEPT)
    /** The bits set in exactly one of the two sets. */
    public fun symmetricExcept(other: PersistentChunkedBitSet): PersistentChunkedBitSet =
        combine(other, Operation.SYMMETRIC_EXCEPT)
    /** Whether every bit set here is also set in [other]. */
    public fun isSubsetOf(other: PersistentChunkedBitSet): Boolean = except(other).isEmpty
    /** Whether the two sets share at least one set bit. */
    public fun overlaps(other: PersistentChunkedBitSet): Boolean = !intersect(other).isEmpty
    /** Whether the two sets have exactly the same bits set. */
    public fun setEquals(other: PersistentChunkedBitSet): Boolean = chunks.toList() == other.chunks.toList()
    /** The set bit indexes in ascending order. */
    public fun toList(): List<Int> = iterator().asSequence().toList()

    /**
     * Walk the whole set and check its invariants, returning its shape measurements.
     *
     * A defensive audit for tests and for data arriving from outside the process, not a routine operation: it
     * visits every chunk.
     *
     * @throws IllegalStateException on the first violation found.
     */
    public fun validateStructure(): PersistentChunkedBitSetStatistics {
        check(chunks.isBalanced()) { "PersistentChunkedBitSet measured tree is unbalanced." }
        var previous = -1
        var counted = 0L
        for (chunk in chunks) {
            check(chunk.bits != 0L && chunk.wordIndex > previous) { "Bit-set chunks are zero or out of order." }
            previous = chunk.wordIndex
            counted = Math.addExact(counted, java.lang.Long.bitCount(chunk.bits).toLong())
        }
        check(counted == count && chunks.size == chunkCount) { "Bit-set cached measures disagree." }
        return PersistentChunkedBitSetStatistics(count, chunkCount)
    }

    override fun iterator(): Iterator<Int> = sequence {
        for (chunk in chunks) {
            var bits = chunk.bits
            while (bits != 0L) {
                val offset = java.lang.Long.numberOfTrailingZeros(bits)
                yield(Math.addExact(Math.multiplyExact(chunk.wordIndex, 64), offset))
                bits = bits and (bits - 1L)
            }
        }
    }.iterator()

    private fun locateWord(wordIndex: Int): PersistentMeasuredTree.Locate<BitSetChunk, BitSetMeasure> =
        chunks.locate { measure -> measure.lastWordIndex?.let { it >= wordIndex } == true }

    private fun combine(other: PersistentChunkedBitSet, operation: Operation): PersistentChunkedBitSet {
        if (this === other) return when (operation) {
            Operation.UNION, Operation.INTERSECT -> this
            Operation.EXCEPT, Operation.SYMMETRIC_EXCEPT -> empty()
        }
        val left = chunks.toList()
        val right = other.chunks.toList()
        val result = ArrayList<BitSetChunk>()
        var leftIndex = 0
        var rightIndex = 0
        while (leftIndex < left.size || rightIndex < right.size) {
            val leftChunk = left.getOrNull(leftIndex)
            val rightChunk = right.getOrNull(rightIndex)
            when {
                rightChunk == null || (leftChunk != null && leftChunk.wordIndex < rightChunk.wordIndex) -> {
                    if (operation == Operation.UNION || operation == Operation.EXCEPT || operation == Operation.SYMMETRIC_EXCEPT) {
                        result.add(checkNotNull(leftChunk))
                    }
                    leftIndex++
                }
                leftChunk == null || rightChunk.wordIndex < leftChunk.wordIndex -> {
                    if (operation == Operation.UNION || operation == Operation.SYMMETRIC_EXCEPT) result.add(rightChunk)
                    rightIndex++
                }
                else -> {
                    val bits = when (operation) {
                        Operation.UNION -> leftChunk.bits or rightChunk.bits
                        Operation.INTERSECT -> leftChunk.bits and rightChunk.bits
                        Operation.EXCEPT -> leftChunk.bits and rightChunk.bits.inv()
                        Operation.SYMMETRIC_EXCEPT -> leftChunk.bits xor rightChunk.bits
                    }
                    if (bits != 0L) result.add(BitSetChunk(leftChunk.wordIndex, bits))
                    leftIndex++
                    rightIndex++
                }
            }
        }
        if (result == left) return this
        if (result == right) return other
        return PersistentChunkedBitSet(PersistentMeasuredTree.from(result, BitSetMeasurePolicy))
    }
}
