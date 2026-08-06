/*
 * Bound probes for the measured finger-tree core.
 *
 * These are the inverted forms of the measurements that established the old join-tree core's
 * weaker bounds. Each counts the policy's combine/measure calls, because asymptotic claims are
 * invisible to correctness tests: an eager join tree would pass every behavioural case here
 * while failing every counter.
 */
package durable7.fingertree

private fun probeCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> probeCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> probeCheckThrows(message: String, action: () -> Unit) {
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

/** The size measure with live call counters, so probes can see exactly what an operation paid. */
private class CountingSizeMeasure : MeasurePolicy<Int, Int> {
    var measureCalls: Int = 0
    var combineCalls: Int = 0

    override val empty: Int = 0

    override fun measure(element: Int): Int {
        measureCalls++
        return 1
    }

    override fun combine(left: Int, right: Int): Int {
        combineCalls++
        return left + right
    }

    fun reset() {
        measureCalls = 0
        combineCalls = 0
    }
}

/** A size measure whose combine can be armed to fail, for failure-atomicity probes. */
private class FailingSizeMeasure : MeasurePolicy<Int, Int> {
    var armed: Boolean = false

    override val empty: Int = 0

    override fun measure(element: Int): Int = 1

    override fun combine(left: Int, right: Int): Int {
        check(!armed) { "Injected combine failure." }
        return left + right
    }
}

/**
 * Appends must cost amortized O(1) policy work - the join tree paid Theta(log n) per push. The
 * immediate work of an endpoint update touches only the outer digit; every deeper cascade is
 * suspended, so the combine totals are identical at both sizes and small: one 2-3 node build
 * (two combines) per roughly three pushes, about 340 combines for 512 pushes.
 */
private fun endpointUpdateCostIsIndependentOfSize() {
    val costs = ArrayList<Int>()
    for (count in listOf(1_024, 32_768)) {
        val policy = CountingSizeMeasure()
        var tree = FingerTree.from(0 until count, policy)
        policy.reset()
        for (value in 0 until 256) {
            tree = tree.append(value)
        }
        for (value in 0 until 256) {
            tree = tree.prepend(value)
        }
        costs.add(policy.combineCalls)
        probeCheckEquals(count + 512, tree.size, "endpoint probe size at n=$count.")
    }

    probeCheckEquals(costs[0], costs[1], "endpoint update cost must not grow with tree size.")
    probeCheck(costs[0] <= 3 * 512, "512 endpoint updates must stay within the amortized budget, cost ${costs[0]}.")
}

/** Front, back, and size are digit and cache reads: no policy call, no middle forcing. */
private fun frontAndBackReadDigitsWithoutPolicyCalls() {
    val policy = CountingSizeMeasure()
    val tree = FingerTree.from(0 until 1_024, policy)
    val grown = tree.append(1_024).prepend(-1)
    policy.reset()

    probeCheckEquals(0, tree.front(), "front probe value.")
    probeCheckEquals(1_023, tree.back(), "back probe value.")
    probeCheckEquals(-1, grown.front(), "grown front probe value.")
    probeCheckEquals(1_024, grown.back(), "grown back probe value.")
    probeCheckEquals(1_024, tree.size, "front/back probe size.")
    probeCheckEquals(1_026, grown.size, "grown probe size.")

    probeCheckEquals(0, policy.measureCalls, "front/back/size must not measure any element.")
    probeCheckEquals(0, policy.combineCalls, "front/back/size must not combine any measures.")
}

/**
 * Concatenation defers its middle recursion, so the immediate cost is one digit regrouping
 * regardless of how unequal the operands are. The join tree's concat walked the taller operand's
 * spine - its cost grew with the height difference.
 */
private fun concatImmediateCostIgnoresOperandAsymmetry() {
    val policy = CountingSizeMeasure()
    val big = FingerTree.from(0 until 4_096, policy)
    val costs = ArrayList<Int>()
    for (smallCount in listOf(1, 1_024)) {
        val small = FingerTree.from(0 until smallCount, policy)
        policy.reset()
        val joined = big.concat(small)
        costs.add(policy.combineCalls)
        probeCheckEquals(4_096 + smallCount, joined.size, "concat probe size for m=$smallCount.")
    }

    for (cost in costs) {
        probeCheck(cost <= 8, "an immediate concat must only regroup one digit boundary: $costs.")
    }
}

/**
 * Work deferred by one version and forced by another must never be repeated. This is the
 * property that makes the amortized bounds valid under persistence: the suspended spine is
 * shared, so the first full-measure force pays for everyone, and each sibling branch re-pays
 * only its own unshared root digits.
 */
private fun forcingMemoizesAcrossPersistentBranches() {
    val policy = CountingSizeMeasure()
    var base = FingerTree.empty(policy)
    for (value in 0 until 10_000) {
        base = base.append(value)
    }

    // Branch the unforced spine 49 ways before anything is forced.
    val branches = (0 until 49).map { value -> base.append(value) }
    policy.reset()

    probeCheckEquals(10_001, branches[0].measure(), "first branch total.")
    val firstForce = policy.combineCalls
    policy.reset()

    for (branch in branches.subList(1, branches.size)) {
        probeCheckEquals(10_001, branch.measure(), "sibling branch total.")
    }
    val otherForces = policy.combineCalls

    probeCheck(firstForce > 2_000, "the first force must replay the deferred spine, combines $firstForce.")
    probeCheck(
        otherForces < firstForce / 3,
        "48 sibling forces must reuse memoized spine work: first $firstForce, rest $otherForces.",
    )
    probeCheck(otherForces / 48 < 200, "each sibling force must be cheap, rest total $otherForces.")
}

/**
 * Organic append loops build Theta(n)-deep pending chains; forcing walks them with an explicit
 * stack, so a 200,000-link chain forces without a StackOverflowError.
 */
private fun deepPendingChainsForceIteratively() {
    var appended = FingerTree.empty(SizeMeasure<Int>())
    for (value in 0 until 200_000) {
        appended = appended.append(value)
    }

    probeCheckEquals(200_000, appended.measure(), "deep chain total measure.")
    probeCheckEquals(0, appended.front(), "deep chain front.")
    probeCheckEquals(199_999, appended.back(), "deep chain back.")
    probeCheckEquals(123_456, appended[123_456], "deep chain index.")
    var iterated = 0
    for (value in appended) {
        iterated++
    }
    probeCheckEquals(200_000, iterated, "deep chain iteration count.")

    var prepended = FingerTree.empty(SizeMeasure<Int>())
    for (value in 0 until 100_000) {
        prepended = prepended.prepend(value)
    }
    probeCheckEquals(100_000, prepended.measure(), "deep prepend chain total measure.")
    probeCheckEquals(99_999, prepended.front(), "deep prepend chain front.")
}

/**
 * A deferred computation that throws publishes nothing: the failed force is retryable (both
 * reads raise, proving the first failure memoized no poison value) and the source stays intact
 * and fully usable once the failure clears.
 */
private fun failedForcesPublishNothingAndAreRetryable() {
    val policy = FailingSizeMeasure()
    var tree = FingerTree.empty(policy)
    for (value in 0 until 100) {
        tree = tree.append(value)
    }

    policy.armed = true
    probeCheckThrows<IllegalStateException>("an armed combine must fail the first force.") {
        tree.measure()
    }
    probeCheckThrows<IllegalStateException>("a failed force publishes nothing, so a retry fails the same way.") {
        tree.measure()
    }
    policy.armed = false

    probeCheckEquals(100, tree.measure(), "the same suspensions force successfully once the failure clears.")
    probeCheckEquals((0 until 100).toList(), tree.toList(), "the source survived the failed forces.")
    probeCheck(tree.debugIsBalanced(), "the source stays structurally valid after failed forces.")
}

/**
 * Endpoint edits defer their overflow work, so the shared structure hides behind pending
 * suspensions; the identity walk must force to see it.
 */
private fun pendingSuspensionsStillRevealStructuralSharing() {
    val policy = SizeMeasure<Int>()
    val base = FingerTree.from(0 until 256, policy)
    val appended = base.append(256).append(257)
    val prepended = base.prepend(-1).prepend(-2)

    probeCheck(base.sharesStorageWith(appended), "append shares structure behind a pending suspension.")
    probeCheck(base.sharesStorageWith(prepended), "prepend shares structure behind a pending suspension.")
    probeCheck(appended.sharesStorageWith(prepended), "sibling edits share the common spine.")
    probeCheck(appended.debugIsBalanced(), "appended branch audits clean.")
    probeCheck(prepended.debugIsBalanced(), "prepended branch audits clean.")
}

/** Randomized endpoint/split/concat edits against a list model, exercising the lazy paths. */
private fun randomizedLazyEditsMatchListModel() {
    var state = 0x0FEE_D5EE_D045_1L
    fun nextInt(bound: Int): Int {
        state = state * 6364136223846793005L + 1442695040888963407L
        return if (bound == 0) 0 else ((state ushr 33) % bound).toInt()
    }

    val policy = SizeMeasure<Int>()
    var tree = FingerTree.empty(policy)
    val model = ArrayList<Int>()

    repeat(600) { round ->
        when (nextInt(8)) {
            0 -> {
                val value = nextInt(1_000)
                tree = tree.append(value)
                model.add(value)
            }
            1 -> {
                val value = nextInt(1_000)
                tree = tree.prepend(value)
                model.add(0, value)
            }
            2 -> {
                val view = tree.tryViewLeft()
                if (view == null) {
                    probeCheck(model.isEmpty(), "left view is null only when empty at round $round.")
                } else {
                    probeCheckEquals(model.removeAt(0), view.first, "left view value at round $round.")
                    tree = view.second
                }
            }
            3 -> {
                val view = tree.tryViewRight()
                if (view == null) {
                    probeCheck(model.isEmpty(), "right view is null only when empty at round $round.")
                } else {
                    probeCheckEquals(model.removeAt(model.size - 1), view.first, "right view value at round $round.")
                    tree = view.second
                }
            }
            4 -> {
                val index = nextInt(model.size + 1)
                val split = tree.splitAtIndex(index)!!
                probeCheckEquals(model.subList(0, index).toList(), split.left.toList(), "split left at round $round.")
                probeCheckEquals(
                    model.subList(index, model.size).toList(),
                    split.right.toList(),
                    "split right at round $round.",
                )
                tree = split.left.concat(split.right)
            }
            5 -> {
                val batch = (0 until nextInt(40)).map { value -> value }
                tree = tree.concat(FingerTree.from(batch, policy))
                model.addAll(batch)
            }
            6 -> if (model.isNotEmpty()) {
                val index = nextInt(model.size)
                probeCheckEquals(model[index], tree[index], "index probe at round $round.")
            }
            else -> {
                val count = nextInt(model.size + 1)
                probeCheckEquals(count, tree.prefixMeasure(count), "prefix measure at round $round.")
            }
        }

        probeCheckEquals(model.size, tree.size, "model size at round $round.")
        probeCheckEquals(model.firstOrNull(), tree.front(), "model front at round $round.")
        probeCheckEquals(model.lastOrNull(), tree.back(), "model back at round $round.")
        if (round % 64 == 0) {
            probeCheckEquals(model.toList(), tree.toList(), "model contents at round $round.")
            probeCheck(tree.debugIsBalanced(), "structural audit at round $round.")
        }
    }

    probeCheckEquals(model.toList(), tree.toList(), "final model contents.")
    probeCheck(tree.debugIsBalanced(), "final structural audit.")
}

/** Test cases exposed to the workspace entry point. */
internal fun measuredFingerTreeProbeTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "endpointUpdateCostIsIndependentOfSize" to ::endpointUpdateCostIsIndependentOfSize,
    "frontAndBackReadDigitsWithoutPolicyCalls" to ::frontAndBackReadDigitsWithoutPolicyCalls,
    "concatImmediateCostIgnoresOperandAsymmetry" to ::concatImmediateCostIgnoresOperandAsymmetry,
    "forcingMemoizesAcrossPersistentBranches" to ::forcingMemoizesAcrossPersistentBranches,
    "deepPendingChainsForceIteratively" to ::deepPendingChainsForceIteratively,
    "failedForcesPublishNothingAndAreRetryable" to ::failedForcesPublishNothingAndAreRetryable,
    "pendingSuspensionsStillRevealStructuralSharing" to ::pendingSuspensionsStillRevealStructuralSharing,
    "randomizedLazyEditsMatchListModel" to ::randomizedLazyEditsMatchListModel,
)
