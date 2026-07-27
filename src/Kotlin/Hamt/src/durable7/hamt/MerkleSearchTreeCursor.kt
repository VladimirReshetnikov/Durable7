/*
 * Immutable ordered cursor over one Merkle search tree version.
 */
package durable7.hamt

/** Presence-safe exact-key search whose [cursor] remains usable on a miss. */
public data class MerkleCursorSearch<K, V>(
    public val cursor: MerkleSearchTreeCursor<K, V>,
    public val found: Boolean,
)

/** Immutable tree-snapshot-plus-rank gap cursor in policy-comparator order. */
public class MerkleSearchTreeCursor<K, V> private constructor(
    private val tree: MerkleSearchTree<K, V>,
    public val position: Int,
) {
    internal companion object {
        internal fun <K, V> create(
            tree: MerkleSearchTree<K, V>,
            position: Int,
        ): MerkleSearchTreeCursor<K, V> = MerkleSearchTreeCursor(tree, position)
    }

    init {
        require(position >= 0 && position <= tree.size) { "Cursor position must be in 0..tree.size." }
    }

    /** Number of entries in the tree version this cursor is positioned in. */
    public val size: Int get() = tree.size
    /** Whether the gap precedes the first entry. */
    public val isAtStart: Boolean get() = position == 0
    /** Whether the gap follows the final entry. */
    public val isAtEnd: Boolean get() = position == size

    /** Returns the retained entry immediately before the gap. */
    public fun peekPrevious(): MerkleEntry<K, V>? =
        if (isAtStart) null else tree.entryAtForCursor(position - 1)

    /** Returns the retained entry immediately after the gap. */
    public fun peekNext(): MerkleEntry<K, V>? = tree.entryAtForCursor(position)

    /**
     * A cursor one gap earlier, or `null` at the start. The receiver is unchanged; movement produces a new cursor
     * over the same tree version.
     */
    public fun movePrevious(): MerkleSearchTreeCursor<K, V>? =
        if (isAtStart) null else create(tree, position - 1)

    /** A cursor one gap later, or `null` at the end. The receiver is unchanged. */
    public fun moveNext(): MerkleSearchTreeCursor<K, V>? =
        if (isAtEnd) null else create(tree, position + 1)

    /**
     * A cursor at the given rank gap of the same tree version, or `null` when the position lies outside `0..size`.
     * Seeking to the current position returns this cursor by identity.
     */
    public fun seek(position: Int): MerkleSearchTreeCursor<K, V>? = when {
        position < 0 || position > size -> null
        position == this.position -> this
        else -> create(tree, position)
    }

    /** Strictly inserts a missing key at its lower-bound gap and returns the gap after it. */
    public fun insert(key: K, value: V): MerkleSearchTreeCursor<K, V> {
        val (expected, found) = tree.lowerBoundRankForCursor(key)
        require(!found) { "The key is already present." }
        ensureCurrentGap(expected)
        return create(tree.setItem(key, value), Math.addExact(position, 1))
    }

    /** Updates an exact next entry or inserts at a missing lower-bound gap. */
    public fun setItem(key: K, value: V): MerkleSearchTreeCursor<K, V> {
        val (expected, found) = tree.lowerBoundRankForCursor(key)
        ensureCurrentGap(expected)
        val edited = tree.setItem(key, value)
        if (edited === tree) return this
        return create(edited, if (found) position else Math.addExact(position, 1))
    }

    /** Replaces the next value while retaining its key representative and the current gap. */
    public fun setNextValue(value: V): MerkleSearchTreeCursor<K, V>? {
        val next = peekNext() ?: return null
        val edited = tree.setItem(next.key, value)
        return if (edited === tree) this else create(edited, position)
    }

    /** Deletes the entry before the gap and moves the gap left. */
    public fun deletePrevious(): MerkleSearchTreeCursor<K, V>? {
        val previous = peekPrevious() ?: return null
        return create(tree.remove(previous.key), position - 1)
    }

    /** Deletes the entry after the gap and keeps the gap fixed. */
    public fun deleteNext(): MerkleSearchTreeCursor<K, V>? {
        val next = peekNext() ?: return null
        return create(tree.remove(next.key), position)
    }

    /** Returns this cursor version's canonical immutable tree. */
    public fun snapshot(): MerkleSearchTree<K, V> = tree

    private fun ensureCurrentGap(expected: Int) {
        require(expected == position) {
            "The key belongs at gap $expected, not at the current gap $position."
        }
    }
}
