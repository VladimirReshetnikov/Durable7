/**
 * Fixed-length persistent vector carrying a checkpoint and an exact maximal-run change index.
 *
 * `PersistentRunDeltaVector` keeps two logical views of one fixed-length sequence - the current
 * values and a checkpoint - plus a persistent ordered map holding one `start -> endExclusive`
 * record for every *maximal* run of positions whose values differ. Dirtiness is relative to a
 * retained {@link ValueEquality} relation, not to write history: writing a relation-equal value is
 * a semantic no-op, and returning a position to its checkpoint equivalence class cancels that
 * position's delta and restores the *exact* checkpoint representative.
 *
 * Let `n` be the fixed length, `k` the number of dirty positions, and `r` the number of maximal
 * dirty runs, so `0 <= r <= k <= n`. Against an unindexed pair of current/checkpoint roots the one
 * strict result is that exact dirty-run descriptor discovery is output-optimal `Theta(r)` rather
 * than worst-case `Theta(n)`, and no common operation loses the baseline's asymptotic time or
 * space. The gap is unbounded: one contiguous changed block gives `k = n` and `r = 1`. Emitting the
 * changed payload *values* is still `Omega(k)` and is not claimed to be faster. This is not a new
 * interval algorithm nor a new persistent-array technique; the run index is an application of the
 * Discrete Interval Encoding Tree idea over an ordinary persistent vector.
 *
 * Length is fixed at construction. There is deliberately no insertion, deletion, concatenation,
 * split, range assignment, rebase, or merge of unrelated branches, because checkpoint
 * correspondence would need a position-transformation policy this abstract data type does not
 * define. There is one checkpoint per version.
 *
 * ## Bounds
 *
 * Both roots are this workspace's {@link RrbVector}, a real eager 32-way relaxed radix-balanced
 * tree: it defers nothing and copies exactly the path it touches, so every root bound below is
 * **worst case, not amortized**. The run index is {@link SortedMap} over the lazy Hinze-Paterson
 * measured sequence, whose ordered *reads* (`floorEntry`, `ceilingEntry`, `entryAt`) are
 * `O(log(r + 1))` but whose *writes* go through the sequence's `O(log(min(n, m)))` **amortized**
 * concatenation, so run-index writes are amortized - a bound the memoized suspensions make valid
 * under persistent branching, not merely under ephemeral linear use. The two substrates are stated
 * separately below rather than blurred into one figure.
 *
 * | Operation | Bound |
 * | --- | --- |
 * | Build `n` values | `Theta(n)` |
 * | `size`, `isEmpty`, `dirtyCount`, `dirtyRunCount`, `hasChanges`, `isClean` | `O(1)` |
 * | Read a current or checkpoint value | `O(log n)` worst case |
 * | Point edit (`setItem`, `resetItem`) | `O(log n)` worst-case root work plus `O(log(r + 1))` amortized index work |
 * | Whole `checkpoint` or `rollback` | `O(1)` |
 * | Dirty membership, run containing a position, run by rank | `O(log(r + 1))` |
 * | Exact dirty-run descriptors | `Theta(r)`, output-optimal |
 * | Accept or revert one indexed run | `O(log n)` worst-case splice plus `O(log(r + 1))` amortized index removal, independent of the run's length |
 * | Enumerate current values | `Theta(n)` |
 * | `validateStructure` | `O(n log n)` |
 *
 * The bound this port does *not* inherit is a global height guarantee. `RrbVector.concat`
 * rebalances only the seam between its operands rather than imposing a global occupancy rule, so a
 * root's height is `O(log32 n)` for builder-built roots and grows by at most one per
 * concatenation - at most two per run splice. Every `O(log n)` above is really `O(h)` in that
 * height, which is the substrate's own claim and not a stronger one.
 *
 * Accepting or reverting one run is four `splitAt` calls and two `concat` calls over the RRB roots.
 * It never walks the run's positions and makes **no** value-relation call at all, which is what
 * makes it independent of the run's length. That splice bound is inherited from the substrate and
 * is not itself a novelty claim.
 *
 * ## Reference identity
 *
 * The cleanliness invariant is "a clean position reuses its *exact* checkpoint representative",
 * which needs object identity rather than value equality: the retained relation may be coarser or
 * finer than `===`, so it cannot decide representative identity. C# uses a private `Cell` class
 * compared with `ReferenceEquals`, Rust retains an `Arc` compared with `Arc::ptr_eq`, and the
 * Kotlin, C++, OCaml, and Python ports box values in an identity-bearing cell. JavaScript's `===`
 * and `Object.is` are *value* equality on numbers, strings, booleans, and every other primitive -
 * exactly the payload types a test naturally reaches for - so an identity invariant stated over raw
 * payloads would be silently vacuous. This port therefore boxes every element in a private `Cell`
 * and states the invariant over cells, exposing {@link PersistentRunDeltaVector.sharesRepresentativeAt}
 * so a test can see the difference between restoring the exact checkpoint representative and
 * storing a merely relation-equal value.
 *
 * That choice also interlocks with the substrate. `RrbVector.setItem` short-circuits on
 * `Object.is`, not on a caller-supplied equality: its leaf writer keeps the existing node when
 * `Object.is(node.items[index], value)`. It applies no value-equality no-op rule, so it can never
 * silently keep an *equal but different* object in place of the one written - but with raw payloads
 * that identity test would fire for equal primitives and make the diagnostic meaningless. Boxing
 * turns that short circuit into precisely the identity test this structure wants: a fresh cell
 * never matches, and a restored checkpoint cell matches only when it is literally already in place.
 *
 * ## Value equivalence
 *
 * C# takes an `IEqualityComparer<T>`; the relation is retained here as a plain
 * {@link ValueEquality} callable, the value-side counterpart of `Comparator`. The caller's
 * obligation is that the relation be a genuine **equivalence relation** - reflexive, symmetric, and
 * transitive - over every value the vector will hold, and that it stay stable for every retained
 * version's lifetime. A non-reflexive relation silently corrupts run accounting: a position
 * rewritten with its own value is recorded as changed, leaving a phantom run that
 * {@link PersistentRunDeltaVector.validateStructure} cannot detect as false, because the phantom is
 * consistent with everything the relation says.
 *
 * Floats are the inherited trap. .NET's default comparer is reflexive on `NaN` (`double.Equals`
 * treats any two `NaN` as equal), while a naive `left === right` is not: `NaN === NaN` is `false`.
 * JavaScript nevertheless has a reflexive built-in that a naive port would miss:
 *
 * - {@link naturalValueEquality}, the default, is `Object.is` - SameValue. It is reflexive on
 *   *every* JavaScript value including `NaN`, so it is always a true equivalence relation. It also
 *   distinguishes `-0` from `+0`, which is a genuinely observable distinction in JavaScript
 *   (`1 / -0` is `-Infinity`), so recording a `+0 -> -0` write as a change is truthful rather than a
 *   defect. This is stricter than .NET's default comparer, which puts the signed zeros in one class.
 * - {@link reflexiveIeeeEquality} is the canonical IEEE-reflexive relation, the counterpart of
 *   Rust's `EqualityPolicy::reflexive_ieee` - SameValueZero. It puts every `NaN` in one class and
 *   `-0` with `+0`, reproducing .NET's default comparer for floating-point payloads exactly. Pass it
 *   for `number` payloads where the signed zeros must not count as a change.
 *
 * Neither helper rescues a `NaN` nested inside a payload it cannot see into; a caller whose values
 * contain floats must supply a relation that is reflexive on those values.
 *
 * Every relation call in an edit happens before any successor root is built, so a throwing relation
 * publishes no partial version: the receiver and every retained branch stay valid and unchanged.
 * `checkpoint`, `rollback`, `dirtyRuns`, and every accept or revert of an indexed run invoke no
 * relation callback at all.
 *
 * ## Divergences from the C# baseline
 *
 * - Values are boxed in an identity-bearing private cell, as in every sibling port. See "Reference
 *   identity" above for why JavaScript's `Object.is` on raw payloads cannot carry the invariant.
 * - The default relation is `Object.is` rather than .NET's `EqualityComparer<T>.Default`. The
 *   reflexivity that makes .NET's default safe is reproduced; the signed-zero class is not, and
 *   {@link reflexiveIeeeEquality} is supplied for callers who want it.
 * - `TryGetDirtyRunContaining` becomes {@link PersistentRunDeltaVector.dirtyRunContaining} returning
 *   `PersistentDirtyRun | undefined`, since a run is never the zero-length default and JavaScript
 *   has no `out` parameter. An out-of-range position still throws, so a clean position stays
 *   distinguishable from an invalid one.
 * - {@link PersistentRunDeltaVector.resetItem} compares the current value against the checkpoint
 *   value directly instead of delegating to `setItem`, following the Rust and Python ports. C#'s
 *   delegation makes a second relation call asking whether the checkpoint value equals itself;
 *   under the required reflexive relation the results are identical, and the direct form does not
 *   depend on reflexivity to terminate a reset.
 * - C# documents `OverflowException` past `int.MaxValue` positions. The substrate's own
 *   `Number.MAX_SAFE_INTEGER` guard governs here, so that contract is not restated.
 * - `EnumerateDirtyRuns` returns a lazy single-pass iterator rather than a repeatable
 *   `IEnumerable`. Every version is immutable, so calling it again yields the same sequence.
 * - `ValidateInvariants` becomes the public
 *   {@link PersistentRunDeltaVector.validateStructure} throwing `Error`, matching this workspace's
 *   convention that `Error` reports an internal invariant violation while `RangeError` reports an
 *   out-of-range position or rank.
 */
import { defaultComparator } from "./ordering.js";
import { RrbVector, type RrbVectorSplit } from "./rrb-vector.js";
import { SortedMap } from "./sorted.js";

/**
 * A value-equality relation deciding checkpoint-relative sameness.
 *
 * The caller's obligation is that the relation be a true equivalence relation - reflexive,
 * symmetric, and transitive - over every value a version will hold, and stable for that version's
 * lifetime. Reflexivity is the load-bearing half: a relation that says a value differs from itself
 * records a phantom run that no invariant check can detect.
 */
export type ValueEquality<T> = (left: T, right: T) => boolean;

/**
 * SameValue equality, the default relation. O(1).
 *
 * `Object.is` is reflexive on every JavaScript value including `NaN`, so it is always a true
 * equivalence relation, unlike a naive `===`. It distinguishes `-0` from `+0`; use
 * {@link reflexiveIeeeEquality} when the signed zeros must share one class.
 */
export function naturalValueEquality<T>(left: T, right: T): boolean {
    return Object.is(left, right);
}

/**
 * SameValueZero equality, the canonical IEEE-reflexive relation for floating-point payloads. O(1).
 *
 * Every `NaN` lands in one class and `-0` lands with `+0`, reproducing .NET's default comparer for
 * `double` exactly and matching Rust's `EqualityPolicy::reflexive_ieee`. On non-numeric payloads it
 * degenerates to `===`, which is reference identity for objects.
 */
export function reflexiveIeeeEquality<T>(left: T, right: T): boolean {
    return left === right || (left !== left && right !== right);
}

/** Shape measurements returned by a successful structural audit. */
export interface RunDeltaVectorStatistics {
    /** Number of positions. */
    readonly count: number;
    /** Number of positions differing from the checkpoint, recounted from the run index. */
    readonly dirtyCount: number;
    /** Number of maximal dirty runs. */
    readonly dirtyRunCount: number;
    /** How many positions hold the very same cell in both roots. */
    readonly sharedRepresentativeCount: number;
    /** Whether the version records no change and reuses the exact checkpoint root. */
    readonly isClean: boolean;
}

/**
 * One maximal half-open run of checkpoint-relative changes.
 *
 * A run covers the positions `start .. start + length` and is never empty. The runs published by one
 * version are ordered, non-overlapping, non-adjacent, and maximal.
 */
export class PersistentDirtyRun {
    /** The zero-based first changed position. */
    public readonly start: number;
    /** The number of consecutive changed positions. */
    public readonly length: number;
    /** Builds a run descriptor covering `length` positions from `start`. */
    public constructor(start: number, length: number) {
        if (!Number.isInteger(start) || !Number.isInteger(length) || start < 0 || length <= 0) {
            throw new RangeError("A dirty run starts at a non-negative position and is never empty.");
        }
        this.start = start;
        this.length = length;
    }
    /** The exclusive end position. */
    public get endExclusive(): number { return this.start + this.length; }
    /** Whether the position lies inside this run. */
    public contains(index: number): boolean { return index >= this.start && index < this.endExclusive; }
    /** The half-open interval in mathematical notation. */
    public toString(): string { return `[${this.start}, ${this.endExclusive})`; }
}

/**
 * One boxed element slot, identified by object identity rather than by value.
 *
 * Boxing is what keeps "a clean position reuses its exact checkpoint representative" a real
 * statement: `Object.is` on two primitives is value equality, so the invariant would be vacuous for
 * exactly the payloads a caller reaches for first. Two cells are the same only when they are
 * literally the same object.
 */
class Cell<T> {
    public readonly value: T;
    public constructor(value: T) { this.value = value; }
}

function emptyRuns(): SortedMap<number, number> { return SortedMap.empty<number, number>(defaultComparator); }

function toRun(start: number, endExclusive: number): PersistentDirtyRun { return new PersistentDirtyRun(start, endExclusive - start); }

function cellAt<T>(root: RrbVector<Cell<T>>, position: number): Cell<T> {
    const cell = root.get(position);
    if (cell === undefined) throw new Error("A validated position is absent from an RRB root.");
    return cell;
}

function requireRoot<T>(root: RrbVector<Cell<T>> | undefined): RrbVector<Cell<T>> {
    if (root === undefined) throw new Error("A validated position was rejected by an RRB root.");
    return root;
}

function requireSplit<T>(split: RrbVectorSplit<Cell<T>> | undefined): RrbVectorSplit<Cell<T>> {
    if (split === undefined) throw new Error("A run boundary is not a valid split position.");
    return split;
}

/**
 * Records the position as dirty, merging the runs on both sides, extending one side, or inserting a
 * fresh singleton run.
 *
 * The result stays ordered, non-overlapping, non-adjacent, and maximal, and at most two records
 * change. The left record is overwritten in place rather than removed and reinserted, because
 * `setItem` replaces a present key.
 */
function addDirtyPosition(runs: SortedMap<number, number>, index: number): SortedMap<number, number> {
    const floor = runs.floorEntry(index);
    const left = floor !== undefined && floor.value === index ? floor : undefined;
    const ceiling = runs.ceilingEntry(index);
    const right = ceiling !== undefined && ceiling.key === index + 1 ? ceiling : undefined;
    if (left !== undefined && right !== undefined) return runs.remove(right.key).setItem(left.key, right.value);
    if (left !== undefined) return runs.setItem(left.key, index + 1);
    if (right !== undefined) return runs.remove(right.key).setItem(index, right.value);
    return runs.setItem(index, index + 1);
}

/**
 * Clears the position, deleting, shrinking at either edge, or splitting its unique containing run.
 *
 * Throws when the position is absent from the run index, which would mean the maintained invariant
 * had already been broken.
 */
function removeDirtyPosition(runs: SortedMap<number, number>, index: number): SortedMap<number, number> {
    const containing = runs.floorEntry(index);
    if (containing === undefined || containing.value <= index) {
        throw new Error("The cleared dirty position is absent from the run index.");
    }
    const start = containing.key;
    const end = containing.value;
    if (start === index && end === index + 1) return runs.remove(start);
    if (start === index) return runs.remove(start).setItem(index + 1, end);
    if (end === index + 1) return runs.setItem(start, index);
    return runs.setItem(start, index).setItem(index + 1, end);
}

/**
 * Splices the source's run slice over the destination's, sharing every untouched subtree.
 *
 * Four splits and two seam concatenations, so the cost is O(log n) worst case regardless of the
 * run's length, and no element is inspected or compared on the way.
 */
function replaceRange<T>(
    destination: RrbVector<Cell<T>>,
    source: RrbVector<Cell<T>>,
    run: PersistentDirtyRun,
): RrbVector<Cell<T>> {
    if (run.start === 0 && run.length === destination.size) return source;
    const destinationSplit = requireSplit(destination.splitAt(run.start));
    const right = requireSplit(destinationSplit.right.splitAt(run.length)).right;
    const sourceTail = requireSplit(source.splitAt(run.start)).right;
    const middle = requireSplit(sourceTail.splitAt(run.length)).left;
    return destinationSplit.left.concat(middle).concat(right);
}

/**
 * A fixed-length persistent vector with a checkpoint and an exact maximal-run change index.
 *
 * See the module documentation for the representation, the shipped bounds, the reference-identity
 * rule, and the equivalence-relation contract. Every version is immutable and every producing
 * operation leaves its receiver - and every other retained version - valid and unchanged. A
 * semantic no-op returns the receiver itself.
 */
export class PersistentRunDeltaVector<T> implements Iterable<T> {
    readonly #current: RrbVector<Cell<T>>;
    readonly #checkpoint: RrbVector<Cell<T>>;
    readonly #dirtyRuns: SortedMap<number, number>;
    readonly #dirtyCount: number;
    /** The retained relation defining checkpoint-relative value equality. */
    public readonly valueEquals: ValueEquality<T>;
    private constructor(
        current: RrbVector<Cell<T>>,
        checkpoint: RrbVector<Cell<T>>,
        dirtyRuns: SortedMap<number, number>,
        dirtyCount: number,
        valueEquals: ValueEquality<T>,
    ) {
        this.#current = current;
        this.#checkpoint = checkpoint;
        this.#dirtyRuns = dirtyRuns;
        this.#dirtyCount = dirtyCount;
        this.valueEquals = valueEquals;
    }
    /** The clean empty vector, retaining the supplied relation by identity. O(1). */
    public static empty<T>(valueEquals: ValueEquality<T> = naturalValueEquality): PersistentRunDeltaVector<T> {
        const root = RrbVector.empty<Cell<T>>();
        return new PersistentRunDeltaVector<T>(root, root, emptyRuns(), 0, valueEquals);
    }
    /**
     * Build a clean fixed-length vector from one pass over the values. Theta(n).
     *
     * Both roots start as the same root, so the vector begins with no dirty runs and no relation
     * callback is invoked.
     */
    public static from<T>(values: Iterable<T>, valueEquals: ValueEquality<T> = naturalValueEquality): PersistentRunDeltaVector<T> {
        const builder = RrbVector.builder<Cell<T>>();
        for (const value of values) builder.append(new Cell(value));
        const root = builder.toImmutable();
        return new PersistentRunDeltaVector<T>(root, root, emptyRuns(), 0, valueEquals);
    }
    /** The fixed number of positions. O(1). */
    public get size(): number { return this.#current.size; }
    /** Whether the vector has no positions. O(1). */
    public get isEmpty(): boolean { return this.#current.isEmpty; }
    /** The number of positions that differ from the checkpoint. O(1). */
    public get dirtyCount(): number { return this.#dirtyCount; }
    /** The number of maximal dirty runs. O(1). */
    public get dirtyRunCount(): number { return this.#dirtyRuns.size; }
    /** Whether at least one current value differs from its checkpoint value. O(1). */
    public get hasChanges(): boolean { return this.#dirtyCount !== 0; }
    /**
     * Whether this version records no change *and* reuses the exact checkpoint root. O(1).
     *
     * The two halves cannot come apart: a version whose last dirty position clears snaps its current
     * root back to the checkpoint root, and {@link validateStructure} rejects any version where they
     * have.
     */
    public get isClean(): boolean { return this.#dirtyRuns.isEmpty && this.#current.sharesRootWith(this.#checkpoint); }
    /**
     * The current value at a zero-based position. O(log n) worst case.
     *
     * Throws `RangeError` outside the vector; negative positions are outside it.
     */
    public at(index: number): T { return cellAt(this.#current, this.#position(index)).value; }
    /**
     * The checkpoint value at a zero-based position. O(log n) worst case.
     *
     * Throws `RangeError` outside the vector.
     */
    public checkpointAt(index: number): T { return cellAt(this.#checkpoint, this.#position(index)).value; }
    /** Copy the current values into an array, in positional order. Theta(n). */
    public toArray(): T[] { return Array.from(this); }
    /** Copy the checkpoint values into an array, in positional order. Theta(n). */
    public checkpointToArray(): T[] { const values: T[] = []; for (const cell of this.#checkpoint) values.push(cell.value); return values; }
    /**
     * Whether a position differs from its checkpoint value. O(log(r + 1)).
     *
     * Throws `RangeError` outside the vector.
     */
    public isDirty(index: number): boolean { return this.#findRun(this.#position(index)) !== undefined; }
    /**
     * The maximal dirty run containing a position, or `undefined` when that position is clean.
     * O(log(r + 1)).
     *
     * Throws `RangeError` outside the vector, so a clean position stays distinguishable from an
     * invalid one.
     */
    public dirtyRunContaining(index: number): PersistentDirtyRun | undefined { return this.#findRun(this.#position(index)); }
    /**
     * The maximal dirty run at a zero-based ascending-start rank. O(log(r + 1)).
     *
     * Throws `RangeError` outside the run index.
     */
    public dirtyRunAt(rank: number): PersistentDirtyRun {
        const entry = this.#dirtyRuns.entryAt(this.#rank(rank));
        if (entry === undefined) throw new Error("A validated dirty-run rank is absent from the run index.");
        return toRun(entry.key, entry.value);
    }
    /**
     * Iterate the maximal dirty runs in ascending position order.
     *
     * Output-optimal Theta(r): the cost depends on the number of runs, not on how many positions
     * they cover. Invokes no value-relation callback. The walk is lazy, so abandoning it early skips
     * the rest.
     */
    public *dirtyRuns(): IterableIterator<PersistentDirtyRun> {
        for (const entry of this.#dirtyRuns) yield toRun(entry.key, entry.value);
    }
    /**
     * A version with the current value at a position replaced. O(log n) worst-case root work plus
     * O(log(r + 1)) amortized index work.
     *
     * A relation-equal replacement is a semantic no-op and returns **the receiver itself**, sharing
     * every root and changing no dirty or run accounting. A replacement landing back in the
     * checkpoint's equivalence class cancels that position's delta and restores the exact checkpoint
     * representative; when the last dirty position clears, the current root snaps back to the
     * checkpoint root. Otherwise the position becomes or stays dirty and at most two neighbouring
     * run records change.
     *
     * Throws `RangeError` outside the vector. Every relation call happens before any successor root
     * is built, so a throwing relation publishes no partial version.
     */
    public setItem(index: number, value: T): PersistentRunDeltaVector<T> {
        const position = this.#position(index);
        const current = cellAt(this.#current, position);
        if (this.valueEquals(current.value, value)) return this;
        const checkpoint = cellAt(this.#checkpoint, position);
        const remainsDirty = !this.valueEquals(checkpoint.value, value);
        return this.#replaceCell(position, remainsDirty ? new Cell(value) : checkpoint, remainsDirty);
    }
    /**
     * A version whose current value at a position is reset to its checkpoint value. O(log n)
     * worst-case root work plus O(log(r + 1)) amortized index work.
     *
     * An already clean position is a no-op returning the receiver itself. A cancelled position
     * recovers the exact checkpoint representative, not merely a relation-equal value.
     *
     * Throws `RangeError` outside the vector.
     */
    public resetItem(index: number): PersistentRunDeltaVector<T> {
        const position = this.#position(index);
        const current = cellAt(this.#current, position);
        const checkpoint = cellAt(this.#checkpoint, position);
        if (this.valueEquals(current.value, checkpoint.value)) return this;
        return this.#replaceCell(position, checkpoint, false);
    }
    /**
     * Accept every current value as a new checkpoint. O(1).
     *
     * An already clean version returns the receiver itself; otherwise the result is a distinct clean
     * version sharing the receiver's current root. No relation callback is invoked either way.
     */
    public checkpoint(): PersistentRunDeltaVector<T> {
        if (!this.hasChanges) return this;
        return new PersistentRunDeltaVector<T>(this.#current, this.#current, emptyRuns(), 0, this.valueEquals);
    }
    /**
     * Revert every current value to the checkpoint. O(1).
     *
     * An already clean version returns the receiver itself; otherwise the result is a distinct clean
     * version sharing the receiver's checkpoint root. No relation callback is invoked either way.
     */
    public rollback(): PersistentRunDeltaVector<T> {
        if (!this.hasChanges) return this;
        return new PersistentRunDeltaVector<T>(this.#checkpoint, this.#checkpoint, emptyRuns(), 0, this.valueEquals);
    }
    /**
     * Accept the maximal dirty run at a rank into the checkpoint. O(log n) worst-case splice plus
     * O(log(r + 1)) amortized index removal.
     *
     * Only the selected run becomes clean; every other run is untouched. Implemented by structural
     * splitting and concatenation of the RRB roots, so the splice costs O(log n) *independently of
     * that run's length* and makes no value-relation call at all.
     *
     * Throws `RangeError` outside the run index.
     */
    public acceptDirtyRunAt(rank: number): PersistentRunDeltaVector<T> { return this.#acceptRun(this.dirtyRunAt(rank)); }
    /**
     * Revert the maximal dirty run at a rank to the checkpoint. O(log n) worst-case splice plus
     * O(log(r + 1)) amortized index removal.
     *
     * The mirror of {@link acceptDirtyRunAt}, with the same splice cost and the same absence of
     * value-relation calls.
     */
    public revertDirtyRunAt(rank: number): PersistentRunDeltaVector<T> { return this.#revertRun(this.dirtyRunAt(rank)); }
    /**
     * Accept the maximal dirty run *containing a position* into the checkpoint. O(log n) worst-case
     * splice plus O(log(r + 1)) amortized index work.
     *
     * The argument is a zero-based position, not a run rank, so a descriptor obtained from
     * {@link dirtyRunContaining} can be acted on without rediscovering its rank. A clean position is
     * vacuous rather than an error: the receiver itself comes back, sharing every root and
     * consulting no relation. A dirty position behaves exactly as {@link acceptDirtyRunAt} on the
     * containing run.
     *
     * Throws `RangeError` outside the vector.
     */
    public acceptDirtyRunContaining(index: number): PersistentRunDeltaVector<T> {
        const run = this.#findRun(this.#position(index));
        return run === undefined ? this : this.#acceptRun(run);
    }
    /**
     * Revert the maximal dirty run *containing a position* to the checkpoint. O(log n) worst-case
     * splice plus O(log(r + 1)) amortized index work.
     *
     * The mirror of {@link acceptDirtyRunContaining}, with the same addressing rule, the same splice
     * cost, and the same absence of value-relation calls.
     */
    public revertDirtyRunContaining(index: number): PersistentRunDeltaVector<T> {
        const run = this.#findRun(this.#position(index));
        return run === undefined ? this : this.#revertRun(run);
    }
    /**
     * Whether two versions are backed by the same current root. O(1).
     *
     * A representation test, not an equality test. Two empty vectors always share, so a negative
     * control needs non-empty roots, and comparing a version with itself proves nothing.
     */
    public sharesCurrentRootWith(other: PersistentRunDeltaVector<T>): boolean { return this.#current.sharesRootWith(other.#current); }
    /**
     * Whether two versions are backed by the same checkpoint root. O(1).
     *
     * A representation test, not an equality test; see {@link sharesCurrentRootWith}.
     */
    public sharesCheckpointRootWith(other: PersistentRunDeltaVector<T>): boolean { return this.#checkpoint.sharesRootWith(other.#checkpoint); }
    /**
     * Whether this version's current and checkpoint roots hold the *same cell* at a position.
     * O(log n).
     *
     * This is the diagnostic behind the cleanliness invariant, and the only way to see the
     * difference between restoring the exact checkpoint representative and storing a merely
     * relation-equal value: `Object.is` on the payloads themselves is value equality for primitives,
     * but cells are never shared between distinct writes.
     *
     * Throws `RangeError` outside the vector.
     */
    public sharesRepresentativeAt(index: number): boolean {
        const position = this.#position(index);
        return cellAt(this.#current, position) === cellAt(this.#checkpoint, position);
    }
    /**
     * How many leaf nodes this version's two roots have in common by object identity.
     *
     * A representation measurement used to show that a run splice reused untouched subtrees rather
     * than rebuilding them. Theta(n) in the number of nodes, so it is a diagnostic and not part of
     * any operation's cost.
     */
    public sharedLeafCount(): number { return this.#current.sharedLeafCount(this.#checkpoint); }
    /**
     * Walk the whole representation independently and return its measurements.
     *
     * Checks both RRB roots against their own structural audit, that a version with no runs is
     * canonicalized onto its checkpoint root, that the run records are ordered, non-overlapping,
     * non-adjacent, in range, and non-empty, that their lengths sum to the cached dirty cardinality,
     * that every position outside every run reuses its exact checkpoint representative, and that
     * every position inside a run really is relation-unequal to its checkpoint value. The last two
     * together establish maximality: a dirty position left outside the runs, or a clean one swept
     * inside them, is rejected.
     *
     * Throws `Error` on the first violation. O(n log n): an auditor, not a hot-path operation, and
     * it does invoke the retained relation.
     */
    public validateStructure(): RunDeltaVectorStatistics {
        const count = this.#current.size;
        if (count !== this.#checkpoint.size) throw new Error("The current and checkpoint roots have different lengths.");
        if (this.#dirtyCount < 0 || this.#dirtyCount > count) throw new Error("The dirty cardinality is outside the position count.");
        this.#current.validateStructure();
        this.#checkpoint.validateStructure();
        if (this.#dirtyRuns.isEmpty && !this.isClean) throw new Error("A clean version is not canonicalized onto its checkpoint root.");
        if (this.#dirtyRuns.isEmpty && this.#dirtyCount !== 0) throw new Error("An empty run index carries a non-zero dirty cardinality.");

        let previousEnd = -1;
        let countedDirty = 0;
        for (const entry of this.#dirtyRuns) {
            if (entry.key < 0 || entry.key >= entry.value || entry.value > count || entry.key <= previousEnd) {
                throw new Error("Dirty runs are empty, out of range, overlapping, adjacent, or unordered.");
            }
            countedDirty += entry.value - entry.key;
            previousEnd = entry.value;
        }
        if (countedDirty !== this.#dirtyCount) throw new Error("Dirty run lengths do not sum to the dirty cardinality.");

        let shared = 0;
        for (let position = 0; position < count; position++) {
            const dirty = this.#findRun(position) !== undefined;
            const current = cellAt(this.#current, position);
            const checkpoint = cellAt(this.#checkpoint, position);
            if (current === checkpoint) shared++;
            else if (!dirty) throw new Error("A clean position does not reuse its exact checkpoint representative.");
            if (dirty && this.valueEquals(current.value, checkpoint.value)) {
                throw new Error("A dirty position is relation-equal to its checkpoint value.");
            }
        }
        return { count, dirtyCount: this.#dirtyCount, dirtyRunCount: this.#dirtyRuns.size, sharedRepresentativeCount: shared, isClean: this.isClean };
    }
    /** Iterate the current values in positional order. Theta(n). */
    public *[Symbol.iterator](): IterableIterator<T> { for (const cell of this.#current) yield cell.value; }
    #position(index: number): number {
        if (!Number.isInteger(index) || index < 0 || index >= this.#current.size) {
            throw new RangeError("Position is outside the run-delta vector.");
        }
        return index;
    }
    #rank(rank: number): number {
        if (!Number.isInteger(rank) || rank < 0 || rank >= this.#dirtyRuns.size) {
            throw new RangeError("Dirty-run rank is outside the run index.");
        }
        return rank;
    }
    #findRun(position: number): PersistentDirtyRun | undefined {
        const entry = this.#dirtyRuns.floorEntry(position);
        return entry === undefined || entry.value <= position ? undefined : toRun(entry.key, entry.value);
    }
    #replaceCell(position: number, replacement: Cell<T>, remainsDirty: boolean): PersistentRunDeltaVector<T> {
        const wasDirty = this.#findRun(position) !== undefined;
        let nextCurrent = requireRoot(this.#current.setItem(position, replacement));
        let nextRuns = this.#dirtyRuns;
        let nextDirtyCount = this.#dirtyCount;
        if (!wasDirty && remainsDirty) { nextRuns = addDirtyPosition(nextRuns, position); nextDirtyCount++; }
        else if (wasDirty && !remainsDirty) { nextRuns = removeDirtyPosition(nextRuns, position); nextDirtyCount--; }
        // A wholly cancelled epoch snaps back to the exact checkpoint root, so clean versions are
        // canonical and later checkpoint, rollback, and cleanliness queries stay O(1).
        if (nextRuns.isEmpty) nextCurrent = this.#checkpoint;
        return new PersistentRunDeltaVector<T>(nextCurrent, this.#checkpoint, nextRuns, nextDirtyCount, this.valueEquals);
    }
    /** Splices one indexed run's current values into the checkpoint, leaving every other run. */
    #acceptRun(run: PersistentDirtyRun): PersistentRunDeltaVector<T> {
        if (this.#dirtyRuns.size === 1) return this.checkpoint();
        return new PersistentRunDeltaVector<T>(
            this.#current,
            replaceRange(this.#checkpoint, this.#current, run),
            this.#dirtyRuns.remove(run.start),
            this.#dirtyCount - run.length,
            this.valueEquals,
        );
    }
    /** Splices one indexed run's checkpoint values back over the current root. */
    #revertRun(run: PersistentDirtyRun): PersistentRunDeltaVector<T> {
        if (this.#dirtyRuns.size === 1) return this.rollback();
        return new PersistentRunDeltaVector<T>(
            replaceRange(this.#current, this.#checkpoint, run),
            this.#checkpoint,
            this.#dirtyRuns.remove(run.start),
            this.#dirtyCount - run.length,
            this.valueEquals,
        );
    }
}
