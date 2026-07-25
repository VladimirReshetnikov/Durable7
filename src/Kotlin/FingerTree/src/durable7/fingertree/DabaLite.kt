package durable7.fingertree

/**
 * Maintains the FIFO-ordered aggregate of a mutable sliding window.
 *
 * This is Tangwongsan, Hirzel, and Schneider's six-cursor DABA Lite algorithm. Each insertion or
 * eviction performs one bounded incremental-reversal fixup; there is no reversal loop. [insert],
 * [evict]/[tryEvict], and [aggregate] invoke [Monoid.combine] at most three, two, and one times,
 * respectively. The complete operations are worst-case O(1) when [Monoid.empty] and
 * [Monoid.combine] are themselves O(1).
 *
 * The type is deliberately mutable and provides no synchronization. Calls on one instance must
 * not overlap unless the caller supplies external serialization. If a monoid callback throws from
 * a mutating operation, this object's published window state remains unchanged; callback side
 * effects outside the object cannot be rolled back.
 *
 * @param monoid the identity and associative FIFO-order combine callbacks
 */
public class DabaLite<T>(private val monoid: Monoid<T>) {
    private val queue: DabaChunkedQueue<T> = DabaChunkedQueue()
    private var f: DabaCursor<T> = queue.begin
    private var l: DabaCursor<T> = f
    private var r: DabaCursor<T> = f
    private var a: DabaCursor<T> = f
    private var b: DabaCursor<T> = f
    private var e: DabaCursor<T> = f
    private var aggregateRA: T = monoid.empty
    private var aggregateB: T = monoid.empty

    /** The number of values in the current window. */
    public var count: Int = 0
        private set

    /** Whether the current window is empty. */
    public val isEmpty: Boolean
        get() = count == 0

    /**
     * The aggregate in FIFO order.
     *
     * An empty query obtains [Monoid.empty] and invokes no combine callback. A nonempty query
     * invokes [Monoid.combine] exactly once.
     */
    public val aggregate: T
        get() = if (count == 0) monoid.empty else monoid.combine(queue.read(f), aggregateB)

    /**
     * Appends [value] to the window.
     *
     * At most three combine callbacks are made. A callback failure leaves the prior window intact.
     *
     * @throws ArithmeticException if the window already contains [Int.MAX_VALUE] values.
     */
    public fun insert(value: T) {
        if (count == Int.MAX_VALUE) {
            throw ArithmeticException("The DABA Lite window cannot contain more than Int.MAX_VALUE items.")
        }

        val oldEnd = e
        val oldCount = count
        val oldAggregateB = aggregateB
        val oldValue = queue.readRaw(oldEnd)
        val oldSuccessor = oldEnd.block.next
        val newAggregateB = monoid.combine(oldAggregateB, value)
        try {
            val newEnd = queue.advanceEnd(oldEnd)
            queue.write(oldEnd, value)
            e = newEnd
            count = oldCount + 1
            aggregateB = newAggregateB
            fixup()
        } catch (error: Throwable) {
            queue.writeRaw(oldEnd, oldValue)
            queue.restoreSuccessor(oldEnd.block, oldSuccessor)
            e = oldEnd
            count = oldCount
            aggregateB = oldAggregateB
            throw error
        }
    }

    /**
     * Removes the oldest value.
     *
     * At most two combine callbacks are made. A callback failure leaves the prior window intact.
     *
     * @throws IllegalStateException if the window is empty.
     */
    public fun evict() {
        if (!tryEvict()) {
            throw IllegalStateException("Cannot evict from an empty DABA Lite window.")
        }
    }

    /**
     * Removes the oldest value when one exists.
     *
     * At most two combine callbacks are made. A callback failure leaves the prior window intact.
     */
    public fun tryEvict(): Boolean {
        if (count == 0) {
            return false
        }

        val oldFront = f
        val oldCount = count
        f = queue.advance(oldFront)
        count = oldCount - 1
        try {
            fixup()
        } catch (error: Throwable) {
            f = oldFront
            count = oldCount
            throw error
        }

        // JVM arrays hold references even for boxed value types, so every retired slot is cleared.
        queue.clearSlot(oldFront)
        queue.trimBefore(f)
        return true
    }

    /**
     * Removes every value in O(1), retaining one fresh 64-slot chunk.
     *
     * An empty clear makes no callback. A nonempty clear obtains [Monoid.empty] once, invokes
     * [Monoid.combine] zero times, and commits only after the identity callback succeeds.
     */
    public fun clear() {
        if (count == 0) {
            return
        }

        val empty = monoid.empty
        val begin = queue.reset()
        f = begin
        l = begin
        r = begin
        a = begin
        b = begin
        e = begin
        aggregateRA = empty
        aggregateB = empty
        count = 0
    }

    /**
     * Validates links, cursor order, DABA region equations, and chunk accounting.
     *
     * Validation is content-blind and invokes neither monoid callback. It takes O(c) time and
     * space for `c` active chunks and returns statistics for the validated representation.
     */
    public fun validateStructure(): DabaLiteStatistics {
        validateCursorIndex(f, "F")
        validateCursorIndex(l, "L")
        validateCursorIndex(r, "R")
        validateCursorIndex(a, "A")
        validateCursorIndex(b, "B")
        validateCursorIndex(e, "E")

        val first = queue.firstBlock
        if (first.previous != null) {
            invalid("The first DABA Lite chunk has a predecessor.")
        }
        if (first !== f.block) {
            invalid("The DABA Lite front cursor is not in the first active chunk.")
        }

        val seen = HashSet<DabaBlock<T>>()
        var previous: DabaBlock<T>? = null
        var block = first
        var blockBase = 0L
        var blockCount = 0
        var fPosition = -1L
        var lPosition = -1L
        var rPosition = -1L
        var aPosition = -1L
        var bPosition = -1L
        var ePosition = -1L

        while (true) {
            if (!seen.add(block)) {
                invalid("The DABA Lite chunk chain contains a cycle.")
            }
            if (block.previous !== previous) {
                invalid("A DABA Lite chunk has an invalid predecessor link.")
            }
            if (previous != null && previous.next !== block) {
                invalid("A DABA Lite chunk has an invalid successor link.")
            }

            if (block === f.block) fPosition = blockBase + f.index
            if (block === l.block) lPosition = blockBase + l.index
            if (block === r.block) rPosition = blockBase + r.index
            if (block === a.block) aPosition = blockBase + a.index
            if (block === b.block) bPosition = blockBase + b.index
            if (block === e.block) ePosition = blockBase + e.index
            blockCount++

            if (block === e.block) {
                if (block.next != null) {
                    invalid("The DABA Lite end cursor is not in the last active chunk.")
                }
                break
            }

            previous = block
            block = block.next ?: invalid("The DABA Lite end cursor is not reachable from the front.")
            blockBase = Math.addExact(blockBase, DABA_CHUNK_CAPACITY.toLong())
        }

        if (fPosition < 0 || lPosition < 0 || rPosition < 0 ||
            aPosition < 0 || bPosition < 0 || ePosition < 0
        ) {
            invalid("A DABA Lite cursor is outside the active chunk chain.")
        }
        if (!(fPosition <= lPosition && lPosition <= rPosition && rPosition <= aPosition &&
                aPosition <= bPosition && bPosition <= ePosition)
        ) {
            invalid("DABA Lite cursors do not satisfy F <= L <= R <= A <= B <= E.")
        }
        if (ePosition - fPosition != count.toLong()) {
            invalid("The DABA Lite count disagrees with the F-to-E cursor distance.")
        }

        val frontLength = bPosition - fPosition
        val backLength = ePosition - bPosition
        val leftLength = rPosition - lPosition
        val rightLength = aPosition - rPosition
        val accumulatorLength = bPosition - aPosition
        if (count == 0) {
            if (!(same(f, l) && same(l, r) && same(r, a) && same(a, b) && same(b, e))) {
                invalid("An empty DABA Lite window does not have coincident cursors.")
            }
        } else {
            if (leftLength != rightLength) {
                invalid("The DABA Lite left and right work regions have different sizes.")
            }
            if (leftLength + rightLength + accumulatorLength + 1 != frontLength - backLength) {
                invalid("The DABA Lite incremental-reversal size equation is invalid.")
            }
            if (lPosition - fPosition != backLength + 1) {
                invalid("The DABA Lite processed-prefix distance is invalid.")
            }
        }

        val expectedBlockCount = (f.index.toLong() + count) / DABA_CHUNK_CAPACITY + 1
        if (blockCount.toLong() != expectedBlockCount) {
            invalid("The DABA Lite active chunk count is invalid.")
        }
        val allocatedSlotCapacity = Math.multiplyExact(blockCount.toLong(), DABA_CHUNK_CAPACITY.toLong())
        val slackSlotCount = allocatedSlotCapacity - count
        if (slackSlotCount !in 1L until (2L * DABA_CHUNK_CAPACITY)) {
            invalid("The DABA Lite chunk slack exceeds its two-chunk bound.")
        }

        return DabaLiteStatistics(
            count = count,
            frontLength = frontLength.toInt(),
            backLength = backLength.toInt(),
            leftLength = leftLength.toInt(),
            rightLength = rightLength.toInt(),
            accumulatorLength = accumulatorLength.toInt(),
            blockCount = blockCount,
            allocatedSlotCapacity = allocatedSlotCapacity,
            slackSlotCount = slackSlotCount,
        )
    }

    /** The current first chunk identity, used only by representation-lifetime tests. */
    internal val firstChunkIdentityForTesting: Any
        get() = queue.firstBlock

    private fun fixup() {
        var nextL = l
        val nextR = r
        var nextA = a
        var nextB = b
        var nextAggregateRA = aggregateRA
        var nextAggregateB = aggregateB
        var cachedEmpty: T? = null
        var hasCachedEmpty = false

        if (same(f, nextB)) {
            val empty = monoid.empty
            publishFixup(e, e, e, e, empty, empty)
            return
        }

        if (same(nextL, nextB)) {
            val empty = monoid.empty
            cachedEmpty = empty
            hasCachedEmpty = true
            nextL = f
            nextA = e
            nextB = e
            nextAggregateRA = nextAggregateB
            nextAggregateB = empty
        }

        if (same(nextL, nextR)) {
            val empty = if (hasCachedEmpty) cachedEmptyValue(cachedEmpty) else monoid.empty
            publishFixup(
                queue.advance(nextL),
                queue.advance(nextR),
                queue.advance(nextA),
                nextB,
                empty,
                nextAggregateB,
            )
            return
        }

        val advancedLeft = queue.advance(nextL)
        val beforeA = queue.retreat(nextA)
        val newLeftValue = monoid.combine(queue.read(nextL), nextAggregateRA)
        val productA = if (same(nextA, nextB)) monoid.empty else queue.read(nextA)
        val newAccumulatorValue = monoid.combine(queue.read(beforeA), productA)

        // No callback follows these writes, so callback failure cannot expose a partial rewrite.
        queue.write(nextL, newLeftValue)
        queue.write(beforeA, newAccumulatorValue)
        publishFixup(advancedLeft, nextR, beforeA, nextB, nextAggregateRA, nextAggregateB)
    }

    private fun publishFixup(
        nextL: DabaCursor<T>,
        nextR: DabaCursor<T>,
        nextA: DabaCursor<T>,
        nextB: DabaCursor<T>,
        nextAggregateRA: T,
        nextAggregateB: T,
    ) {
        l = nextL
        r = nextR
        a = nextA
        b = nextB
        aggregateRA = nextAggregateRA
        aggregateB = nextAggregateB
    }

    private fun validateCursorIndex(cursor: DabaCursor<T>, name: String) {
        if (cursor.index !in 0 until DABA_CHUNK_CAPACITY) {
            invalid("The DABA Lite $name cursor has an invalid chunk index.")
        }
    }

    @Suppress("UNCHECKED_CAST")
    private fun cachedEmptyValue(value: T?): T = value as T

    private fun invalid(message: String): Nothing = throw IllegalStateException(message)
}

/**
 * Statistics returned after a successful [DabaLite.validateStructure] audit.
 *
 * @property count number of values in the FIFO window
 * @property frontLength distance from F to B
 * @property backLength distance from B to E
 * @property leftLength distance from L to R
 * @property rightLength distance from R to A
 * @property accumulatorLength distance from A to B
 * @property blockCount number of active fixed-size chunks
 * @property allocatedSlotCapacity total slots in the active chunks
 * @property slackSlotCount allocated slots not occupied by window values
 */
public data class DabaLiteStatistics(
    public val count: Int,
    public val frontLength: Int,
    public val backLength: Int,
    public val leftLength: Int,
    public val rightLength: Int,
    public val accumulatorLength: Int,
    public val blockCount: Int,
    public val allocatedSlotCapacity: Long,
    public val slackSlotCount: Long,
)

private const val DABA_CHUNK_CAPACITY: Int = 64

private class DabaBlock<T> {
    val items: Array<Any?> = arrayOfNulls(DABA_CHUNK_CAPACITY)
    var previous: DabaBlock<T>? = null
    var next: DabaBlock<T>? = null
}

private data class DabaCursor<T>(val block: DabaBlock<T>, val index: Int)

private fun <T> same(left: DabaCursor<T>, right: DabaCursor<T>): Boolean =
    left.block === right.block && left.index == right.index

private class DabaChunkedQueue<T> {
    private var first: DabaBlock<T> = DabaBlock()

    val begin: DabaCursor<T>
        get() = DabaCursor(first, 0)

    val firstBlock: DabaBlock<T>
        get() = first

    @Suppress("UNCHECKED_CAST")
    fun read(cursor: DabaCursor<T>): T = cursor.block.items[cursor.index] as T

    fun readRaw(cursor: DabaCursor<T>): Any? = cursor.block.items[cursor.index]

    fun write(cursor: DabaCursor<T>, value: T) {
        cursor.block.items[cursor.index] = value
    }

    fun writeRaw(cursor: DabaCursor<T>, value: Any?) {
        cursor.block.items[cursor.index] = value
    }

    fun clearSlot(cursor: DabaCursor<T>) {
        cursor.block.items[cursor.index] = null
    }

    fun reset(): DabaCursor<T> {
        val replacement = DabaBlock<T>()
        first = replacement
        return DabaCursor(replacement, 0)
    }

    fun advance(cursor: DabaCursor<T>): DabaCursor<T> {
        if (cursor.index + 1 < DABA_CHUNK_CAPACITY) {
            return DabaCursor(cursor.block, cursor.index + 1)
        }
        return DabaCursor(
            cursor.block.next ?: throw IllegalStateException("Cannot advance beyond the DABA Lite end chunk."),
            0,
        )
    }

    fun advanceEnd(cursor: DabaCursor<T>): DabaCursor<T> {
        if (cursor.index + 1 < DABA_CHUNK_CAPACITY) {
            return DabaCursor(cursor.block, cursor.index + 1)
        }

        var next = cursor.block.next
        if (next == null) {
            next = DabaBlock()
            next.previous = cursor.block
            cursor.block.next = next
        }
        return DabaCursor(next, 0)
    }

    fun retreat(cursor: DabaCursor<T>): DabaCursor<T> {
        if (cursor.index != 0) {
            return DabaCursor(cursor.block, cursor.index - 1)
        }
        return DabaCursor(
            cursor.block.previous ?: throw IllegalStateException("Cannot retreat before the DABA Lite front chunk."),
            DABA_CHUNK_CAPACITY - 1,
        )
    }

    fun restoreSuccessor(block: DabaBlock<T>, successor: DabaBlock<T>?) {
        val current = block.next
        if (current === successor) {
            return
        }
        block.next = successor
        successor?.previous = block
        current?.previous = null
    }

    fun trimBefore(front: DabaCursor<T>) {
        if (first === front.block) {
            return
        }
        val predecessor = front.block.previous
        first = front.block
        first.previous = null
        predecessor?.next = null
    }
}
