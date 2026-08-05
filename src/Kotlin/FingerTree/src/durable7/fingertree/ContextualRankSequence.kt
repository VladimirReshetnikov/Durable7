/*
 * A persistent sequence that ranks and selects events whose occurrence depends on finite left
 * context.
 *
 * An ordinary sum-measured sequence ranks elements that carry a fixed local weight. It cannot rank a
 * comma that separates fields in one prefix context and is quoted data in another, because no single
 * weight is right for both. A streaming automaton keeps that context but has to replay a prefix after
 * every edit. Caching, for every possible incoming state, the outgoing state and the emitted event
 * count of each subtree turns the context-dependent scan into an associative measure, so one
 * measure-directed descent answers rank and select without replaying anything.
 */
package durable7.fingertree

/**
 * One transition of a deterministic additive event machine.
 *
 * A single transition may emit more than one event: rank counts every emitted event, while select
 * maps each event back to its containing input element and its zero-based ordinal within that
 * element.
 *
 * @property nextState the state after consuming the element; must lie in `0 until stateCount`.
 * @property eventCount the nonnegative number of logical events the transition emits.
 */
public data class ContextualEventTransition(
    public val nextState: Int,
    public val eventCount: Long,
)

/**
 * A finite deterministic machine whose transitions emit nonnegative event counts.
 *
 * This is a runtime policy object, following the same convention as [MeasurePolicy] and
 * [RangeUpdateAlgebra]; the C# reference expresses the same idea as a static-abstract phantom policy
 * type and the Rust port as a trait with an associated constant. A sequence retains the exact
 * instance it was built with, so results — including empty ones — stay usable.
 *
 * [stateCount] must be positive and must not change for the lifetime of a sequence built on this
 * machine. For every state in `0 until stateCount`, [transition] must return a state in the same
 * range together with a nonnegative event count, and it must be deterministic and free of side
 * effects. Violating any of those is a policy bug, not a recoverable miss; see
 * [ContextualRankSequence] for the exact failure contract.
 *
 * @param T the input-element type.
 */
public interface ContextualEventMachine<T> {
    /** The finite number of machine states; must be greater than zero. */
    public val stateCount: Int

    /**
     * Consumes one element from one valid state.
     *
     * @param state the incoming state, always in `0 until stateCount`.
     * @param element the input element.
     * @return the outgoing state and the nonnegative emitted-event count.
     */
    public fun transition(state: Int, element: T): ContextualEventTransition
}

/**
 * The result of running a contextual event machine over a prefix.
 *
 * @property finalState the machine state after the prefix.
 * @property eventCount the number of events the prefix emitted.
 */
public data class ContextualPrefixSummary(
    public val finalState: Int,
    public val eventCount: Long,
)

/**
 * One contextual event together with the input element that emitted it.
 *
 * @property elementIndex the zero-based index of the emitting element.
 * @property eventIndexInElement the event's zero-based ordinal among the events that one transition
 * emitted; always less than [elementEventCount].
 * @property stateBefore the machine state before the element.
 * @property stateAfter the machine state after the element.
 * @property elementEventCount the total number of events that transition emitted.
 */
public data class ContextualEventLocation(
    public val elementIndex: Int,
    public val eventIndexInElement: Long,
    public val stateBefore: Int,
    public val stateAfter: Int,
    public val elementEventCount: Long,
)

/**
 * The two halves produced by [ContextualRankSequence.splitAt].
 *
 * Both halves are complete sequences that share structure with the original, which stays valid.
 *
 * @property left the elements before the boundary.
 * @property right the elements at and after the boundary.
 */
public data class ContextualRankSplit<T>(
    public val left: ContextualRankSequence<T>,
    public val right: ContextualRankSequence<T>,
)

/**
 * The measurements reported by a successful [ContextualRankSequence.validateStructure].
 *
 * @property count the number of elements, recounted by enumeration.
 * @property stateCount the machine's retained state count.
 * @property maximumTotalEventCount the largest whole-sequence event total over all initial states,
 * recomputed by a direct machine scan.
 */
public data class ContextualRankSequenceStatistics(
    public val count: Int,
    public val stateCount: Int,
    public val maximumTotalEventCount: Long,
)

/**
 * An immutable sequence indexed by events whose occurrence depends on finite left context.
 *
 * Every subtree caches, for each of the machine's `s` states, the state that subtree leaves the
 * machine in and the number of events it emits while being consumed. Ordered composition feeds the
 * left subtree's outgoing state into the right subtree's table:
 *
 * ```text
 * (A * B).next(q)   = B.next(A.next(q))
 * (A * B).events(q) = A.events(q) + B.events(A.next(q))
 * (A * B).count     = A.count + B.count
 * ```
 *
 * That operation is associative, so it is a lawful measure, but it is deliberately **not**
 * commutative: `A * B` and `B * A` generally disagree in both components, and every combination
 * therefore has to be performed in sequence order. The identity maps every state to itself, emits no
 * events, and holds no elements. Because event counts are nonnegative, the select predicate
 * `prefix.events(q0) > r` is monotone, so exactly one measure-directed descent finds the element that
 * emitted event `r` — no binary search and no prefix replay.
 *
 * Each element additionally retains its own effect table, computed eagerly when the element is
 * admitted and never recomputed, so a descent reads cached tables instead of re-running the machine.
 *
 * ## Failure contract
 *
 * Out-of-range indices, boundaries, ranges, initial states, and event ranks are ordinary misses and
 * return `null`, matching the rest of this workspace. Three conditions are policy or capacity
 * failures instead:
 *
 * - a machine whose [ContextualEventMachine.stateCount] is not positive is rejected by the factory
 *   with [IllegalArgumentException];
 * - a machine that returns a next state outside `0 until stateCount`, or a negative event count,
 *   raises [IllegalStateException] when the offending element is admitted;
 * - an element count past [Int.MAX_VALUE] or a composed event total past [Long.MAX_VALUE] raises
 *   [ArithmeticException] from `Math.addExact`.
 *
 * Every failure is atomic: each operation builds its complete successor before returning it, so a
 * rejected edit publishes nothing and every retained version stays exactly as it was.
 *
 * ## Bounds
 *
 * Let `n` be this sequence's element count, `m` another operand's, and `s` the machine's state count,
 * which is read once when a sequence lineage is created and then retained. The substrate is the
 * package's strict measured AVL join tree, which performs no lazy amortization, so every figure below
 * is the worst-case cost of the single operation it describes rather than an average earned back over
 * a run of them.
 *
 * | Operation | Cost |
 * | --- | --- |
 * | [size], [isEmpty], [evaluate] | O(1); [evaluate] reads the cached root table |
 * | [get] | O(log n), composing no summary |
 * | [evaluatePrefix], [eventRank], [trySelectEvent] | O(s log n) |
 * | [setItem], [insertAt], [removeAt], [splitAt], [getRange] | O(s log n) |
 * | [prepend], [append] | Θ(s log n) |
 * | [concat] | Θ(s · (log m + \|h − h'\|)) for operand heights `h` and `h'` |
 * | [from], [validateStructure] | O(s n) |
 * | [toList], [iterator] | O(n) |
 *
 * Two of those are weaker than the C# reference's, and the cause is the substrate rather than a
 * different algorithm.
 *
 * **Endpoint updates are Θ(s log n), not O(s) amortized.** A finger tree earns amortized O(1) node
 * work at either end from its digits; this package's substrate is a height-balanced join tree with no
 * finger, so pushing one element joins a singleton against a tree of height Θ(log n) and rebuilds
 * every node on that spine, each rebuild composing an O(s) effect table. [insertAt] at a boundary
 * inherits the same cost, so it too is Θ(s log n) rather than O(s).
 *
 * **Concatenation is Θ(s · (log m + |h − h'|)), which is not O(s log(min(n, m))).** The substrate's
 * join first extracts the leading element of the right operand, walking that operand's left spine at
 * Θ(s log m), and then descends the taller operand until the heights meet, rebuilding Θ(|h − h'|)
 * nodes at O(s) each. Appending a short run to a long sequence therefore costs Θ(s log n), and
 * prepending a long sequence to a short one costs Θ(s log m); in general the honest bound is
 * O(s log(n + m)). The C# reference's log(min(n, m)) form does not hold here in either direction.
 *
 * Storage is O(s n): every node caches an O(s) table and every element caches its own. A path-copying
 * edit adds O(s log n) fresh summary storage while untouched structure stays shared. When the machine
 * is fixed, `s` is a constant and the contextual queries become O(log n) instead of the Θ(n) replay a
 * state-free persistent sequence would need; if `s` is treated as an input rather than a fixed policy,
 * this structure does not dominate a plain persistent sequence, because its edits and its storage both
 * carry the `s` factor explicitly.
 *
 * ## Deliberate limits
 *
 * Context must be finite and must flow left to right. Event weights must be nonnegative so select
 * stays monotone. One input element occupies one measured position, so this is not a chunked text rope
 * and makes no cache-density claim. A retained sequence's machine cannot be changed; rebuilding under
 * another machine costs O(s n).
 *
 * Every version is immutable and safe for concurrent readers to the same extent as the caller-owned
 * elements it stores and the machine callbacks it reaches.
 *
 * @param T the stored input-element type.
 */
public class ContextualRankSequence<T> private constructor(
    private val entries: PersistentMeasuredTree<ContextualEntry<T>, ContextualSummary>,
    private val policy: ContextualMeasure<T>,
) : Iterable<T> {
    /** Factories for contextual rank sequences. */
    public companion object {
        /**
         * Creates the empty sequence over [machine]. Its summary maps every state to itself and emits
         * no events.
         *
         * @throws IllegalArgumentException when [machine] declares fewer than one state.
         */
        public fun <T> empty(machine: ContextualEventMachine<T>): ContextualRankSequence<T> {
            val policy = contextualPolicy(machine)
            return ContextualRankSequence(PersistentMeasuredTree.empty(policy), policy)
        }

        /**
         * Builds a sequence holding [values] in input order under [machine], in O(s n).
         *
         * The source is enumerated exactly once and captured in full before any machine callback runs.
         * Passing a sequence that already retains this exact machine instance returns it unchanged.
         *
         * @throws IllegalArgumentException when [machine] declares fewer than one state.
         * @throws IllegalStateException when [machine] violates its transition contract.
         */
        public fun <T> from(
            values: Iterable<T>,
            machine: ContextualEventMachine<T>,
        ): ContextualRankSequence<T> {
            if (values is ContextualRankSequence<*> && values.machine === machine) {
                @Suppress("UNCHECKED_CAST")
                return values as ContextualRankSequence<T>
            }

            val policy = contextualPolicy(machine)
            val owned = values.toList()
            val admitted = ArrayList<ContextualEntry<T>>(owned.size)
            for (value in owned) {
                admitted.add(policy.entryFor(value))
            }
            return ContextualRankSequence(PersistentMeasuredTree.from(admitted, policy), policy)
        }
    }

    /** The exact machine instance this sequence was built with. */
    public val machine: ContextualEventMachine<T>
        get() = policy.machine

    /** The machine's finite state count, read once when this lineage was created. */
    public val stateCount: Int
        get() = policy.stateCount

    /** The cached number of stored elements. */
    public val size: Int
        get() = entries.size

    /** Whether this sequence holds no elements. */
    public val isEmpty: Boolean
        get() = entries.isEmpty

    /**
     * Returns the element at [index], or `null` when [index] lies outside `0 until size`.
     *
     * A stored `null` element is reported the same way as a missing one, matching [FingerTree.get].
     */
    public operator fun get(index: Int): T? = entries[index]?.value

    /**
     * Runs the machine over the whole sequence from [initialState].
     *
     * @return the final state and emitted-event count, or `null` when [initialState] lies outside
     * `0 until stateCount`.
     */
    public fun evaluate(initialState: Int): ContextualPrefixSummary? {
        if (!isValidState(initialState)) {
            return null
        }
        return entries.measure().evaluate(initialState)
    }

    /**
     * Runs the machine over the first [elementCount] elements from [initialState].
     *
     * @return the state and event count at the prefix boundary, or `null` when [elementCount] lies
     * outside `0..size` or [initialState] outside `0 until stateCount`.
     */
    public fun evaluatePrefix(elementCount: Int, initialState: Int): ContextualPrefixSummary? {
        if (!isValidState(initialState) || elementCount < 0 || elementCount > size) {
            return null
        }
        if (elementCount == 0) {
            return ContextualPrefixSummary(initialState, 0L)
        }
        if (elementCount == size) {
            return entries.measure().evaluate(initialState)
        }
        return entries.measurePrefix(elementCount).evaluate(initialState)
    }

    /**
     * Returns the number of contextual events emitted strictly before an element boundary.
     *
     * @return the boundary's event rank, or `null` on the same inputs as [evaluatePrefix].
     */
    public fun eventRank(elementCount: Int, initialState: Int): Long? =
        evaluatePrefix(elementCount, initialState)?.eventCount

    /**
     * Locates the zero-based contextual event [eventIndex], counting from [initialState].
     *
     * The predicate `prefix.events(initialState) > eventIndex` is monotone because event counts are
     * nonnegative, so one measure-directed descent suffices; no prefix is replayed and the machine is
     * not consulted, because the located element's effect table is already cached.
     *
     * @return the emitting element and transition detail, or `null` when [initialState] lies outside
     * `0 until stateCount` or [eventIndex] is negative or at or past the whole-sequence event total.
     */
    public fun trySelectEvent(eventIndex: Long, initialState: Int): ContextualEventLocation? {
        if (!isValidState(initialState)) {
            return null
        }
        if (eventIndex < 0L || eventIndex >= entries.measure().eventCount(initialState)) {
            return null
        }

        val located = entries.locate { summary -> summary.eventCount(initialState) > eventIndex }
        val entry = located.value
            ?: error("The cached contextual summary disagrees with the stored elements.")
        val before = located.measureBefore
        val stateBefore = before.finalState(initialState)
        return ContextualEventLocation(
            before.count,
            eventIndex - before.eventCount(initialState),
            stateBefore,
            entry.summary.finalState(stateBefore),
            entry.summary.eventCount(stateBefore),
        )
    }

    /**
     * Returns a sequence with [value] added at the front.
     *
     * @throws IllegalStateException when the machine violates its transition contract.
     * @throws ArithmeticException when the element count would exceed [Int.MAX_VALUE].
     */
    public fun prepend(value: T): ContextualRankSequence<T> {
        Math.addExact(size, 1)
        return wrap(entries.prepend(policy.entryFor(value)))
    }

    /**
     * Returns a sequence with [value] added at the back.
     *
     * @throws IllegalStateException when the machine violates its transition contract.
     * @throws ArithmeticException when the element count would exceed [Int.MAX_VALUE].
     */
    public fun append(value: T): ContextualRankSequence<T> {
        Math.addExact(size, 1)
        return wrap(entries.append(policy.entryFor(value)))
    }

    /**
     * Concatenates [other] after this sequence, reusing a nonempty operand when the other is empty.
     *
     * Compatible operands must retain the same machine object or machine values that compare equal
     * and agree on [stateCount]. This follows [RangeUpdateSequence.concat], which likewise accepts
     * identical or value-equal policies, and relaxes the C# and Rust ports, which gate compatibility
     * by the machine's static type because it is a type parameter there. The compatibility check
     * precedes the empty-operand shortcut, so an incompatible operand is rejected even when it is
     * empty.
     *
     * @throws IllegalArgumentException when the two machines are incompatible.
     * @throws ArithmeticException when the combined element count would exceed [Int.MAX_VALUE], or
     * the composed event total would exceed [Long.MAX_VALUE].
     */
    public fun concat(other: ContextualRankSequence<T>): ContextualRankSequence<T> {
        // Structural impossibility precedes the compatibility check, which may reach a caller-owned
        // machine's equals.
        Math.addExact(size, other.size)
        require(policy == other.policy) {
            "Cannot concatenate contextual rank sequences built with different event machines."
        }
        if (other.isEmpty) {
            return this
        }
        if (isEmpty) {
            return other
        }
        return wrap(entries.concat(other.entries))
    }

    /**
     * Inserts [value] so that [index] existing elements precede it.
     *
     * @return the updated version, or `null` when [index] lies outside `0..size`.
     * @throws IllegalStateException when the machine violates its transition contract.
     * @throws ArithmeticException when the element count would exceed [Int.MAX_VALUE], or the
     * composed event total would exceed [Long.MAX_VALUE].
     */
    public fun insertAt(index: Int, value: T): ContextualRankSequence<T>? {
        if (index < 0 || index > size) {
            return null
        }
        Math.addExact(size, 1)
        val inserted = entries.insertAt(index, policy.entryFor(value))
            ?: error("A validated boundary must be insertable.")
        return wrap(inserted)
    }

    /**
     * Replaces the element at [index] unconditionally, performing no element-equality test.
     *
     * @return the updated version, or `null` when [index] lies outside `0 until size`.
     * @throws IllegalStateException when the machine violates its transition contract.
     * @throws ArithmeticException when the composed event total would exceed [Long.MAX_VALUE].
     */
    public fun setItem(index: Int, value: T): ContextualRankSequence<T>? {
        if (index < 0 || index >= size) {
            return null
        }
        val replaced = entries.setItem(index, policy.entryFor(value))
            ?: error("A validated element index must be replaceable.")
        return wrap(replaced)
    }

    /**
     * Removes the element at [index].
     *
     * @return the updated version, or `null` when [index] lies outside `0 until size`.
     */
    public fun removeAt(index: Int): ContextualRankSequence<T>? {
        if (index < 0 || index >= size) {
            return null
        }
        val removed = entries.removeAt(index) ?: error("A validated element index must be removable.")
        return wrap(removed)
    }

    /**
     * Splits at an element boundary. Splitting at either endpoint returns this exact instance as the
     * nonempty half.
     *
     * @return the prefix and suffix, or `null` when [index] lies outside `0..size`.
     */
    public fun splitAt(index: Int): ContextualRankSplit<T>? {
        if (index < 0 || index > size) {
            return null
        }
        if (index == 0) {
            return ContextualRankSplit(emptyInLineage(), this)
        }
        if (index == size) {
            return ContextualRankSplit(this, emptyInLineage())
        }

        val split = entries.splitAt(index) ?: error("A validated boundary must be splittable.")
        return ContextualRankSplit(wrap(split.first), wrap(split.second))
    }

    /**
     * Returns the [count] elements starting at [start]. A range covering the whole of a non-empty
     * sequence returns this exact instance; an empty range is answered first, so on an empty
     * receiver the whole-sequence range yields this lineage's empty sequence rather than the
     * receiver itself.
     *
     * @return the selected range, or `null` when the range falls outside the sequence.
     */
    public fun getRange(start: Int, count: Int): ContextualRankSequence<T>? {
        if (!isValidRange(start, count, size)) {
            return null
        }
        if (count == 0) {
            return emptyInLineage()
        }
        if (count == size) {
            return this
        }

        val tail = entries.splitAt(start) ?: error("A validated range start must be splittable.")
        val head = tail.second.splitAt(count) ?: error("A validated range length must be splittable.")
        return wrap(head.first)
    }

    /** Materializes the elements in sequence order. */
    public fun toList(): List<T> {
        val result = ArrayList<T>(size)
        for (entry in entries) {
            result.add(entry.value)
        }
        return result
    }

    /** Whether the two versions share at least one physical node. Intended for deterministic tests. */
    public fun sharesStructureWith(other: ContextualRankSequence<T>): Boolean =
        entries.sharesStructureWith(other.entries)

    /**
     * Rescans every element with the machine and compares the result with every cached table.
     *
     * A defensive audit for tests and for suspect data, not a routine operation: it runs the machine
     * once per element per state, so it costs O(s n) and invokes the machine `s · n` times.
     *
     * @return the recomputed element count, state count, and largest whole-sequence event total.
     * @throws IllegalStateException on the first invariant violation found.
     * @throws ArithmeticException when the rescan's own event total exceeds [Long.MAX_VALUE].
     */
    public fun validateStructure(): ContextualRankSequenceStatistics {
        check(entries.isBalanced()) { "The contextual rank sequence's measured tree is unbalanced." }

        val width = policy.stateCount
        val root = entries.measure()
        check(root.width == width) {
            "The cached root summary does not describe exactly one effect per machine state."
        }

        val stored = entries.toList()
        check(stored.size == size && root.count == size) {
            "The cached element count disagrees with enumeration."
        }
        for (entry in stored) {
            check(entry.summary.count == 1 && entry.summary.width == width) {
                "A cached leaf summary is not a single-element table over every machine state."
            }
        }

        var maximumTotalEventCount = 0L
        for (initialState in 0 until width) {
            var state = initialState
            var events = 0L
            for (entry in stored) {
                val transition = policy.machine.transition(state, entry.value)
                check(transition.nextState >= 0 && transition.nextState < width) {
                    "The contextual event machine returned a state outside its declared range."
                }
                check(transition.eventCount >= 0L) {
                    "The contextual event machine returned a negative event count."
                }
                check(
                    entry.summary.finalState(state) == transition.nextState &&
                        entry.summary.eventCount(state) == transition.eventCount,
                ) {
                    "A cached leaf effect table disagrees with a direct machine scan."
                }
                events = Math.addExact(events, transition.eventCount)
                state = transition.nextState
            }

            check(state == root.finalState(initialState)) {
                "A cached outgoing state disagrees with a direct machine scan."
            }
            check(events == root.eventCount(initialState)) {
                "A cached event count disagrees with a direct machine scan."
            }
            if (events > maximumTotalEventCount) {
                maximumTotalEventCount = events
            }
        }

        return ContextualRankSequenceStatistics(size, width, maximumTotalEventCount)
    }

    /** Iterates the stored elements in sequence order. */
    override fun iterator(): Iterator<T> = ContextualValueIterator(entries.iterator())

    private fun isValidState(state: Int): Boolean = state >= 0 && state < policy.stateCount

    private fun wrap(
        tree: PersistentMeasuredTree<ContextualEntry<T>, ContextualSummary>,
    ): ContextualRankSequence<T> = ContextualRankSequence(tree, policy)

    private fun emptyInLineage(): ContextualRankSequence<T> =
        wrap(PersistentMeasuredTree.empty(policy))
}

/**
 * A subtree's effect on the machine: for every incoming state, the outgoing state and the number of
 * events emitted, together with the element count. The tables are plain eagerly filled arrays, never
 * deferred behind a thunk, and are never mutated after construction.
 */
private class ContextualSummary(
    val count: Int,
    private val nextStates: IntArray,
    private val eventCounts: LongArray,
) {
    val width: Int
        get() = nextStates.size

    fun finalState(initialState: Int): Int = nextStates[initialState]

    fun eventCount(initialState: Int): Long = eventCounts[initialState]

    fun evaluate(initialState: Int): ContextualPrefixSummary =
        ContextualPrefixSummary(nextStates[initialState], eventCounts[initialState])
}

/** One stored element together with its own eagerly computed effect table. */
private class ContextualEntry<T>(val value: T, val summary: ContextualSummary)

/**
 * The measure policy that lifts one machine into the package's measured tree. It retains the machine
 * and the state count it read once, so a sequence never re-reads a mutable [ContextualEventMachine.stateCount].
 */
private class ContextualMeasure<T>(
    val machine: ContextualEventMachine<T>,
    val stateCount: Int,
) : MeasurePolicy<ContextualEntry<T>, ContextualSummary> {
    override val empty: ContextualSummary =
        ContextualSummary(0, IntArray(stateCount) { it }, LongArray(stateCount))

    override fun measure(element: ContextualEntry<T>): ContextualSummary = element.summary

    override fun combine(left: ContextualSummary, right: ContextualSummary): ContextualSummary {
        if (left.count == 0) {
            return right
        }
        if (right.count == 0) {
            return left
        }

        val nextStates = IntArray(stateCount)
        val eventCounts = LongArray(stateCount)
        for (state in 0 until stateCount) {
            // Ordered composition: the left operand runs first and hands its outgoing state to the
            // right operand. Swapping the operands generally changes both components.
            val middle = left.finalState(state)
            nextStates[state] = right.finalState(middle)
            eventCounts[state] = Math.addExact(left.eventCount(state), right.eventCount(middle))
        }
        return ContextualSummary(Math.addExact(left.count, right.count), nextStates, eventCounts)
    }

    /** Admits one element, running the machine once per state and validating every transition. */
    fun entryFor(value: T): ContextualEntry<T> {
        val nextStates = IntArray(stateCount)
        val eventCounts = LongArray(stateCount)
        for (state in 0 until stateCount) {
            val transition = machine.transition(state, value)
            check(transition.nextState >= 0 && transition.nextState < stateCount) {
                "The contextual event machine returned a state outside its declared range."
            }
            check(transition.eventCount >= 0L) {
                "The contextual event machine returned a negative event count."
            }
            nextStates[state] = transition.nextState
            eventCounts[state] = transition.eventCount
        }
        return ContextualEntry(value, ContextualSummary(1, nextStates, eventCounts))
    }

    override fun equals(other: Any?): Boolean =
        other is ContextualMeasure<*> &&
            stateCount == other.stateCount &&
            (machine === other.machine || machine == other.machine)

    override fun hashCode(): Int = 31 * stateCount + machine.hashCode()
}

/** Reads and validates a machine's state count once, then retains it in a fresh policy. */
private fun <T> contextualPolicy(machine: ContextualEventMachine<T>): ContextualMeasure<T> {
    val stateCount = machine.stateCount
    require(stateCount > 0) { "A contextual event machine must declare at least one state." }
    return ContextualMeasure(machine, stateCount)
}

/** Unwraps stored entries so callers iterate their own elements. */
private class ContextualValueIterator<T>(
    private val source: Iterator<ContextualEntry<T>>,
) : Iterator<T> {
    override fun hasNext(): Boolean = source.hasNext()

    override fun next(): T = source.next().value
}
