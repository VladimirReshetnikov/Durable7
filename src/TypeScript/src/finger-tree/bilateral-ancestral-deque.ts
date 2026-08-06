/**
 * Persistent deque held as two oppositely oriented intervals of an append-only ancestry arena.
 *
 * This is not a balanced sequence tree. A version is a constant-sized handle over one shared
 * {@link IncrementalAncestorArena}: it retains a *left* interval `(l_anchor, l_tail]` and a *right*
 * interval `(r_anchor, r_tail]`, and its logical value is `reverse(left)` followed by `right`.
 * Adding at the front publishes one leaf below the left tail; adding at the back publishes one below
 * the right tail. Reversal merely exchanges the two intervals, so it is O(1) and needs neither a
 * lazy flag nor a rebuild.
 *
 * The load-bearing property is *slice closure*: every contiguous range meets each interval at most
 * once, so a slice is again a bilateral handle rather than a rebuilt tree.
 *
 * Each interval also caches `base`, the first physical node after `anchor`. That cache is redundant
 * but load-bearing: it keeps both endpoint reads query-free even when the opposite arm is empty. For
 * the left arm the logical *first* value sits at `tail` and the logical *last* at `base`; for the
 * right arm those roles are exchanged. Those four handles are the deque's cached endpoints - visible
 * indices `0` and `leftCount - 1`, and `leftCount` and `count - 1` - and every one of them is read
 * with no level-ancestor query at all.
 *
 * ## Bounds
 *
 * Two layers of claim must not be conflated.
 *
 * 1. **The reduction.** Let `M` be the number of nodes the arena has ever published, `U(M)` the cost
 *    of adding a leaf, and `Q(M)` the cost of one level-ancestor query. Then {@link
 *    BilateralAncestralDeque.addFirst} and {@link BilateralAncestralDeque.addLast} cost `U(M)` and
 *    make no query; {@link BilateralAncestralDeque.getAt} costs at most **one** query; {@link
 *    BilateralAncestralDeque.getRange}, {@link BilateralAncestralDeque.take}, {@link
 *    BilateralAncestralDeque.drop}, and {@link BilateralAncestralDeque.splitAt} cost at most **two**,
 *    including the ranges that cross the centre and touch both arms; end removal makes **no** query
 *    on its own nonempty arm and at most **one** when it crosses the centre; and {@link
 *    BilateralAncestralDeque.validateStructure} spends two queries per nonempty interval, a ceiling
 *    of four. `count`, `isEmpty`, `first`, `last`, `reverse`, and `clear` are O(1) worst case and
 *    query-free. Enumeration is Theta(count).
 * 2. **The optimal backend.** An Alstrup-Holm incremental level-ancestor structure has
 *    `U(M) = Q(M) = O(1)` worst case in linear space, which would make every scalar operation of
 *    this restricted algebra O(1) worst case. **No such backend ships here**; that statement is a
 *    reduction to a published result, not a property of anything in this package.
 *
 * Backed by the shipped {@link MyersIncrementalAncestorArena}, end insertion is O(1) *amortized* (a
 * square-boundary addition pays Theta(sqrt(M)) to allocate the next odd block), endpoint reads,
 * `count`, reversal, and clearing are O(1) worst case, and indexing, removal, slicing, splitting,
 * and the structural audit are O(log M) worst case after M historical insertions. Nothing here
 * upgrades an amortized or logarithmic bound to a worst-case constant one.
 *
 * ## Deliberate limits
 *
 * The algebra is restricted on purpose: add or remove at either end, read either end or an indexed
 * value, reverse, take/drop/range/split, and enumerate front to back. Arbitrary concatenation of
 * unrelated versions, point replacement, and middle editing are absent, and no bound above may be
 * read as covering an omitted operation. Arena space is charged to *all* successful historical end
 * insertions, not to the values visible from one handle: a handle retains its arena, and the arena
 * retains every node it ever published. Enumeration is Theta(count) time and materializes O(count)
 * temporary values, because the forward-oriented right interval is reversed through a buffer to keep
 * enumeration query-free.
 *
 * There is no module-global empty value: an empty deque belongs to an arena, and retaining it
 * retains that arena. Two handles may enumerate equal values while retaining different intervals or
 * arenas, so this type promises no O(1) sequence equality and implements none.
 *
 * ## Port notes
 *
 * The managed reference raises `OverflowException` once the visible count or an ancestry depth would
 * pass `Int32.MaxValue`. JavaScript numbers are doubles and the arena publishes ordinary numeric
 * depths, so both ceilings sit far beyond what memory admits; following the stated policy of
 * `incremental-ancestor.ts`, this port synthesizes no artificial limit to imitate them.
 */
import { MyersIncrementalAncestorArena, type IncrementalAncestorArena } from "./incremental-ancestor.js";

/**
 * One oriented ancestry interval `(anchor, tail]` named by arena handles.
 *
 * `count` is the number of visible values, which equals `depthOf(tail) - depthOf(anchor)`. `base`
 * caches the first physical node after `anchor`, so the endpoint on the anchor side is read with no
 * level-ancestor query. An empty interval sets all three handles to the node it is anchored at.
 */
export interface BilateralAncestralDequeInterval {
    readonly anchor: number;
    readonly base: number;
    readonly tail: number;
    readonly count: number;
}

/**
 * Interval and arena measurements returned by a successful structural audit.
 *
 * The two anchor depths are absolute; the arena's bottom node reports `-1`. They are read with the
 * arena's O(1) primitives and cost no level-ancestor query.
 */
export interface BilateralAncestralDequeStatistics {
    readonly count: number;
    readonly leftCount: number;
    readonly rightCount: number;
    readonly leftAnchorDepth: number;
    readonly rightAnchorDepth: number;
    readonly arenaPublishedNodeCount: number;
}

/** One removed endpoint value together with the persistent deque that remains without it. */
export interface BilateralAncestralDequePop<T> {
    readonly value: T;
    readonly rest: BilateralAncestralDeque<T>;
}

/**
 * The prefix and suffix produced by {@link BilateralAncestralDeque.splitAt}. Together they enumerate
 * the value that was split, and each side stays independently growable.
 */
export interface BilateralAncestralDequeSplit<T> {
    readonly left: BilateralAncestralDeque<T>;
    readonly right: BilateralAncestralDeque<T>;
}

/** The empty interval whose three endpoints all name one node. */
function emptyInterval(node: number): BilateralAncestralDequeInterval { return { anchor: node, base: node, tail: node, count: 0 }; }

/**
 * Immutable deque held as two oppositely oriented intervals of an append-only ancestry arena.
 *
 * A value retains its arena, the two interval descriptors, and a redundant total-count cache. Every
 * operation returns a new handle and leaves this one - and every other retained version - denoting
 * exactly the same sequence forever, because the arena is append-only and its published nodes are
 * immutable. Growing an *old* handle starts a sibling branch rather than disturbing the handles
 * derived from it, which is what makes this fully persistent for its restricted algebra; it is not
 * confluently persistent, since two unrelated versions cannot be merged.
 *
 * See the module documentation for the representation, the query ceilings, the separation between
 * the optimal-backend bounds and the shipped Myers bounds, and the omitted operations.
 */
export class BilateralAncestralDeque<T> implements Iterable<T> {
    readonly #arena: IncrementalAncestorArena<T>;
    readonly #left: BilateralAncestralDequeInterval;
    readonly #right: BilateralAncestralDequeInterval;
    /** The number of visible values, read from the cache. O(1), query-free. */
    public readonly count: number;
    private constructor(arena: IncrementalAncestorArena<T>, left: BilateralAncestralDequeInterval, right: BilateralAncestralDequeInterval, count: number) {
        this.#arena = arena; this.#left = left; this.#right = right; this.count = count;
    }

    /**
     * An empty deque owned by an explicit incremental-ancestor arena. O(1).
     *
     * This is the extension seam: supplying a backend with different `U(M)`/`Q(M)` costs changes
     * every parameterized bound this module documents. Sharing one arena between independent deque
     * families is allowed; the arena is append-only, so their nodes interleave harmlessly. Throws
     * `RangeError` when the arena does not report depth `-1` for its bottom node, because the
     * interval arithmetic anchors an empty deque there. A rejected arena is never asked to publish.
     */
    public static empty<T>(arena: IncrementalAncestorArena<T>): BilateralAncestralDeque<T> {
        const bottom = arena.bottom;
        const depth = arena.depthOf(bottom);
        if (depth !== -1) throw new RangeError(`The arena bottom node must have depth -1 but reports depth ${depth}.`);
        const interval = emptyInterval(bottom);
        return new BilateralAncestralDeque(arena, interval, interval, 0);
    }

    /**
     * An empty deque backed by a fresh, independently collectible Myers arena. O(1).
     *
     * There is deliberately no shared global empty value: each call owns its own arena, so unrelated
     * workloads never share retained history.
     */
    public static emptyMyers<T>(): BilateralAncestralDeque<T> { return BilateralAncestralDeque.empty<T>(new MyersIncrementalAncestorArena<T>()); }

    /**
     * A Myers-backed deque holding the values in enumeration order. Theta(n) leaf additions, no
     * queries; O(n) amortized in total with the shipped arena.
     *
     * Each value is added at the back, publishing one arena node. An existing deque passed here is
     * *not* returned unchanged: the result always owns a private arena and shares no retained
     * history with its source.
     */
    public static from<T>(values: Iterable<T>): BilateralAncestralDeque<T> {
        let result = BilateralAncestralDeque.emptyMyers<T>();
        for (const value of values) result = result.addLast(value);
        return result;
    }

    /**
     * A handle built directly from already-formed interval descriptors, with no validation. O(1).
     *
     * Exposed so diagnostics and tests can assemble - or deliberately corrupt - a representation and
     * then run {@link BilateralAncestralDeque.validateStructure} over it. Normal construction goes
     * through {@link BilateralAncestralDeque.empty}, {@link BilateralAncestralDeque.emptyMyers}, or
     * {@link BilateralAncestralDeque.from}.
     */
    public static fromRepresentation<T>(arena: IncrementalAncestorArena<T>, left: BilateralAncestralDequeInterval, right: BilateralAncestralDequeInterval, count: number): BilateralAncestralDeque<T> {
        return new BilateralAncestralDeque(arena, left, right, count);
    }

    /** Whether this handle denotes an empty deque. O(1), query-free. */
    public get isEmpty(): boolean { return this.count === 0; }

    /** The shared arena this handle navigates. Exposed for diagnostics, not for mutation. O(1). */
    public get arena(): IncrementalAncestorArena<T> { return this.#arena; }

    /** The reversed left interval, whose tail holds the first visible value. O(1). */
    public get leftInterval(): BilateralAncestralDequeInterval { return this.#left; }

    /** The forward right interval, whose tail holds the last visible value. O(1). */
    public get rightInterval(): BilateralAncestralDequeInterval { return this.#right; }

    /**
     * The first visible value, read from a cached endpoint with no ancestor query. O(1).
     *
     * Throws `RangeError` when the deque is empty.
     */
    public get first(): T { this.#ensureNotEmpty(); return this.#arena.valueAt(this.#left.count !== 0 ? this.#left.tail : this.#right.base); }

    /**
     * The last visible value, read from a cached endpoint with no ancestor query. O(1).
     *
     * Throws `RangeError` when the deque is empty.
     */
    public get last(): T { this.#ensureNotEmpty(); return this.#arena.valueAt(this.#right.count !== 0 ? this.#right.tail : this.#left.base); }

    /**
     * The visible value at a zero-based index, costing at most one level-ancestor query `Q(M)`.
     *
     * The four cached endpoints - index `0`, the end of the left arm, the start of the right arm,
     * and the last index - are answered from retained handles with no query at all. Throws
     * `RangeError` when the index is not an integer inside the deque.
     */
    public getAt(index: number): T {
        if (!Number.isInteger(index) || index < 0 || index >= this.count) throw new RangeError("The index is outside the bilateral ancestral deque.");
        const left = this.#left;
        let node: number;
        if (index < left.count) {
            if (index === 0) node = left.tail;
            else if (index === left.count - 1) node = left.base;
            else node = this.#arena.ancestorAtDepth(left.tail, this.#arena.depthOf(left.tail) - index);
        } else {
            const right = this.#right;
            const rightIndex = index - left.count;
            if (rightIndex === 0) node = right.base;
            else if (rightIndex === right.count - 1) node = right.tail;
            else node = this.#arena.ancestorAtDepth(right.tail, this.#arena.depthOf(right.anchor) + rightIndex + 1);
        }
        return this.#arena.valueAt(node);
    }

    /**
     * A deque with the value added at the front. `U(M)`, no query: O(1) amortized with the shipped
     * Myers arena. This handle and every other retained version are unaffected.
     */
    public addFirst(value: T): BilateralAncestralDeque<T> {
        return new BilateralAncestralDeque(this.#arena, this.#addToTail(this.#left, value), this.#right, this.count + 1);
    }

    /**
     * A deque with the value added at the back. `U(M)`, no query: O(1) amortized with the shipped
     * Myers arena. This handle and every other retained version are unaffected.
     */
    public addLast(value: T): BilateralAncestralDeque<T> {
        return new BilateralAncestralDeque(this.#arena, this.#left, this.#addToTail(this.#right, value), this.count + 1);
    }

    /**
     * The deque without its first visible value.
     *
     * Makes no ancestor query while the left arm is nonempty, because only that arm's tail moves to
     * its parent. A removal that crosses the centre into the right arm advances that arm's anchor to
     * its cached base and costs at most one query, which selects the next cached base. Throws
     * `RangeError` when the deque is empty.
     */
    public removeFirst(): BilateralAncestralDeque<T> {
        this.#ensureNotEmpty();
        if (this.count === 1) return this.#emptyHandle();
        if (this.#left.count !== 0) return new BilateralAncestralDeque(this.#arena, this.#removeTail(this.#left), this.#right, this.count - 1);
        return new BilateralAncestralDeque(this.#arena, this.#left, this.#removeBase(this.#right), this.count - 1);
    }

    /**
     * The deque without its last visible value.
     *
     * Makes no ancestor query while the right arm is nonempty, and at most one when the removal
     * crosses the centre into the left arm. Throws `RangeError` when the deque is empty.
     */
    public removeLast(): BilateralAncestralDeque<T> {
        this.#ensureNotEmpty();
        if (this.count === 1) return this.#emptyHandle();
        if (this.#right.count !== 0) return new BilateralAncestralDeque(this.#arena, this.#left, this.#removeTail(this.#right), this.count - 1);
        return new BilateralAncestralDeque(this.#arena, this.#removeBase(this.#left), this.#right, this.count - 1);
    }

    /**
     * The first visible value with the remainder, or `undefined` when the deque is empty. Same cost
     * as {@link BilateralAncestralDeque.removeFirst}.
     *
     * Nothing is published on the empty path and this handle stays valid either way. A stored
     * `undefined` element is a present value: absence is reported only by the `undefined` result.
     */
    public tryRemoveFirst(): BilateralAncestralDequePop<T> | undefined {
        return this.count === 0 ? undefined : { value: this.first, rest: this.removeFirst() };
    }

    /**
     * The last visible value with the remainder, or `undefined` when the deque is empty. Same cost
     * as {@link BilateralAncestralDeque.removeLast}.
     */
    public tryRemoveLast(): BilateralAncestralDequePop<T> | undefined {
        return this.count === 0 ? undefined : { value: this.last, rest: this.removeLast() };
    }

    /**
     * The first `count` visible values, as a growable bilateral slice.
     *
     * A prefix is a slice, so it costs at most two ancestor queries; taking the whole length returns
     * a value sharing this handle's storage and makes none. Throws `RangeError` when `count` is not
     * an integer in `0..this.count`.
     */
    public take(count: number): BilateralAncestralDeque<T> {
        this.#checkBoundary(count);
        return count === this.count ? this : this.getRange(0, count);
    }

    /**
     * The deque after omitting its first `count` visible values.
     *
     * A suffix is a slice, so it costs at most two ancestor queries; dropping nothing returns a
     * value sharing this handle's storage and makes none. Throws `RangeError` when `count` is not an
     * integer in `0..this.count`. This is the reference contract's `Drop`.
     */
    public drop(count: number): BilateralAncestralDeque<T> {
        this.#checkBoundary(count);
        return count === 0 ? this : this.getRange(count, this.count - count);
    }

    /**
     * One contiguous, growable bilateral slice. This is the reference contract's `Slice`, named for
     * its `(index, count)` shape rather than `slice`'s `(start, end)` one.
     *
     * Every contiguous range meets each interval at most once, so the result is again a bilateral
     * handle and the operation costs **at most two** ancestor queries no matter how many values it
     * selects - including a range that crosses the centre, where the left part is necessarily a
     * suffix of the left arm and the right part a prefix of the right arm, so each side spends at
     * most one. The whole range returns a value sharing this handle's storage and an empty range a
     * fresh empty handle; neither makes a query.
     *
     * Throws `RangeError` when either argument is not a nonnegative integer, when `index` exceeds
     * the length, or when the range runs past the end. The count is validated as
     * `count > this.count - index`, so an index plus count that would run away is rejected rather
     * than wrapped.
     */
    public getRange(index: number, count: number): BilateralAncestralDeque<T> {
        this.#checkBoundary(index);
        if (!Number.isInteger(count) || count < 0) throw new RangeError("The count must be a nonnegative integer.");
        if (count > this.count - index) throw new RangeError("The range extends past the end of the bilateral ancestral deque.");
        if (index === 0 && count === this.count) return this;
        if (count === 0) return this.#emptyHandle();

        const end = index + count;
        const leftCount = this.#left.count;
        const leftStart = Math.min(index, leftCount);
        const leftEnd = Math.min(end, leftCount);
        const rightStart = Math.max(0, index - leftCount);
        const rightEnd = Math.max(0, end - leftCount);
        const left = this.#sliceLeft(this.#left, leftStart, leftEnd - leftStart);
        const right = this.#sliceRight(this.#right, rightStart, rightEnd - rightStart);
        return new BilateralAncestralDeque(this.#arena, left, right, count);
    }

    /**
     * This deque split at a zero-based boundary into a growable prefix and suffix.
     *
     * Although it is expressed as a prefix plus a suffix, the pair costs at most two ancestor queries
     * in total: a prefix that stops inside an arm spends one and its complementary suffix spends one.
     * Both endpoint boundaries return values sharing this handle's storage and make no query. Throws
     * `RangeError` when the boundary is not an integer in `0..this.count`.
     */
    public splitAt(index: number): BilateralAncestralDequeSplit<T> {
        this.#checkBoundary(index);
        return { left: this.take(index), right: this.drop(index) };
    }

    /**
     * The logical reversal, obtained by exchanging the two oriented intervals. O(1), query-free.
     *
     * No node is published, copied, or navigated, and no lazy flag is recorded. A deque of at most
     * one value is its own reversal and returns a value sharing this handle's storage.
     */
    public reverse(): BilateralAncestralDeque<T> {
        return this.count <= 1 ? this : new BilateralAncestralDeque(this.#arena, this.#right, this.#left, this.count);
    }

    /**
     * An empty, independently growable handle in the same arena. O(1), query-free.
     *
     * Clearing an already-empty deque returns a value sharing this handle's storage. Nothing is
     * reclaimed: the arena keeps every node it ever published.
     */
    public clear(): BilateralAncestralDeque<T> { return this.count === 0 ? this : this.#emptyHandle(); }

    /**
     * The visible values in front-to-back order. Theta(count) time, no queries.
     *
     * Follows parent links rather than performing `count` ancestor queries. The forward-oriented
     * right interval is walked from its tail and reversed through a buffer, which is why enumeration
     * retains O(count) temporary values.
     */
    public toArray(): T[] {
        const values: T[] = [];
        let node = this.#left.tail;
        for (let remaining = this.#left.count; remaining > 0; remaining -= 1) {
            values.push(this.#arena.valueAt(node));
            if (remaining !== 1) node = this.#arena.parentOf(node);
        }
        for (const value of this.#rightValues()) values.push(value);
        return values;
    }

    /**
     * Iterates the visible values front to back. Theta(count) time, no queries.
     *
     * The reversed left interval streams through parent links with constant iterator state; the
     * forward right interval is buffered when it is first reached, so the iterator can retain
     * O(count) values. Both paths are captured from this handle, so a later branch is never observed.
     */
    public *[Symbol.iterator](): IterableIterator<T> {
        let node = this.#left.tail;
        for (let remaining = this.#left.count; remaining > 0; remaining -= 1) {
            yield this.#arena.valueAt(node);
            if (remaining !== 1) node = this.#arena.parentOf(node);
        }
        for (const value of this.#rightValues()) yield value;
    }

    /** Whether both handles navigate the same arena, so their histories can interleave. O(1). */
    public sharesArenaWith(other: BilateralAncestralDeque<T>): boolean { return this.#arena === other.#arena; }

    /**
     * Whether both handles denote the same arena nodes through identical intervals. O(1).
     *
     * A representation test, not an equality test. The reference asserts reference identity for the
     * operations that return their receiver; a handle produced here by a no-op is literally the
     * receiver, and this predicate additionally recognizes a handle rebuilt with the same cached
     * interval descriptors as sharing exactly the same storage.
     */
    public sharesStorageWith(other: BilateralAncestralDeque<T>): boolean {
        return this.#arena === other.#arena && this.count === other.count && sameInterval(this.#left, other.#left) && sameInterval(this.#right, other.#right);
    }

    /**
     * Audits the cached interval endpoints and counts, and reports what was measured.
     *
     * Checks that the total count agrees with the two interval counts, that an empty interval has one
     * shared endpoint, that each nonempty interval's count matches its endpoint depths, and that its
     * cached base really is the first node after its anchor on the tail's ancestry path. Confirming
     * that path costs exactly **two** ancestor queries per nonempty interval, a ceiling of four, so
     * the audit is `O(1 + Q(M))` rather than O(1) work; an empty handle spends none. Ports the
     * reference's internal `ValidateInvariants`. Throws `Error` on the first violation. A defensive
     * audit, not part of normal use.
     */
    public validateStructure(): BilateralAncestralDequeStatistics {
        if (this.count < 0 || this.#left.count < 0 || this.#right.count < 0) throw new Error("A bilateral ancestral deque count is negative.");
        if (this.count !== this.#left.count + this.#right.count) throw new Error("The total count disagrees with the interval counts.");
        const leftAnchorDepth = this.#validateInterval(this.#left);
        const rightAnchorDepth = this.#validateInterval(this.#right);
        return {
            count: this.count,
            leftCount: this.#left.count,
            rightCount: this.#right.count,
            leftAnchorDepth,
            rightAnchorDepth,
            arenaPublishedNodeCount: this.#arena.publishedNodeCount,
        };
    }

    /** Checks one interval and returns its anchor depth, spending two queries when nonempty. */
    #validateInterval(segment: BilateralAncestralDequeInterval): number {
        if (segment.count === 0) {
            if (segment.anchor !== segment.base || segment.base !== segment.tail) throw new Error("An empty interval does not have one shared endpoint.");
            return this.#arena.depthOf(segment.anchor);
        }
        const anchorDepth = this.#arena.depthOf(segment.anchor);
        const tailDepth = this.#arena.depthOf(segment.tail);
        if (tailDepth - anchorDepth !== segment.count) throw new Error("An interval count disagrees with its endpoint depths.");
        if (this.#arena.parentOf(segment.base) !== segment.anchor) throw new Error("The cached base is not the first node after the anchor.");
        if (this.#arena.ancestorAtDepth(segment.tail, anchorDepth) !== segment.anchor) throw new Error("The interval anchor is not an ancestor of its tail.");
        if (this.#arena.ancestorAtDepth(segment.tail, anchorDepth + 1) !== segment.base) throw new Error("The cached base is not on the tail's ancestry path.");
        return anchorDepth;
    }

    /** The right arm's values in front-to-back order, walked from its tail through parent links. */
    #rightValues(): T[] {
        const values: T[] = [];
        let node = this.#right.tail;
        for (let remaining = this.#right.count; remaining > 0; remaining -= 1) {
            values.push(this.#arena.valueAt(node));
            if (remaining !== 1) node = this.#arena.parentOf(node);
        }
        values.reverse();
        return values;
    }

    /** An empty handle anchored at this arena's bottom node. */
    #emptyHandle(): BilateralAncestralDeque<T> {
        const interval = emptyInterval(this.#arena.bottom);
        return new BilateralAncestralDeque(this.#arena, interval, interval, 0);
    }

    /** Publishes one leaf below an interval's tail and extends the interval by one value. */
    #addToTail(segment: BilateralAncestralDequeInterval, value: T): BilateralAncestralDequeInterval {
        const tail = this.#arena.addLeaf(segment.tail, value);
        return { anchor: segment.anchor, base: segment.count === 0 ? tail : segment.base, tail, count: segment.count + 1 };
    }

    /** Drops an interval's newest label by moving its tail to the parent. No queries. */
    #removeTail(segment: BilateralAncestralDequeInterval): BilateralAncestralDequeInterval {
        if (segment.count === 0) throw new Error("An empty interval has no tail label to drop.");
        const tail = this.#arena.parentOf(segment.tail);
        if (segment.count === 1) return emptyInterval(tail);
        return { anchor: segment.anchor, base: segment.base, tail, count: segment.count - 1 };
    }

    /**
     * Drops an interval's oldest label by advancing its anchor to the cached base.
     *
     * Costs at most one ancestor query, which selects the next cached base at absolute depth
     * `depthOf(tail) - count + 2`. A two-value interval needs no query, because the surviving value
     * is already the retained tail.
     */
    #removeBase(segment: BilateralAncestralDequeInterval): BilateralAncestralDequeInterval {
        if (segment.count === 0) throw new Error("An empty interval has no base label to drop.");
        if (segment.count === 1) return emptyInterval(segment.tail);
        const base = segment.count === 2
            ? segment.tail
            : this.#arena.ancestorAtDepth(segment.tail, this.#arena.depthOf(segment.tail) - segment.count + 2);
        return { anchor: segment.base, base, tail: segment.tail, count: segment.count - 1 };
    }

    /**
     * Selects the sub-interval `[start, start + count)` of a reversed left arm.
     *
     * The reversed reading order means `start` counts down from the tail. At most two queries, and
     * fewer whenever a cached endpoint already answers.
     */
    #sliceLeft(segment: BilateralAncestralDequeInterval, start: number, count: number): BilateralAncestralDequeInterval {
        if (start < 0 || count < 0 || start > segment.count - count) throw new Error("A left sub-interval must lie inside its interval.");
        if (count === 0) return emptyInterval(this.#arena.bottom);
        if (start === 0 && count === segment.count) return segment;

        const tailDepth = this.#arena.depthOf(segment.tail);
        const baseDepth = tailDepth - segment.count + 1;
        const slicedTailDepth = tailDepth - start;
        const slicedBaseDepth = slicedTailDepth - count + 1;
        const tail = slicedTailDepth === tailDepth ? segment.tail : this.#arena.ancestorAtDepth(segment.tail, slicedTailDepth);
        let base: number;
        if (slicedBaseDepth === slicedTailDepth) base = tail;
        else if (slicedBaseDepth === baseDepth) base = segment.base;
        else base = this.#arena.ancestorAtDepth(segment.tail, slicedBaseDepth);
        const anchor = base === segment.base ? segment.anchor : this.#arena.parentOf(base);
        return { anchor, base, tail, count };
    }

    /**
     * Selects the sub-interval `[start, start + count)` of a forward right arm. At most two queries,
     * and fewer whenever a cached endpoint already answers.
     */
    #sliceRight(segment: BilateralAncestralDequeInterval, start: number, count: number): BilateralAncestralDequeInterval {
        if (start < 0 || count < 0 || start > segment.count - count) throw new Error("A right sub-interval must lie inside its interval.");
        if (count === 0) return emptyInterval(this.#arena.bottom);
        if (start === 0 && count === segment.count) return segment;

        const anchorDepth = this.#arena.depthOf(segment.anchor);
        const slicedBaseDepth = anchorDepth + start + 1;
        const slicedTailDepth = slicedBaseDepth + count - 1;
        const end = start + count;
        const base = start === 0 ? segment.base : this.#arena.ancestorAtDepth(segment.tail, slicedBaseDepth);
        let tail: number;
        if (slicedTailDepth === slicedBaseDepth) tail = base;
        else if (end === segment.count) tail = segment.tail;
        else tail = this.#arena.ancestorAtDepth(segment.tail, slicedTailDepth);
        const anchor = start === 0 ? segment.anchor : this.#arena.parentOf(base);
        return { anchor, base, tail, count };
    }

    #ensureNotEmpty(): void { if (this.count === 0) throw new RangeError("The bilateral ancestral deque is empty."); }

    #checkBoundary(index: number): void {
        if (!Number.isInteger(index) || index < 0 || index > this.count) throw new RangeError("The boundary is outside the bilateral ancestral deque.");
    }
}

/** Field-by-field interval comparison, used by the representation-sharing predicate. */
function sameInterval(left: BilateralAncestralDequeInterval, right: BilateralAncestralDequeInterval): boolean {
    return left.anchor === right.anchor && left.base === right.base && left.tail === right.tail && left.count === right.count;
}
