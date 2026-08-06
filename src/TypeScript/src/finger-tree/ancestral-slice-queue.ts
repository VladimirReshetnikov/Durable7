/**
 * Persistent queue whose every version is one interval of an append-only ancestry path.
 *
 * A value is the constant-sized tuple `(arena, tail, lowDepth, count)` naming an interval of the
 * unique bottom-to-`tail` path inside a shared incremental level-ancestor arena. The visible values
 * are the labels of the ancestors of `tail` at absolute depths `lowDepth` through `depthOf(tail)`,
 * so `count === depthOf(tail) - lowDepth + 1` always holds. Appending adds one child below `tail`;
 * removing or slicing moves only the two interval boundaries.
 *
 * Because the arena is append-only and its published nodes are immutable, every retained handle
 * keeps denoting exactly the same sequence forever, and adding below an *old* handle simply starts
 * a sibling branch. That is what makes the structure fully persistent for its restricted algebra:
 * any historical version can be extended, not only the newest one. It is not confluently
 * persistent, because two unrelated versions cannot be merged.
 *
 * ## The anchored-empty rule
 *
 * An empty value is not "nothing". It retains the node immediately *before* its window as an
 * anchor, so `lowDepth === depthOf(tail) + 1` holds exactly when the value is empty. Appending to
 * any empty slice therefore yields exactly the new value and never resurrects a discarded prefix. A
 * queue drained from the front and one drained from the back keep *different* anchors while
 * denoting the same empty sequence, so there is no canonical empty value and no process-global
 * empty arena.
 *
 * ## Bounds
 *
 * The bounds are parameterized by the backend. Let `M` be the number of labelled nodes the arena
 * has ever published, `U(M)` the cost of adding a leaf, and `Q(M)` the cost of one level-ancestor
 * query. Then {@link AncestralSliceQueue.addLast} costs `U(M)`; {@link AncestralSliceQueue.first},
 * {@link AncestralSliceQueue.getAt}, {@link AncestralSliceQueue.take},
 * {@link AncestralSliceQueue.getRange}, {@link AncestralSliceQueue.tryRemoveFirst}, and a
 * nontrivial {@link AncestralSliceQueue.splitAt} cost `Q(M)`; and {@link AncestralSliceQueue.count},
 * {@link AncestralSliceQueue.isEmpty}, {@link AncestralSliceQueue.last},
 * {@link AncestralSliceQueue.removeFirst}, {@link AncestralSliceQueue.removeLast},
 * {@link AncestralSliceQueue.tryRemoveLast}, and {@link AncestralSliceQueue.drop} are O(1). Every
 * operation allocates O(1) new words besides the single arena node that `addLast` publishes;
 * {@link AncestralSliceQueue.toArray} and iteration walk parent links into one temporary array,
 * taking Theta(count) time and Theta(count) transient element slots rather than `count` queries.
 *
 * Instantiating the backend with Alstrup-Holm incremental level ancestry would give
 * `U(M) = Q(M) = O(1)` *worst case* in linear space, making every scalar operation above O(1) worst
 * case. **That backend is not implemented here**; the statement is a reduction to a published
 * result, not a property of anything in this package. The shipped
 * {@link MyersIncrementalAncestorArena} instead has O(1) *amortized* leaf addition — a single
 * addition at a square boundary pays Theta(sqrt M) to allocate the next odd block — and O(log M)
 * worst-case ancestor queries, so the shipped bounds are `addLast` O(1) amortized, the `Q(M)` group
 * O(log M) worst case, and the O(1) group O(1) worst case. No amortized bound above may be read as
 * a worst-case bound.
 *
 * Boundary-specialized calls make no ancestor query at all: `take(count)`, `drop(0)`, the whole
 * range `getRange(0, count)`, every suffix `getRange(index, count - index)` including the empty
 * suffix `getRange(count, 0)`, a split at `count`, indexed access at the last visible index, and
 * `first` on a singleton. A split at `0` of a *non-empty* queue is not query-free here: the
 * anchored-empty rule requires the empty prefix to retain the node at depth `lowDepth - 1`, and
 * this port always names that node with an ancestor query. When `lowDepth > 0` no cheaper route
 * exists, since only an ancestor query can name that node from the retained tail; when
 * `lowDepth === 0` it is the arena's bottom node, which the seam also names in O(1), so the uniform
 * query there is a choice rather than a necessity. On an *empty* queue the two split boundaries
 * coincide, so a split at `0` is query-free.
 *
 * ## Deliberate limits
 *
 * The algebra intentionally excludes prepending, point replacement, arbitrary middle edits, and
 * concatenation of unrelated histories; omitting them is precisely why the indexed and slicing
 * bounds are possible, and no claim here counts an operation the queue does not support. Arena
 * space is charged to *all* successful historical appends, not to the visible length of one handle:
 * dropping a prefix or releasing a handle reclaims nothing, and every published payload stays
 * reachable until the arena itself is collected. Two handles may enumerate equal values while
 * retaining different tails, anchors, or arenas, so this type promises no O(1) sequence equality or
 * hashing and implements neither.
 *
 * The managed baseline raises when the visible count or the ancestry depth reaches `Int32.MaxValue`
 * and serializes every arena access under a private lock. Neither applies here: JavaScript has no
 * shared-memory concurrency for ordinary objects, and the arena this queue is built on deliberately
 * synthesizes no ceiling of its own because depth and node count are bounded by memory long before
 * they approach `Number.MAX_SAFE_INTEGER`. This port therefore imitates neither.
 */
import { MyersIncrementalAncestorArena, type IncrementalAncestorArena } from "./incremental-ancestor.js";

/**
 * The absolute depth of the last value of a `count`-long window starting at `lowDepth`. O(1).
 *
 * An empty window yields `lowDepth - 1`, which is exactly the anchor depth the anchored-empty rule
 * requires.
 */
function windowEndDepth(lowDepth: number, count: number): number { return lowDepth + count - 1; }

/**
 * Window, anchor, and arena measurements returned by a successful structural audit.
 *
 * The audit uses only the arena's O(1) primitive reads, so it performs no level-ancestor query and
 * cannot perturb a query-count measurement taken around it.
 */
export interface AncestralSliceQueueStatistics {
    readonly count: number;
    readonly lowDepth: number;
    readonly tailDepth: number;
    readonly anchorDepth: number;
    readonly anchoredAtBottom: boolean;
    readonly arenaPublishedNodeCount: number;
}

/**
 * One removed endpoint value and the persistent queue that remains without it.
 *
 * A stored `undefined` is a present value: absence is reported only by an `undefined` result in
 * place of the whole record.
 */
export interface AncestralSliceQueuePop<T> { readonly value: T; readonly rest: AncestralSliceQueue<T> }

/** The two appendable ancestry intervals produced by {@link AncestralSliceQueue.splitAt}. */
export interface AncestralSliceQueueSplit<T> { readonly left: AncestralSliceQueue<T>; readonly right: AncestralSliceQueue<T> }

/**
 * Immutable, fully persistent queue slice denoting one interval of an append ancestry path.
 *
 * A value retains its arena, a tail node, the absolute depth of its first visible value, and a
 * redundant count cache. Empty values retain the node immediately before their window as an anchor,
 * so appending to any empty slice yields exactly the new value without exposing a discarded prefix.
 * Adding below an old handle creates a sibling branch in the shared monotone arena rather than
 * disturbing that handle.
 *
 * See the module documentation for the parameterized and shipped complexity tables and for the
 * operations this algebra deliberately omits.
 */
export class AncestralSliceQueue<T> implements Iterable<T> {
    readonly #arena: IncrementalAncestorArena<T>;
    readonly #tail: number;
    readonly #lowDepth: number;
    readonly #count: number;
    private constructor(arena: IncrementalAncestorArena<T>, tail: number, lowDepth: number, count: number) {
        this.#arena = arena; this.#tail = tail; this.#lowDepth = lowDepth; this.#count = count;
    }

    /**
     * An empty queue owned by an explicit incremental-ancestor arena. O(1).
     *
     * This is the extension seam: supplying a backend with different `U(M)`/`Q(M)` costs changes
     * every parameterized bound the module documents. The arena retains and navigates every
     * appended node. Throws `TypeError` when the arena does not report depth -1 for its bottom
     * node, which the anchored-empty rule needs so that a queue anchored there begins at depth zero.
     */
    public static withArena<T>(arena: IncrementalAncestorArena<T>): AncestralSliceQueue<T> {
        const bottom = arena.bottom;
        if (arena.depthOf(bottom) !== -1) throw new TypeError("An ancestral slice queue arena must report depth -1 for its bottom node.");
        return new AncestralSliceQueue<T>(arena, bottom, 0, 0);
    }

    /**
     * An empty queue backed by a fresh, independently collectible Myers jump-link arena. O(1).
     *
     * There is deliberately no shared global empty value: each call owns its own arena, so
     * unrelated workloads never share retained history.
     */
    public static empty<T>(): AncestralSliceQueue<T> { return AncestralSliceQueue.withArena<T>(new MyersIncrementalAncestorArena<T>()); }

    /**
     * A Myers-backed queue holding `values` in enumeration order, in Theta(k) amortized time.
     *
     * Publishes one arena node per value. An existing queue is deliberately *not* returned
     * unchanged: the result always owns a private arena, so it shares no retained history with its
     * source.
     */
    public static from<T>(values: Iterable<T>): AncestralSliceQueue<T> {
        let result = AncestralSliceQueue.empty<T>();
        for (const value of values) result = result.addLast(value);
        return result;
    }

    /** The number of visible values, read from the cache in O(1). */
    public get count(): number { return this.#count; }

    /** Whether this handle denotes an empty ancestry interval. O(1). */
    public get isEmpty(): boolean { return this.#count === 0; }

    /**
     * The first visible value, costing one ancestor query `Q(M)`.
     *
     * The front is selected jointly by the tail and the low boundary, so it is not an O(1) endpoint
     * read under a logarithmic backend. A singleton reuses its retained tail and makes no query.
     * Throws `RangeError` when the queue is empty.
     */
    public get first(): T {
        this.#ensureNotEmpty();
        return this.#arena.valueAt(this.#count === 1 ? this.#tail : this.#arena.ancestorAtDepth(this.#tail, this.#lowDepth));
    }

    /**
     * The last visible value, read in O(1) because the handle retains the tail directly.
     *
     * Throws `RangeError` when the queue is empty.
     */
    public get last(): T { this.#ensureNotEmpty(); return this.#arena.valueAt(this.#tail); }

    /**
     * The visible value at a zero-based index, costing one ancestor query `Q(M)`.
     *
     * The last visible index reuses the retained tail and makes no query. Throws `RangeError` when
     * the index is not an integer inside the queue.
     */
    public getAt(index: number): T {
        if (!Number.isInteger(index) || index < 0 || index >= this.#count) throw new RangeError("The index is outside the ancestral slice queue.");
        return this.#arena.valueAt(index === this.#count - 1 ? this.#tail : this.#arena.ancestorAtDepth(this.#tail, this.#lowDepth + index));
    }

    /**
     * A queue with `value` appended below this handle's tail or empty anchor, costing `U(M)`.
     *
     * Publishes exactly one arena node. This handle and every other retained version are
     * unaffected; appending below an old handle starts a sibling branch. An arena that rejects the
     * addition publishes nothing and leaves this handle valid.
     */
    public addLast(value: T): AncestralSliceQueue<T> {
        return new AncestralSliceQueue<T>(this.#arena, this.#arena.addLeaf(this.#tail, value), this.#lowDepth, this.#count + 1);
    }

    /**
     * The queue without its first visible value. O(1).
     *
     * Only the low boundary moves, so no ancestor query is made. Emptying a singleton this way
     * leaves the old tail as the result's anchor. Throws `RangeError` when the queue is empty.
     */
    public removeFirst(): AncestralSliceQueue<T> {
        this.#ensureNotEmpty();
        return new AncestralSliceQueue<T>(this.#arena, this.#tail, this.#lowDepth + 1, this.#count - 1);
    }

    /**
     * The queue without its last visible value. O(1).
     *
     * The new tail is the old tail's parent, which the arena reads in constant time. Emptying a
     * singleton this way anchors the result immediately before the window. Throws `RangeError` when
     * the queue is empty.
     */
    public removeLast(): AncestralSliceQueue<T> {
        this.#ensureNotEmpty();
        return new AncestralSliceQueue<T>(this.#arena, this.#arena.parentOf(this.#tail), this.#lowDepth, this.#count - 1);
    }

    /**
     * The first visible value with the remainder, or `undefined` when the queue is empty.
     *
     * Costs `Q(M)` because it also returns the front value. Nothing is published on the empty path
     * and this handle stays valid either way.
     */
    public tryRemoveFirst(): AncestralSliceQueuePop<T> | undefined {
        return this.#count === 0 ? undefined : { value: this.first, rest: this.removeFirst() };
    }

    /**
     * The last visible value with the remainder, or `undefined` when the queue is empty. O(1).
     *
     * The tail is retained directly, so no ancestor query is made.
     */
    public tryRemoveLast(): AncestralSliceQueuePop<T> | undefined {
        return this.#count === 0 ? undefined : { value: this.last, rest: this.removeLast() };
    }

    /**
     * The first `count` visible values, as an appendable ancestry prefix, costing `Q(M)`.
     *
     * `take(this.count)` returns this handle unchanged and makes no query. The result is a real
     * ancestry slice and remains appendable, and `take(0)` on a non-empty queue is anchored
     * immediately before the window. Throws `RangeError` when `count` is not an integer in
     * `0..this.count`.
     */
    public take(count: number): AncestralSliceQueue<T> {
        this.#checkCount(count);
        return count === this.#count ? this : this.getRange(0, count);
    }

    /**
     * The queue after omitting its first `count` visible values. O(1).
     *
     * Only the low boundary moves, so no ancestor query is ever made. `drop(0)` returns this handle
     * unchanged, and `drop(this.count)` keeps the old tail as the empty result's anchor. This is the
     * reference contract's `Drop`. Throws `RangeError` when `count` is not an integer in
     * `0..this.count`.
     */
    public drop(count: number): AncestralSliceQueue<T> {
        this.#checkCount(count);
        if (count === 0) return this;
        return new AncestralSliceQueue<T>(this.#arena, this.#tail, this.#lowDepth + count, this.#count - count);
    }

    /**
     * One contiguous, appendable ancestry slice, costing `Q(M)`. This is the reference contract's
     * `Slice`, renamed because `slice` would read as the array method's start/end convention.
     *
     * A range whose result ends at the current tail — the whole range and every suffix, including
     * the empty suffix `getRange(this.count, 0)` — reuses that tail and makes no query. A
     * zero-length range elsewhere selects the ancestor immediately before its start as an anchor,
     * which is the arena's bottom node when the start is the very front of a queue grown from an
     * empty one.
     *
     * The count is validated as `count > this.count - index`, so an index plus count that would run
     * away is rejected rather than wrapped. Throws `RangeError` when `index` is outside
     * `0..this.count` or `count` is negative or runs past the end.
     */
    public getRange(index: number, count: number): AncestralSliceQueue<T> {
        this.#checkBoundary(index);
        if (!Number.isInteger(count) || count < 0) throw new RangeError("The ancestral slice queue range count must be a nonnegative integer.");
        if (count > this.#count - index) throw new RangeError("The range extends past the end of the ancestral slice queue.");
        if (index === 0 && count === this.#count) return this;

        const lowDepth = this.#lowDepth + index;
        const targetDepth = windowEndDepth(lowDepth, count);
        const tail = targetDepth === windowEndDepth(this.#lowDepth, this.#count) ? this.#tail : this.#arena.ancestorAtDepth(this.#tail, targetDepth);
        return new AncestralSliceQueue<T>(this.#arena, tail, lowDepth, count);
    }

    /**
     * This queue split at a zero-based boundary into an appendable prefix and suffix, costing `Q(M)`.
     *
     * The prefix performs the one ancestor-seeking query and the suffix only moves the low boundary.
     * Both results are real ancestry slices and can independently start new branches. A split at
     * `this.count` makes no query. A split at `0` of a non-empty queue still costs `Q(M)` here,
     * because the anchored-empty rule requires the empty prefix to retain the node at depth
     * `lowDepth - 1`; that is forced when `lowDepth > 0`, while at `lowDepth === 0` the node is the
     * arena's bottom, an O(1) read, so the query is a uniformity choice rather than a necessity. On
     * an empty queue the two boundaries coincide, so the split is query-free there. Throws
     * `RangeError` when the boundary is outside `0..this.count`.
     */
    public splitAt(index: number): AncestralSliceQueueSplit<T> {
        this.#checkBoundary(index);
        return { left: this.take(index), right: this.drop(index) };
    }

    /**
     * The visible values in front-to-back order, in Theta(count) time and transient slots.
     *
     * Follows parent links from the tail rather than performing `count` ancestor queries, so it
     * makes no level-ancestor query at all. A later append cannot enter the captured path.
     */
    public toArray(): T[] {
        const values = new Array<T>(this.#count);
        let node = this.#tail;
        for (let index = this.#count - 1; index >= 0; index -= 1) {
            values[index] = this.#arena.valueAt(node);
            if (index !== 0) node = this.#arena.parentOf(node);
        }
        return values;
    }

    /**
     * Iterate the visible values front to back, capturing the whole path into one array.
     *
     * The captured path is the one this handle denotes, and the arena's published nodes are
     * immutable, so the iterator can never observe a later append. See {@link toArray} for the cost.
     */
    public *[Symbol.iterator](): IterableIterator<T> { yield* this.toArray(); }

    /** The shared arena this handle navigates, exposed for diagnostics rather than mutation. O(1). */
    public get arena(): IncrementalAncestorArena<T> { return this.#arena; }

    /** The arena handle of the retained tail, or of the anchor when the queue is empty. O(1). */
    public get tailHandle(): number { return this.#tail; }

    /** The absolute ancestry depth of the first visible value. O(1). */
    public get lowDepth(): number { return this.#lowDepth; }

    /** Whether both handles navigate the same arena, so their histories can interleave. O(1). */
    public sharesArenaWith(other: AncestralSliceQueue<T>): boolean { return this.#arena === other.#arena; }

    /**
     * Whether both handles retain the same arena node as their tail. O(1).
     *
     * Used to show that a whole-window or suffix operation returned a value sharing the source tail
     * rather than one rebuilt through an ancestor query.
     */
    public sharesTailWith(other: AncestralSliceQueue<T>): boolean { return this.#arena === other.#arena && this.#tail === other.#tail; }

    /**
     * Audit the window against the arena and report what it measured, in Theta(count) time.
     *
     * Checks that the low boundary is a real depth, that the cached count agrees with the tail and
     * low depths, that every visible node carries a value, that each parent hop from the tail drops
     * the depth by exactly one, and that the anchor sits at `lowDepth - 1`. Uses only the arena's
     * O(1) primitive reads, so it makes no level-ancestor query and cannot perturb a query-count
     * measurement taken around it. Throws `Error` on the first violated invariant. A defensive
     * audit, not part of normal use.
     */
    public validateStructure(): AncestralSliceQueueStatistics {
        if (this.#count < 0) throw new Error("The ancestral slice queue count is negative.");
        if (this.#lowDepth < 0) throw new Error("The ancestral slice queue low boundary is not a real depth.");
        const bottom = this.#arena.bottom;
        const tailDepth = this.#arena.depthOf(this.#tail);
        if (tailDepth !== windowEndDepth(this.#lowDepth, this.#count)) throw new Error("The ancestral slice queue count disagrees with its tail and low depths.");
        if ((this.#tail === bottom) !== (tailDepth === -1)) throw new Error("Only the ancestral slice queue arena bottom may sit at depth -1.");

        let node = this.#tail;
        let depth = tailDepth;
        for (let remaining = this.#count; remaining > 0; remaining -= 1) {
            this.#arena.valueAt(node);
            const parent = this.#arena.parentOf(node);
            if (this.#arena.depthOf(parent) !== depth - 1) throw new Error("An ancestral slice queue parent hop skipped a depth.");
            node = parent; depth -= 1;
        }
        if (depth !== this.#lowDepth - 1) throw new Error("The ancestral slice queue anchor is not below its window.");
        const anchoredAtBottom = node === bottom;
        if (anchoredAtBottom !== (depth === -1)) throw new Error("The ancestral slice queue anchor depth contradicts its identity.");
        return { count: this.#count, lowDepth: this.#lowDepth, tailDepth, anchorDepth: depth, anchoredAtBottom, arenaPublishedNodeCount: this.#arena.publishedNodeCount };
    }

    #ensureNotEmpty(): void { if (this.#count === 0) throw new RangeError("The ancestral slice queue is empty."); }
    #checkBoundary(index: number): void {
        if (!Number.isInteger(index) || index < 0 || index > this.#count) throw new RangeError("The boundary is outside the ancestral slice queue.");
    }
    #checkCount(count: number): void {
        if (!Number.isInteger(count) || count < 0 || count > this.#count) throw new RangeError("The count is outside the ancestral slice queue.");
    }
}
