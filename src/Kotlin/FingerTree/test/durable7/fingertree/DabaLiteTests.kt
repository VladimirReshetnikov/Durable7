package durable7.fingertree

import java.lang.ref.Reference
import java.lang.ref.WeakReference
import java.util.ArrayDeque
import java.util.Random

private const val OFFSET_IDENTITY: Int = 11

private fun dabaCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> dabaCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> dabaCheckThrows(message: String, action: () -> Unit) {
    try {
        action()
    } catch (error: Throwable) {
        if (error is T) {
            return
        }
        throw AssertionError("$message Expected ${T::class.simpleName}, got ${error::class.simpleName}.", error)
    }
    throw AssertionError("$message Expected ${T::class.simpleName}.")
}

private fun dabaExhaustiveShortHistoriesMatchNoncommutativeModel() {
    val left = Matrix.create(2)
    val right = Matrix.create(7)
    dabaCheck(Matrix.multiply(left, right) != Matrix.multiply(right, left), "matrix monoid must be noncommutative")

    val historyLength = 10
    for (mask in 0 until (1 shl historyLength)) {
        val daba = DabaLite(MatrixMonoid)
        val model = ArrayDeque<Matrix>()
        for (step in 0 until historyLength) {
            if ((mask and (1 shl step)) != 0) {
                val value = Matrix.create(mask * 17 + step + 1)
                daba.insert(value)
                model.addLast(value)
            } else if (model.isEmpty()) {
                dabaCheck(!daba.tryEvict(), "empty history eviction")
            } else {
                dabaCheck(daba.tryEvict(), "nonempty history eviction")
                model.removeFirst()
            }

            assertMatrixState(daba, model)
        }
    }
}

private fun dabaRandomizedHistoryMatchesNaiveModel() {
    val empty = DabaLite(CountingOffsetMonoid)
    CountingOffsetMonoid.reset()
    dabaCheck(empty.isEmpty, "new window is empty")
    dabaCheckEquals(OFFSET_IDENTITY, empty.aggregate, "empty aggregate identity")
    dabaCheckEquals(0, CountingOffsetMonoid.combineCount, "empty aggregate combine-free")
    dabaCheckEquals(1, CountingOffsetMonoid.emptyCount, "empty aggregate identity callback")
    CountingOffsetMonoid.reset()
    dabaCheck(!empty.tryEvict(), "empty tryEvict")
    dabaCheckThrows<IllegalStateException>("empty evict") { empty.evict() }
    dabaCheckEquals(0, CountingOffsetMonoid.combineCount, "empty eviction combine-free")
    dabaCheckEquals(0, CountingOffsetMonoid.emptyCount, "empty eviction identity-free")

    val random = Random(20260715L)
    val daba = DabaLite(LongSumMonoid)
    val model = ArrayDeque<Long>()
    repeat(100_000) {
        if (model.isEmpty() || random.nextBoolean()) {
            val value = random.nextInt(20_001).toLong() - 10_000L
            daba.insert(value)
            model.addLast(value)
        } else {
            dabaCheck(daba.tryEvict(), "random nonempty eviction")
            model.removeFirst()
        }
        dabaCheckEquals(model.sum(), daba.aggregate, "random aggregate")
        dabaCheckEquals(model.size, daba.count, "random count")
    }

    val measuredMonoid = DabaLite(IntSumMeasure)
    measuredMonoid.insert(5)
    measuredMonoid.insert(8)
    measuredMonoid.insert(13)
    dabaCheckEquals(26, measuredMonoid.aggregate, "MeasurePolicy is directly reusable as a monoid")

    val nullable = DabaLite(FirstNonNullMonoid)
    nullable.insert(null)
    nullable.insert("retained")
    dabaCheckEquals("retained", nullable.aggregate, "nullable identity")
    nullable.evict()
    dabaCheckEquals("retained", nullable.aggregate, "nullable identity after eviction")
}

private fun dabaChunkBoundariesAndChurnRemainBounded() {
    for (size in listOf(63, 64, 65, 127, 128, 129)) {
        val daba = DabaLite(MatrixMonoid)
        val model = ArrayDeque<Matrix>()
        repeat(size) { index ->
            val value = Matrix.create(index + 1)
            daba.insert(value)
            model.addLast(value)
        }

        val initial = daba.validateStructure()
        dabaCheckEquals(size / 64 + 1, initial.blockCount, "initial block count at $size")
        dabaCheck(initial.slackSlotCount in 1L..127L, "initial slack at $size")
        assertMatrixState(daba, model)

        repeat(512) { index ->
            daba.evict()
            model.removeFirst()
            val value = Matrix.create(10_000 + size * 1_000 + index)
            daba.insert(value)
            model.addLast(value)
            val statistics = daba.validateStructure()
            dabaCheck(statistics.slackSlotCount in 1L..127L, "churn slack at $size/$index")
            dabaCheck(statistics.blockCount in 1..(size / 64 + 2), "churn blocks at $size/$index")
            if ((index and 15) == 0) {
                dabaCheckEquals(foldMatrices(model), daba.aggregate, "churn aggregate at $size/$index")
            }
        }

        while (model.isNotEmpty()) {
            daba.evict()
            model.removeFirst()
            assertMatrixState(daba, model)
        }

        val empty = daba.validateStructure()
        dabaCheckEquals(1, empty.blockCount, "drained block count at $size")
        dabaCheckEquals(64L, empty.allocatedSlotCapacity, "drained capacity at $size")
    }
}

private fun dabaNonDefaultIdentityCoversEveryFixupAndCallbackCeiling() {
    val daba = DabaLite(CountingOffsetMonoid)
    CountingOffsetMonoid.reset()
    val model = ArrayDeque<Int>()
    val phases = HashSet<FixupPhase>()
    var maximumInsert = 0
    var maximumEvict = 0
    var maximumQuery = 0

    repeat(4) { cycle ->
        repeat(130) { index ->
            phases.add(classifyNextFixup(daba.validateStructure(), evicting = false))
            CountingOffsetMonoid.reset()
            val value = cycle * 1_000 + index + 20
            daba.insert(value)
            model.addLast(value)
            dabaCheck(CountingOffsetMonoid.combineCount in 1..3, "insert combine ceiling")
            maximumInsert = maxOf(maximumInsert, CountingOffsetMonoid.combineCount)
            maximumQuery = assertOffsetState(daba, model, maximumQuery)
        }

        while (model.isNotEmpty()) {
            phases.add(classifyNextFixup(daba.validateStructure(), evicting = true))
            CountingOffsetMonoid.reset()
            daba.evict()
            model.removeFirst()
            dabaCheck(CountingOffsetMonoid.combineCount in 0..2, "evict combine ceiling")
            maximumEvict = maxOf(maximumEvict, CountingOffsetMonoid.combineCount)
            maximumQuery = assertOffsetState(daba, model, maximumQuery)
        }
    }

    dabaCheckEquals(FixupPhase.entries.toSet(), phases, "all four fixup phases")
    dabaCheckEquals(3, maximumInsert, "maximum insert combines")
    dabaCheckEquals(2, maximumEvict, "maximum evict combines")
    dabaCheckEquals(1, maximumQuery, "maximum query combines")
}

private fun dabaEveryReachableCombineFailureIsAtomic() {
    for (inserting in listOf(true, false)) {
        val maximum = if (inserting) 3 else 2
        for (ordinal in 1..maximum) {
            val history = findCombineHistory(inserting, ordinal)
            ThrowingCombineMonoid.reset()
            val replay = replayHistory(history, ThrowingCombineMonoid)
                ?: throw AssertionError("valid combine history")
            val daba = replay.first
            val model = replay.second
            val beforeStatistics = daba.validateStructure()
            val beforeAggregate = foldOffset(model)

            ThrowingCombineMonoid.throwOn(ordinal)
            dabaCheckThrows<CallbackException>("combine failure $inserting/$ordinal") {
                applyCandidate(daba, inserting)
            }
            dabaCheckEquals(ordinal, ThrowingCombineMonoid.combineCount, "throwing combine ordinal")

            ThrowingCombineMonoid.reset()
            dabaCheckEquals(beforeStatistics, daba.validateStructure(), "combine failure statistics")
            dabaCheckEquals(beforeAggregate, daba.aggregate, "combine failure aggregate")
            retryCandidateAndContinue(daba, model, inserting)
        }
    }
}

private fun dabaEveryReachableIdentityFailureAndClearAreAtomic() {
    for (inserting in listOf(true, false)) {
        val maximum = findMaximumEmptyCallbacks(inserting)
        dabaCheck(maximum in 1..2, "reachable empty callback count for inserting=$inserting")
        for (ordinal in 1..maximum) {
            val history = findEmptyHistory(inserting, ordinal)
            ThrowingEmptyMonoid.reset()
            val replay = replayHistory(history, ThrowingEmptyMonoid)
                ?: throw AssertionError("valid identity history")
            val daba = replay.first
            val model = replay.second
            val beforeStatistics = daba.validateStructure()
            val beforeAggregate = foldOffset(model)

            ThrowingEmptyMonoid.throwOn(ordinal)
            dabaCheckThrows<CallbackException>("identity failure $inserting/$ordinal") {
                applyCandidate(daba, inserting)
            }
            dabaCheckEquals(ordinal, ThrowingEmptyMonoid.emptyCount, "throwing identity ordinal")

            ThrowingEmptyMonoid.reset()
            dabaCheckEquals(beforeStatistics, daba.validateStructure(), "identity failure statistics")
            dabaCheckEquals(beforeAggregate, daba.aggregate, "identity failure aggregate")
            retryCandidateAndContinue(daba, model, inserting)
        }
    }

    ThrowingEmptyMonoid.reset()
    val clearTarget = DabaLite(ThrowingEmptyMonoid)
    ThrowingEmptyMonoid.reset()
    clearTarget.insert(23)
    val beforeStatistics = clearTarget.validateStructure()
    val beforeAggregate = clearTarget.aggregate
    ThrowingEmptyMonoid.throwOn(1)
    dabaCheckThrows<CallbackException>("clear identity failure") { clearTarget.clear() }
    ThrowingEmptyMonoid.reset()
    dabaCheckEquals(beforeStatistics, clearTarget.validateStructure(), "clear failure statistics")
    dabaCheckEquals(beforeAggregate, clearTarget.aggregate, "clear failure aggregate")
    clearTarget.clear()
    dabaCheck(clearTarget.isEmpty, "clear retry")
}

private fun dabaInsertBoundaryRollbackUnlinksProvisionalChunk() {
    ThrowingCombineMonoid.reset()
    val daba = DabaLite(ThrowingCombineMonoid)
    val model = ArrayDeque<Int>()
    repeat(63) { index ->
        val value = 20 + index
        daba.insert(value)
        model.addLast(value)
    }

    val beforeStatistics = daba.validateStructure()
    val beforeAggregate = foldOffset(model)
    dabaCheckEquals(1, beforeStatistics.blockCount, "pre-boundary blocks")
    dabaCheckEquals(1L, beforeStatistics.slackSlotCount, "pre-boundary slack")

    ThrowingCombineMonoid.throwOn(2)
    dabaCheckThrows<CallbackException>("boundary callback rollback") { daba.insert(101) }
    dabaCheckEquals(2, ThrowingCombineMonoid.combineCount, "boundary callback ordinal")

    ThrowingCombineMonoid.reset()
    dabaCheckEquals(beforeStatistics, daba.validateStructure(), "boundary rollback statistics")
    dabaCheckEquals(beforeAggregate, daba.aggregate, "boundary rollback aggregate")
    dabaCheckEquals(63, daba.count, "boundary rollback count")

    daba.insert(101)
    model.addLast(101)
    val retried = daba.validateStructure()
    dabaCheckEquals(64, retried.count, "boundary retry count")
    dabaCheckEquals(2, retried.blockCount, "boundary retry blocks")
    dabaCheckEquals(128L, retried.allocatedSlotCapacity, "boundary retry capacity")
    dabaCheckEquals(foldOffset(model), daba.aggregate, "boundary retry aggregate")
}

private fun dabaClearReusesOneChunkAndRetiredStorageIsCollectible() {
    val daba = DabaLite(CountingOffsetMonoid)
    CountingOffsetMonoid.reset()
    repeat(257) { daba.insert(it + 20) }

    CountingOffsetMonoid.reset()
    dabaCheckEquals(257, daba.validateStructure().count, "clear setup count")
    dabaCheckEquals(0, CountingOffsetMonoid.combineCount, "validation combine-free")
    dabaCheckEquals(0, CountingOffsetMonoid.emptyCount, "validation identity-free")

    daba.clear()
    dabaCheckEquals(0, CountingOffsetMonoid.combineCount, "clear combine count")
    dabaCheckEquals(1, CountingOffsetMonoid.emptyCount, "clear identity count")
    val statistics = daba.validateStructure()
    dabaCheckEquals(0, statistics.count, "clear count")
    dabaCheckEquals(1, statistics.blockCount, "clear block count")
    dabaCheckEquals(64L, statistics.allocatedSlotCapacity, "clear capacity")
    dabaCheckEquals(64L, statistics.slackSlotCount, "clear slack")

    CountingOffsetMonoid.reset()
    daba.clear()
    dabaCheckEquals(0, CountingOffsetMonoid.combineCount, "empty clear combine count")
    dabaCheckEquals(0, CountingOffsetMonoid.emptyCount, "empty clear identity count")
    daba.insert(20)
    daba.insert(30)
    dabaCheckEquals(39, daba.aggregate, "reuse after clear")

    val retiredScenario = createRetiredChunkScenario()
    collectUntilCleared(retiredScenario.second)
    dabaCheck(retiredScenario.second.get() == null, "retired predecessor chunk must be collectible")
    dabaCheckEquals(2, retiredScenario.first.validateStructure().blockCount, "live aggregator after chunk collection")
    Reference.reachabilityFence(retiredScenario.first)

    val referenceScenario = createPromptReferenceReleaseScenario()
    dabaCheck(!referenceScenario.first.aggregate.isIdentity, "remaining reference aggregate")
    collectUntilCleared(referenceScenario.second)
    dabaCheck(referenceScenario.second.get() == null, "evicted reference must be collectible")
    Reference.reachabilityFence(referenceScenario.first)
}

private fun assertMatrixState(daba: DabaLite<Matrix>, model: Iterable<Matrix>) {
    val expected = if (model is Collection<*>) model.size else model.count()
    dabaCheckEquals(expected, daba.validateStructure().count, "matrix count")
    dabaCheckEquals(foldMatrices(model), daba.aggregate, "matrix aggregate")
}

private fun foldMatrices(values: Iterable<Matrix>): Matrix {
    var result = Matrix.identity
    for (value in values) {
        result = Matrix.multiply(result, value)
    }
    return result
}

private fun foldOffset(values: Iterable<Int>): Int {
    var result = OFFSET_IDENTITY
    for (value in values) {
        result = result + value - OFFSET_IDENTITY
    }
    return result
}

private fun assertOffsetState(daba: DabaLite<Int>, model: ArrayDeque<Int>, previousMaximum: Int): Int {
    dabaCheckEquals(model.size, daba.validateStructure().count, "offset count")
    CountingOffsetMonoid.reset()
    dabaCheckEquals(foldOffset(model), daba.aggregate, "offset aggregate")
    dabaCheck(CountingOffsetMonoid.combineCount in 0..1, "query combine ceiling")
    return maxOf(previousMaximum, CountingOffsetMonoid.combineCount)
}

private fun classifyNextFixup(statistics: DabaLiteStatistics, evicting: Boolean): FixupPhase = when {
    (!evicting && statistics.count == 0) || (evicting && statistics.frontLength == 1) -> FixupPhase.SINGLETON
    statistics.leftLength + statistics.rightLength + statistics.accumulatorLength == 0 -> FixupPhase.FLIP_AND_SHRINK
    statistics.leftLength == 0 -> FixupPhase.SHIFT
    else -> FixupPhase.SHRINK
}

private fun findCombineHistory(inserting: Boolean, minimumCallbacks: Int): List<Boolean> =
    findHistory(
        inserting = inserting,
        minimumCallbacks = minimumCallbacks,
        monoid = ThrowingCombineMonoid,
        reset = ThrowingCombineMonoid::reset,
        callbackCount = { ThrowingCombineMonoid.combineCount },
    )

private fun findEmptyHistory(inserting: Boolean, minimumCallbacks: Int): List<Boolean> =
    findHistory(
        inserting = inserting,
        minimumCallbacks = minimumCallbacks,
        monoid = ThrowingEmptyMonoid,
        reset = ThrowingEmptyMonoid::reset,
        callbackCount = { ThrowingEmptyMonoid.emptyCount },
    )

private fun findMaximumEmptyCallbacks(inserting: Boolean): Int {
    var maximum = 0
    for (ordinal in 1..3) {
        try {
            findEmptyHistory(inserting, ordinal)
            maximum = ordinal
        } catch (_: NoSuchElementException) {
            break
        }
    }
    return maximum
}

private fun findHistory(
    inserting: Boolean,
    minimumCallbacks: Int,
    monoid: Monoid<Int>,
    reset: () -> Unit,
    callbackCount: () -> Int,
): List<Boolean> {
    for (length in 0..10) {
        for (mask in 0 until (1 shl length)) {
            val history = decodeHistory(mask, length)
            reset()
            val replay = replayHistory(history, monoid) ?: continue
            if (!inserting && replay.second.isEmpty()) {
                continue
            }
            reset()
            applyCandidate(replay.first, inserting)
            if (callbackCount() >= minimumCallbacks) {
                reset()
                return history
            }
        }
    }
    throw NoSuchElementException("No short DABA history reaches callback ordinal $minimumCallbacks.")
}

private fun decodeHistory(mask: Int, length: Int): List<Boolean> =
    List(length) { index -> (mask and (1 shl index)) != 0 }

private fun replayHistory(history: List<Boolean>, monoid: Monoid<Int>): Pair<DabaLite<Int>, ArrayDeque<Int>>? {
    val daba = DabaLite(monoid)
    val model = ArrayDeque<Int>()
    var next = 20
    for (inserting in history) {
        if (inserting) {
            daba.insert(next)
            model.addLast(next++)
        } else {
            if (model.isEmpty()) {
                return null
            }
            daba.evict()
            model.removeFirst()
        }
    }
    return daba to model
}

private fun applyCandidate(daba: DabaLite<Int>, inserting: Boolean) {
    if (inserting) {
        daba.insert(101)
    } else {
        daba.evict()
    }
}

private fun retryCandidateAndContinue(daba: DabaLite<Int>, model: ArrayDeque<Int>, inserting: Boolean) {
    applyCandidate(daba, inserting)
    if (inserting) {
        model.addLast(101)
    } else {
        model.removeFirst()
    }

    repeat(24) { index ->
        daba.insert(200 + index)
        model.addLast(200 + index)
        if ((index and 1) == 0) {
            daba.evict()
            model.removeFirst()
        }
        dabaCheckEquals(model.size, daba.validateStructure().count, "continued count")
        dabaCheckEquals(foldOffset(model), daba.aggregate, "continued aggregate")
    }
}

private fun createRetiredChunkScenario(): Pair<DabaLite<Int>, WeakReference<Any>> {
    val daba = DabaLite(OffsetMonoid)
    val retired = WeakReference(daba.firstChunkIdentityForTesting)
    repeat(129) { daba.insert(it + 20) }
    repeat(64) { daba.evict() }
    return daba to retired
}

private fun createPromptReferenceReleaseScenario(): Pair<DabaLite<ReferenceAggregate>, WeakReference<Any>> {
    val daba = DabaLite(FirstReferenceMonoid)
    val victim = Any()
    val weak = WeakReference<Any>(victim)
    daba.insert(ReferenceAggregate(victim))
    daba.insert(ReferenceAggregate(Any()))
    daba.evict()
    return daba to weak
}

private fun collectUntilCleared(reference: WeakReference<*>) {
    repeat(12) {
        if (reference.get() == null) {
            return
        }
        val pressure = Array(4) { ByteArray(1 shl 20) }
        System.gc()
        Reference.reachabilityFence(pressure)
    }
}

private enum class FixupPhase {
    SINGLETON,
    FLIP_AND_SHRINK,
    SHIFT,
    SHRINK,
}

private data class Matrix(val m00: Long, val m01: Long, val m10: Long, val m11: Long) {
    companion object {
        private const val MODULUS: Long = 1_000_003L

        val identity: Matrix = Matrix(1, 0, 0, 1)

        fun create(seed: Int): Matrix = Matrix(
            kotlin.math.abs(seed.toLong() * 17 + 3) % MODULUS,
            kotlin.math.abs(seed.toLong() * 29 + 5) % MODULUS,
            kotlin.math.abs(seed.toLong() * 43 + 7) % MODULUS,
            kotlin.math.abs(seed.toLong() * 61 + 11) % MODULUS,
        )

        fun multiply(left: Matrix, right: Matrix): Matrix = Matrix(
            (left.m00 * right.m00 + left.m01 * right.m10) % MODULUS,
            (left.m00 * right.m01 + left.m01 * right.m11) % MODULUS,
            (left.m10 * right.m00 + left.m11 * right.m10) % MODULUS,
            (left.m10 * right.m01 + left.m11 * right.m11) % MODULUS,
        )
    }
}

private object MatrixMonoid : Monoid<Matrix> {
    override val empty: Matrix
        get() = Matrix.identity

    override fun combine(left: Matrix, right: Matrix): Matrix = Matrix.multiply(left, right)
}

private object LongSumMonoid : Monoid<Long> {
    override val empty: Long = 0L
    override fun combine(left: Long, right: Long): Long = left + right
}

private object FirstNonNullMonoid : Monoid<String?> {
    override val empty: String? = null
    override fun combine(left: String?, right: String?): String? = left ?: right
}

private object OffsetMonoid : Monoid<Int> {
    override val empty: Int = OFFSET_IDENTITY
    override fun combine(left: Int, right: Int): Int = left + right - OFFSET_IDENTITY
}

private object CountingOffsetMonoid : Monoid<Int> {
    var combineCount: Int = 0
        private set
    var emptyCount: Int = 0
        private set

    override val empty: Int
        get() {
            emptyCount++
            return OFFSET_IDENTITY
        }

    override fun combine(left: Int, right: Int): Int {
        combineCount++
        return left + right - OFFSET_IDENTITY
    }

    fun reset() {
        combineCount = 0
        emptyCount = 0
    }
}

private object ThrowingCombineMonoid : Monoid<Int> {
    private var throwOn: Int = 0
    var combineCount: Int = 0
        private set

    override val empty: Int = OFFSET_IDENTITY

    override fun combine(left: Int, right: Int): Int {
        combineCount++
        if (combineCount == throwOn) {
            throw CallbackException()
        }
        return left + right - OFFSET_IDENTITY
    }

    fun reset() {
        combineCount = 0
        throwOn = 0
    }

    fun throwOn(ordinal: Int) {
        combineCount = 0
        throwOn = ordinal
    }
}

private object ThrowingEmptyMonoid : Monoid<Int> {
    private var throwOn: Int = 0
    var emptyCount: Int = 0
        private set

    override val empty: Int
        get() {
            emptyCount++
            if (emptyCount == throwOn) {
                throw CallbackException()
            }
            return OFFSET_IDENTITY
        }

    override fun combine(left: Int, right: Int): Int = left + right - OFFSET_IDENTITY

    fun reset() {
        emptyCount = 0
        throwOn = 0
    }

    fun throwOn(ordinal: Int) {
        emptyCount = 0
        throwOn = ordinal
    }
}

private class ReferenceAggregate(val token: Any?, val isIdentity: Boolean = false) {
    companion object {
        val identity: ReferenceAggregate = ReferenceAggregate(null, isIdentity = true)
    }
}

private object FirstReferenceMonoid : Monoid<ReferenceAggregate> {
    override val empty: ReferenceAggregate
        get() = ReferenceAggregate.identity

    override fun combine(left: ReferenceAggregate, right: ReferenceAggregate): ReferenceAggregate =
        if (left.isIdentity) right else left
}

private class CallbackException : RuntimeException()

internal fun dabaLiteTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "dabaExhaustiveShortHistoriesMatchNoncommutativeModel" to ::dabaExhaustiveShortHistoriesMatchNoncommutativeModel,
    "dabaRandomizedHistoryMatchesNaiveModel" to ::dabaRandomizedHistoryMatchesNaiveModel,
    "dabaChunkBoundariesAndChurnRemainBounded" to ::dabaChunkBoundariesAndChurnRemainBounded,
    "dabaNonDefaultIdentityCoversEveryFixupAndCallbackCeiling" to
        ::dabaNonDefaultIdentityCoversEveryFixupAndCallbackCeiling,
    "dabaEveryReachableCombineFailureIsAtomic" to ::dabaEveryReachableCombineFailureIsAtomic,
    "dabaEveryReachableIdentityFailureAndClearAreAtomic" to
        ::dabaEveryReachableIdentityFailureAndClearAreAtomic,
    "dabaInsertBoundaryRollbackUnlinksProvisionalChunk" to ::dabaInsertBoundaryRollbackUnlinksProvisionalChunk,
    "dabaClearReusesOneChunkAndRetiredStorageIsCollectible" to
        ::dabaClearReusesOneChunkAndRetiredStorageIsCollectible,
)
