/*
 * The one value-equality abstraction this package's delta-tracking collections retain.
 *
 * Both [PersistentRunDeltaVector] and [PersistentDeltaMap] answer the same two questions - which
 * writes are semantic no-ops, and when a recorded change cancels against its checkpoint - so they
 * share one policy type rather than declaring a private equality abstraction each. The Rust port
 * ships a single `EqualityPolicy<T>` for exactly this reason.
 */
package durable7.fingertree

/**
 * Compares two values for equivalence on behalf of a delta-tracking collection.
 *
 * An implementation must define a stable equivalence relation - reflexive, symmetric, and
 * transitive - for as long as any version retaining it is alive. That obligation is load-bearing
 * rather than cosmetic: a collection cancels a recorded change when this relation reports equality,
 * so a non-reflexive relation silently corrupts change accounting and leaves phantom records that no
 * invariant check can detect. Values retained by a version must not mutate state this relation
 * observes; replacing such a value through [PersistentRunDeltaVector.setItem] or
 * [PersistentDeltaMap.setItem] is the supported way to change equality-observable state. Concurrent
 * reads over retained versions are safe when this relation is safe for concurrent calls.
 */
public fun interface ValueEqualityComparer<in T> {
    /** Reports whether [left] and [right] belong to the same equivalence class. */
    public fun areEqual(left: T, right: T): Boolean
}

/**
 * A retained value-equality policy.
 *
 * The policy decides which writes are semantic no-ops and when a recorded change cancels against
 * its checkpoint, so it determines a version's observable change set. Retaining it on the value -
 * rather than taking a relation per call - is what makes two versions comparable: they only agree
 * about cancellation when they decided it the same way.
 *
 * Canonical policies ([natural], [reflexiveIeeeDouble], [reflexiveIeeeFloat]) are shared singletons,
 * so independently obtained instances of the same canonical kind are [isCompatibleWith] one another.
 * A [custom] policy carries its own identity: two independently constructed custom policies are
 * incompatible even when their relations happen to agree, because there is no way to prove that.
 */
public class ValueEqualityPolicy<T> private constructor(
    private val comparer: ValueEqualityComparer<T>,
    /** Whether this is one of the shared canonical policies rather than a caller-supplied one. */
    public val isCanonical: Boolean,
) {
    /** Canonical policy factories. */
    public companion object {
        // The natural relation is generic, so `==` here routes through `Any.equals` rather than
        // through a primitive comparison. That matters for floating-point payloads: boxed
        // `java.lang.Double.equals` compares raw bits, so it *is* reflexive on NaN - unlike the
        // primitive `==` that Kotlin selects for statically typed Double or Float operands. It also
        // separates -0.0 from +0.0, which the primitive `==` and .NET's default comparer do not.
        // Reference identity is tested first, matching `RrbVector`'s own value comparison: it skips
        // a redundant `equals` call when a caller writes the very object already stored, and it
        // makes the relation reflexive even for a payload whose `equals` is not.
        private val naturalComparer: ValueEqualityComparer<Any?> =
            ValueEqualityComparer { left, right -> left === right || left == right }

        private val naturalInstance: ValueEqualityPolicy<Any?> =
            ValueEqualityPolicy(naturalComparer, isCanonical = true)

        private val reflexiveDoubleInstance: ValueEqualityPolicy<Double> =
            ValueEqualityPolicy(
                // These operands are statically Double, so `==` is the primitive IEEE comparison:
                // +0.0 equals -0.0 and NaN equals nothing. The added clause supplies the missing
                // reflexivity, which is what makes the whole relation an equivalence relation.
                ValueEqualityComparer { left, right ->
                    left == right || (left.isNaN() && right.isNaN())
                },
                isCanonical = true,
            )

        private val reflexiveFloatInstance: ValueEqualityPolicy<Float> =
            ValueEqualityPolicy(
                ValueEqualityComparer { left, right ->
                    left == right || (left.isNaN() && right.isNaN())
                },
                isCanonical = true,
            )

        /**
         * The shared canonical natural-equality policy, which compares through [Any.equals].
         *
         * On the JVM that is already an equivalence relation for every payload, including boxed
         * floating-point values: `java.lang.Double.equals` compares raw bits, so `NaN` equals `NaN`
         * here even though the primitive `==` does not. It also keeps `-0.0` and `+0.0` in separate
         * classes, which is where it parts company with .NET's default comparer; use
         * [reflexiveIeeeDouble] or [reflexiveIeeeFloat] for the .NET-matching float relation.
         */
        @Suppress("UNCHECKED_CAST")
        public fun <T> natural(): ValueEqualityPolicy<T> = naturalInstance as ValueEqualityPolicy<T>

        /**
         * A distinct policy retaining [comparer], which must be a stable equivalence relation.
         *
         * The returned policy is identified by [comparer], so policies built from the same comparer
         * instance stay compatible while independently built ones do not.
         */
        public fun <T> custom(comparer: ValueEqualityComparer<T>): ValueEqualityPolicy<T> =
            ValueEqualityPolicy(comparer, isCanonical = false)

        /**
         * The shared canonical reflexive IEEE-754 policy for [Double].
         *
         * Every `NaN` is equivalent to every other `NaN` and `-0.0` stays equivalent to `+0.0`,
         * which is both an equivalence relation and the behavior of `EqualityComparer<double>`'s
         * default in the C# baseline.
         */
        public fun reflexiveIeeeDouble(): ValueEqualityPolicy<Double> = reflexiveDoubleInstance

        /**
         * The shared canonical reflexive IEEE-754 policy for [Float].
         *
         * Every `NaN` is equivalent to every other `NaN` and `-0.0f` stays equivalent to `+0.0f`,
         * matching `EqualityComparer<float>`'s default in the C# baseline.
         */
        public fun reflexiveIeeeFloat(): ValueEqualityPolicy<Float> = reflexiveFloatInstance
    }

    /** Reports whether [left] and [right] are equivalent under this policy. */
    public fun areEqual(left: T, right: T): Boolean = comparer.areEqual(left, right)

    /**
     * Whether this policy and [other] retain the same relation identity, so two versions carrying
     * them decided cancellation the same way. A representation test, not an equality test.
     */
    public fun isCompatibleWith(other: ValueEqualityPolicy<T>): Boolean = comparer === other.comparer

    /** A short description that never calls the retained relation. */
    override fun toString(): String = "ValueEqualityPolicy(canonical=$isCanonical)"
}
