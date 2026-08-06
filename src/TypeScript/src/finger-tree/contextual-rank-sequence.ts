/**
 * A persistent sequence that ranks and selects events whose occurrence depends on finite left
 * context.
 *
 * An ordinary sum-measured sequence can rank elements carrying a fixed local weight. It cannot rank
 * a comma that is a field separator in one prefix context and quoted data in another, because no
 * single weight is correct for both. A streaming automaton keeps that context but has to replay a
 * prefix after every edit.
 *
 * `ContextualRankSequence` resolves this by lifting a finite deterministic machine into the measure
 * of this workspace's measured finger tree. Every subtree caches, *for every possible incoming
 * state*, the outgoing state and the number of events emitted while consuming that subtree. Ordered
 * composition feeds the left subtree's outgoing state into the right subtree's summary, which turns
 * a context-dependent scan into an associative measure:
 *
 * ```text
 * (A * B).next(q)   = B.next(A.next(q))
 * (A * B).events(q) = A.events(q) + B.events(A.next(q))
 * (A * B).count     = A.count + B.count
 * ```
 *
 * Composition is associative but emphatically **not** commutative: `A * B` and `B * A` generally
 * disagree in *both* components, so summaries are only ever combined left to right. The identity
 * maps every state to itself, emits zero events, and holds zero elements. Because event counts are
 * nonnegative, the select predicate `prefix.events(q0) > r` is monotone, so a single
 * measure-directed descent finds the element that emitted event `r` - no binary search over
 * boundaries and no prefix replay.
 *
 * The machine is a retained runtime **policy object**, not a type parameter, exactly as
 * `RangeUpdateSequence` retains its `RangeUpdateAlgebra` and `BrodalOkasakiHeap` retains its
 * comparator. A sequence keeps the exact object it was built with, so an empty split half or empty
 * range stays a usable sequence, and concatenating two sequences is gated on retained-policy
 * identity: a mismatch is a `TypeError`, following `BrodalOkasakiHeap.meld`.
 *
 * Each stored element is wrapped with its eagerly computed O(s) effect row. That does three things
 * at once: the machine runs once per element rather than once per level of every later descent, so
 * every cached query invokes the machine *zero* times; the row is computed before any tree node is
 * built, so a rejected or throwing transition publishes no successor; and a located entry is never
 * `undefined`, which keeps a stored `undefined` element unambiguous in the substrate's locate
 * result.
 *
 * # Bounds
 *
 * Let `n` be the element count and `s` the machine's fixed state count.
 *
 * * Whole-sequence evaluation is O(1) amortized: it reads the memoized root summary, and the first
 *   read of a freshly built spine may force its suspended middles once.
 * * Indexing is O(log n), composes no summary, and calls the machine not at all.
 * * Prefix evaluation, contextual event rank and select, insertion, replacement, removal,
 *   splitting, and slicing are O(s log n) amortized: a measure-directed descent over O(log n)
 *   levels, each level composing one O(s) effect table.
 * * Endpoint updates are **O(s) amortized** (O(s log n) worst case per call), the reference bound.
 *   `measured-sequence.ts` is a genuine lazy Hinze-Paterson finger tree - 1-4-item digits at both
 *   ends, 2-3 nodes below, and every deep node's middle held behind a memoized defunctionalized
 *   suspension - so an endpoint push performs amortized O(1) node work, each node composing one
 *   O(s) effect table. The amortization is valid under **persistent branching**, not merely
 *   ephemeral linear use, because a forced suspension memoizes into a cell shared by every version
 *   referencing it: work deferred by one version and forced by another is never repeated. Earlier
 *   ports of this collection sat on join trees and honestly documented Theta(s log n) here; this
 *   workspace's substrate removes that divergence.
 * * Concatenation is **O(s log(min(n, m))) amortized**, also the reference bound: the substrate
 *   suspends its recursion into the two middles and regroups only the boundary digits eagerly, so
 *   the immediate cost is a constant number of table compositions regardless of how unequal the
 *   operands are. No height-difference term survives.
 * * Enumeration is O(n) and building from a source is O(s n).
 *
 * Each element stores its own O(s) effect row and every internal node stores one more, so the
 * structure costs O(s n) summary space rather than O(n); each retained changed spine adds
 * O(s log n) while untouched structure stays shared. When the machine is fixed, `s` is a constant
 * and the contextual queries are O(log n) instead of the Theta(n) replay a state-free persistent
 * sequence needs. If `s` is treated as an input rather than a fixed policy, the structure does not
 * dominate a plain persistent sequence: its edits and its storage explicitly carry the `s` factor.
 *
 * # Deliberate limits
 *
 * Context must be finite and must flow left to right. Event weights must be nonnegative so select
 * stays monotone. One input element occupies one measured leaf, so this is not a chunked text rope
 * and makes no cache-density claim. There is no way to change the machine of an existing sequence;
 * rebuilding under another machine costs O(s n).
 */
import { MeasuredSequence } from "./measured-sequence.js";
import type { MeasurePolicy } from "./measures.js";

/** One transition of a deterministic additive event machine. */
export interface ContextualEventTransition {
    /** The state after consuming the element. Must lie in `0..stateCount - 1`. */
    readonly nextState: number;
    /** The nonnegative number of logical events emitted; one transition may emit more than one. */
    readonly eventCount: number;
}

/**
 * A finite deterministic machine whose transitions emit nonnegative event counts.
 *
 * A machine is a retained *value*, not a type parameter: the sequence holds the exact object it was
 * built with, so an empty result stays usable and two sequences are compatible exactly when they
 * retain the same object. `stateCount` must be a positive integer and must not change while any
 * sequence built from the machine is alive. For every state in `0..stateCount - 1`, `transition`
 * must return a state in the same range and a nonnegative safe-integer event count, and it must be
 * deterministic and free of side effects. Violating any of those is a policy bug and raises a
 * `RangeError`.
 */
export interface ContextualEventMachine<TElement> {
    /** The finite number of machine states. Must be a positive integer. */
    readonly stateCount: number;
    /** Consumes one element from one valid state, reporting the outgoing state and event count. */
    transition(state: number, element: TElement): ContextualEventTransition;
}

/** The result of running a contextual event machine over a prefix. */
export interface ContextualPrefixSummary {
    /** The machine state after the prefix. */
    readonly finalState: number;
    /** The number of events emitted by the prefix. */
    readonly eventCount: number;
}

/** Identifies one contextual event and the input element that emitted it. */
export interface ContextualEventLocation {
    /** The zero-based index of the emitting element. */
    readonly elementIndex: number;
    /** The event's zero-based ordinal among the events emitted by that one transition. */
    readonly eventIndexInElement: number;
    /** The machine state before the element. */
    readonly stateBefore: number;
    /** The machine state after the element. */
    readonly stateAfter: number;
    /** The total number of events emitted by that transition. */
    readonly elementEventCount: number;
}

/** The two sequences produced by a split; both share structure with the original. */
export interface ContextualRankSequenceSplit<TElement> {
    /** The elements before the boundary. */
    readonly left: ContextualRankSequence<TElement>;
    /** The elements at and after the boundary. */
    readonly right: ContextualRankSequence<TElement>;
}

/** Measurements returned by a successful structural audit, all recomputed by direct scan. */
export interface ContextualRankSequenceStatistics {
    /** The number of elements, recounted by enumeration. */
    readonly count: number;
    /** The machine's state count. */
    readonly stateCount: number;
    /** The largest total event count over all initial states, recomputed by direct scan. */
    readonly maximumTotalEventCount: number;
}

/**
 * Work charged inside one synchronous observation scope.
 *
 * Asymptotic claims are invisible to correctness tests - an eager join-tree substrate would satisfy
 * every behavioural assertion while paying Theta(s log n) per endpoint push - so the bound probes
 * count the two operations the bounds are stated in. This mirrors the diagnostic seam
 * `range-update-sequence-internals.ts` already exposes for the same reason.
 */
export interface ContextualRankOperationStatistics {
    /** Ordered compositions of two nonidentity subtree summaries, each O(s) table work. */
    readonly summaryCompositions: number;
    /** Machine transition invocations. */
    readonly machineTransitions: number;
}

/** An observed operation's result together with the work it charged. */
export interface ContextualRankObservation<Result> {
    /** Whatever the observed operation returned. */
    readonly result: Result;
    /** The counters accumulated while it ran. */
    readonly statistics: ContextualRankOperationStatistics;
}

interface Counters {
    summaryCompositions: number;
    machineTransitions: number;
}

let currentCounters: Counters | undefined;

function recordComposition(): void { if (currentCounters !== undefined) currentCounters.summaryCompositions++; }

function recordTransition(): void { if (currentCounters !== undefined) currentCounters.machineTransitions++; }

/**
 * Runs a synchronous operation inside a fresh observation scope and reports the work it charged.
 *
 * Scopes nest: an inner scope counts on its own and the enclosing one is restored afterwards, in
 * all cases including a throw. Deferred substrate work is charged where it is *forced*, not where
 * it was scheduled, which is precisely what makes an amortized claim checkable.
 */
export function observeContextualRankWork<Result>(operation: () => Result): ContextualRankObservation<Result> {
    const previous = currentCounters;
    const counters: Counters = { summaryCompositions: 0, machineTransitions: 0 };
    currentCounters = counters;
    try {
        const result = operation();
        return { result, statistics: { ...counters } };
    } finally {
        currentCounters = previous;
    }
}

/**
 * A subtree summary: one outgoing state and event count per machine state, plus the element count.
 *
 * The count is what lets composition short-circuit an identity operand without allocating a table;
 * it is also an independent cross-check of the substrate's own cached sizes during an audit.
 */
class Summary {
    public constructor(
        public readonly count: number,
        public readonly nextStates: readonly number[],
        public readonly eventCounts: readonly number[],
    ) {}
    /** The state this subtree leaves the machine in, entered from `initialState`. */
    public finalState(initialState: number): number { return this.nextStates[initialState]!; }
    /** The number of events this subtree emits, entered from `initialState`. */
    public eventCount(initialState: number): number { return this.eventCounts[initialState]!; }
    /** Both components at once, as one prefix summary. */
    public evaluate(initialState: number): ContextualPrefixSummary {
        return { finalState: this.nextStates[initialState]!, eventCount: this.eventCounts[initialState]! };
    }
}

/**
 * Composes two subtree summaries in sequence order, `left` preceding `right`.
 *
 * Directional: the left summary's outgoing state selects the right summary's row, so swapping the
 * operands generally changes the outgoing state *and* the event count. Never call it with the
 * operands reversed to "save" a branch.
 */
function combineSummaries(left: Summary, right: Summary): Summary {
    if (left.count === 0) return right;
    if (right.count === 0) return left;
    recordComposition();
    const width = left.nextStates.length;
    const nextStates: number[] = new Array<number>(width);
    const eventCounts: number[] = new Array<number>(width);
    for (let state = 0; state < width; state++) {
        const middle = left.nextStates[state]!;
        nextStates[state] = right.nextStates[middle]!;
        const total = left.eventCounts[state]! + right.eventCounts[middle]!;
        if (!Number.isSafeInteger(total)) {
            throw new RangeError("The composed contextual event count exceeds Number.MAX_SAFE_INTEGER.");
        }
        eventCounts[state] = total;
    }
    return new Summary(left.count + right.count, nextStates, eventCounts);
}

/** One stored element together with its eagerly computed effect row. */
class Entry<TElement> {
    public constructor(public readonly element: TElement, public readonly summary: Summary) {}
}

/**
 * Everything derived once per machine object: its validated state count, the measure policy the
 * substrate is parameterized by, and the canonical empty sequence.
 *
 * Deriving the policy per machine rather than sharing one is deliberate. The substrate rejects a
 * concatenation whose operands retain different policy objects, so machine identity and policy
 * identity coincide and the gate is enforced twice - once here with a precise message, once in the
 * substrate.
 */
interface MachineContext<TElement> {
    readonly machine: ContextualEventMachine<TElement>;
    readonly stateCount: number;
    readonly policy: MeasurePolicy<Entry<TElement>, Summary>;
    empty: ContextualRankSequence<TElement> | undefined;
}

const contexts = new WeakMap<object, MachineContext<unknown>>();

function contextFor<TElement>(machine: ContextualEventMachine<TElement>): MachineContext<TElement> {
    if ((typeof machine !== "object" || machine === null) && typeof machine !== "function") {
        throw new TypeError("A contextual event machine object is required.");
    }
    const key = machine as object;
    const existing = contexts.get(key) as MachineContext<TElement> | undefined;
    if (existing !== undefined) return existing;

    if (typeof machine.transition !== "function") {
        throw new TypeError("A contextual event machine must supply a transition function.");
    }
    const stateCount = machine.stateCount;
    if (!Number.isInteger(stateCount) || stateCount < 1) {
        throw new RangeError("A contextual event machine must have at least one state.");
    }

    const nextStates: number[] = new Array<number>(stateCount);
    const eventCounts: number[] = new Array<number>(stateCount);
    for (let state = 0; state < stateCount; state++) { nextStates[state] = state; eventCounts[state] = 0; }
    const identity = new Summary(0, nextStates, eventCounts);
    const context: MachineContext<TElement> = {
        machine,
        stateCount,
        policy: { identity, combine: combineSummaries, measure: (entry: Entry<TElement>): Summary => entry.summary },
        empty: undefined,
    };
    contexts.set(key, context as unknown as MachineContext<unknown>);
    return context;
}

/**
 * Runs the machine once from every incoming state, eagerly building the element's effect row.
 *
 * Every contract check happens here, before any tree node is built, which is what makes a rejected
 * edit publish nothing and leave every retained version untouched.
 */
function makeEntry<TElement>(context: MachineContext<TElement>, element: TElement): Entry<TElement> {
    const stateCount = context.stateCount;
    const nextStates: number[] = new Array<number>(stateCount);
    const eventCounts: number[] = new Array<number>(stateCount);
    for (let state = 0; state < stateCount; state++) {
        recordTransition();
        const transition = context.machine.transition(state, element);
        const nextState = transition.nextState;
        const eventCount = transition.eventCount;
        if (!Number.isInteger(nextState) || nextState < 0 || nextState >= stateCount) {
            throw new RangeError("The contextual event machine returned an invalid next state.");
        }
        if (!Number.isSafeInteger(eventCount)) {
            throw new RangeError("The contextual event machine returned an event count that is not a safe integer.");
        }
        if (eventCount < 0) {
            throw new RangeError("The contextual event machine returned a negative event count.");
        }
        nextStates[state] = nextState;
        eventCounts[state] = eventCount;
    }
    return new Entry(element, new Summary(1, nextStates, eventCounts));
}

/**
 * Builds a contextual event machine from a plain function instead of a class.
 *
 * The caller owns the machine contract: `stateCount` must be a positive integer and `transition`
 * must stay inside `0..stateCount - 1` and emit nonnegative counts, since contextual rank and
 * select rely on both. The returned object is the retained identity, so keep it if you intend to
 * concatenate sequences built from it.
 */
export function createContextualEventMachine<TElement>(
    stateCount: number,
    transition: (state: number, element: TElement) => ContextualEventTransition,
): ContextualEventMachine<TElement> {
    if (typeof transition !== "function") throw new TypeError("A transition function is required.");
    if (!Number.isInteger(stateCount) || stateCount < 1) {
        throw new RangeError("A contextual event machine must have at least one state.");
    }
    return { stateCount, transition };
}

/** An immutable sequence indexed by events whose occurrence depends on finite left context. */
export class ContextualRankSequence<TElement> implements Iterable<TElement> {
    readonly #entries: MeasuredSequence<Entry<TElement>, Summary>;
    readonly #context: MachineContext<TElement>;

    private constructor(entries: MeasuredSequence<Entry<TElement>, Summary>, context: MachineContext<TElement>) {
        this.#entries = entries;
        this.#context = context;
    }

    /**
     * The canonical empty sequence over a machine, retaining that exact object.
     *
     * Its summary maps every state to itself with zero events, so an empty split half or range
     * stays a usable sequence rather than a degenerate one. Repeated calls with the same machine
     * return the same instance.
     */
    public static empty<TElement>(machine: ContextualEventMachine<TElement>): ContextualRankSequence<TElement> {
        return ContextualRankSequence.#emptyOf(contextFor(machine));
    }

    static #emptyOf<TElement>(context: MachineContext<TElement>): ContextualRankSequence<TElement> {
        context.empty ??= new ContextualRankSequence(MeasuredSequence.empty(context.policy), context);
        return context.empty;
    }

    /**
     * Builds a sequence from a source in input order, in one eager bottom-up pass. O(s n).
     *
     * Passing a sequence that already retains this exact machine returns it unchanged.
     */
    public static from<TElement>(
        values: Iterable<TElement>,
        machine: ContextualEventMachine<TElement>,
    ): ContextualRankSequence<TElement> {
        const context = contextFor(machine);
        if (values instanceof ContextualRankSequence) {
            const existing: ContextualRankSequence<TElement> = values;
            if (existing.#context === context) return existing;
        }
        const entries: Entry<TElement>[] = [];
        for (const value of values) entries.push(makeEntry(context, value));
        if (entries.length === 0) return ContextualRankSequence.empty(machine);
        return new ContextualRankSequence(MeasuredSequence.from(entries, context.policy), context);
    }

    /** The retained machine. Only sequences retaining this exact object may be concatenated. */
    public get machine(): ContextualEventMachine<TElement> { return this.#context.machine; }
    /** The machine's finite state count, read once when this machine's lineage was created. */
    public get stateCount(): number { return this.#context.stateCount; }
    /** Number of stored elements, read from the substrate's strict cached sizes. O(1). */
    public get size(): number { return this.#entries.size; }
    /** Cross-port alias for `size`, matching the reference's `Count`. */
    public get count(): number { return this.#entries.size; }
    /** Whether the sequence holds no elements. O(1). */
    public get isEmpty(): boolean { return this.#entries.isEmpty; }

    /**
     * The element at a zero-based index.
     *
     * O(log n): a descent by cached subtree sizes that composes no summary and never calls the
     * machine. Throws `RangeError` when the index is outside the sequence.
     */
    public get(index: number): TElement {
        this.#checkElementIndex(index);
        const entry = this.#entries.at(index);
        if (entry === undefined) throw new Error("A validated element lookup found no entry.");
        return entry.element;
    }

    /**
     * Runs the machine over the whole sequence from an initial state.
     *
     * O(1) amortized: the answer is the memoized root summary. Returns `undefined` when
     * `initialState` is outside `0..stateCount - 1`.
     */
    public evaluate(initialState: number): ContextualPrefixSummary | undefined {
        if (!this.#isValidState(initialState)) return undefined;
        return this.#entries.measure.evaluate(initialState);
    }

    /**
     * Runs the machine over the first `elementCount` elements from an initial state.
     *
     * O(s log n) amortized, and O(1) at either endpoint, where the answer is either the identity or
     * the cached root summary. Throws `RangeError` when `elementCount` falls outside `0..size`, and
     * returns `undefined` when `initialState` is outside the machine range.
     */
    public evaluatePrefix(elementCount: number, initialState: number): ContextualPrefixSummary | undefined {
        this.#checkBoundaryIndex(elementCount);
        if (!this.#isValidState(initialState)) return undefined;
        if (elementCount === 0) return { finalState: initialState, eventCount: 0 };
        if (elementCount === this.size) return this.#entries.measure.evaluate(initialState);
        const measure = this.#entries.prefixMeasure(elementCount);
        if (measure === undefined) throw new Error("A validated prefix length has no measure.");
        return measure.evaluate(initialState);
    }

    /**
     * The number of contextual events strictly before an element boundary.
     *
     * Fails on exactly the inputs `evaluatePrefix` fails on. O(s log n) amortized.
     */
    public eventRank(elementCount: number, initialState: number): number | undefined {
        return this.evaluatePrefix(elementCount, initialState)?.eventCount;
    }

    /**
     * Locates the zero-based contextual event `eventIndex`, counting from `initialState`.
     *
     * Because event counts are nonnegative, `prefix.events(q0) > r` is monotone, so this is a
     * *single* measure-directed descent: no binary search over boundaries and no prefix replay. The
     * located element's own effect row is already cached, so the machine is not called at all.
     * O(s log n) amortized.
     *
     * Returns `undefined` when `initialState` is outside the machine range, or when `eventIndex` is
     * negative, fractional, or at or past the sequence's total event count.
     */
    public trySelectEvent(eventIndex: number, initialState: number): ContextualEventLocation | undefined {
        if (!this.#isValidState(initialState)) return undefined;
        if (!Number.isInteger(eventIndex) || eventIndex < 0) return undefined;
        if (eventIndex >= this.#entries.measure.eventCount(initialState)) return undefined;
        const located = this.#entries.locate((summary) => summary.eventCount(initialState) > eventIndex);
        if (!located.found || located.value === undefined) {
            throw new Error("The contextual event summary is inconsistent.");
        }
        const before = located.measureBefore;
        const stateBefore = before.finalState(initialState);
        const row = located.value.summary;
        return {
            elementIndex: located.index,
            eventIndexInElement: eventIndex - before.eventCount(initialState),
            stateBefore,
            stateAfter: row.finalState(stateBefore),
            elementEventCount: row.eventCount(stateBefore),
        };
    }

    /**
     * A sequence with one element prepended.
     *
     * O(s) amortized, O(s log n) worst case per call, valid under persistent branching: an endpoint
     * digit push on the lazy finger-tree substrate whose overflow cascades ride memoized
     * suspensions.
     */
    public prepend(item: TElement): ContextualRankSequence<TElement> {
        return this.#wrap(this.#entries.prepend(this.#entry(item)));
    }

    /** A sequence with one element appended. O(s) amortized, as `prepend` is. */
    public append(item: TElement): ContextualRankSequence<TElement> {
        return this.#wrap(this.#entries.append(this.#entry(item)));
    }

    /**
     * This sequence's elements followed by the other's.
     *
     * Both operands must retain the same machine object, since their effect rows would otherwise be
     * tables of different automata - possibly of different widths. A mismatch is a `TypeError`,
     * following `BrodalOkasakiHeap.meld`. An empty operand is not copied: the result *is* the other
     * operand. O(s log(min(n, m))) amortized in every shape.
     */
    public concat(other: ContextualRankSequence<TElement>): ContextualRankSequence<TElement> {
        if (!(other instanceof ContextualRankSequence)) {
            throw new TypeError("Another contextual rank sequence is required.");
        }
        if (other.#context !== this.#context) {
            throw new TypeError("Sequences must retain the same contextual event machine object.");
        }
        if (other.isEmpty) return this;
        if (this.isEmpty) return other;
        return this.#wrap(this.#entries.concat(other.#entries));
    }

    /**
     * A sequence with `item` inserted so that `index` old elements precede it. O(s log n) amortized,
     * and O(s) amortized at either endpoint. Throws `RangeError` for an index outside `0..size`.
     */
    public insert(index: number, item: TElement): ContextualRankSequence<TElement> {
        this.#checkBoundaryIndex(index);
        if (index === 0) return this.prepend(item);
        if (index === this.size) return this.append(item);
        const entries = this.#entries.insertAt(index, this.#entry(item));
        if (entries === undefined) throw new Error("A validated insertion boundary was rejected.");
        return this.#wrap(entries);
    }

    /** A sequence with the element at `index` replaced. O(s log n) amortized. */
    public setItem(index: number, item: TElement): ContextualRankSequence<TElement> {
        this.#checkElementIndex(index);
        const entries = this.#entries.setAt(index, this.#entry(item));
        if (entries === undefined) throw new Error("A validated replacement index was rejected.");
        return this.#wrap(entries);
    }

    /** A sequence without the element at `index`. O(s log n) amortized. */
    public removeAt(index: number): ContextualRankSequence<TElement> {
        this.#checkElementIndex(index);
        const entries = this.#entries.removeAt(index);
        if (entries === undefined) throw new Error("A validated removal index was rejected.");
        // Canonicalize, as `from`, `splitAt` and `getRange` already do: the reference returns its
        // singleton empty here, and without this the only way to reach a non-canonical empty over a
        // machine would be a removal that drains the sequence.
        return entries.isEmpty ? this.#empty() : this.#wrap(entries);
    }

    /**
     * The elements before `index` and those from it on. O(s log n) amortized; splitting at either
     * endpoint returns the receiver itself as the nonempty half rather than rebuilding it.
     */
    public splitAt(index: number): ContextualRankSequenceSplit<TElement> {
        this.#checkBoundaryIndex(index);
        if (index === 0) return { left: this.#empty(), right: this };
        if (index === this.size) return { left: this, right: this.#empty() };
        const split = this.#entries.splitAt(index);
        if (split === undefined) throw new Error("A validated split boundary was rejected.");
        return { left: this.#wrap(split.left), right: this.#wrap(split.right) };
    }

    /**
     * The `count` elements starting at `index`, as a new sequence. O(s log n) amortized; a range
     * covering the whole sequence returns the receiver.
     */
    public getRange(index: number, count: number): ContextualRankSequence<TElement> {
        this.#checkRange(index, count);
        if (count === 0) return this.#empty();
        if (count === this.size) return this;
        return this.splitAt(index).right.splitAt(count).left;
    }

    /** Copies the elements into an array, in sequence order. O(n). */
    public toArray(): TElement[] { return Array.from(this); }

    /**
     * Whether both versions share any substrate node by identity.
     *
     * A representation test used to confirm that a derived version really reused structure, not an
     * equality test. The walk forces suspended middles, because deferred structure hides shared
     * subtrees inside pending cells where no identity walk could see them.
     */
    public sharesStructureWith(other: ContextualRankSequence<TElement>): boolean {
        return this.#entries.sharesStructureWith(other.#entries);
    }

    /**
     * Recomputes every cached summary by direct machine scan and compares it with the tree.
     *
     * Checks that the machine still declares the state count this lineage was built with, that each
     * element's cached effect row matches a fresh scan, that the root summary is exactly the ordered
     * composition of those rows, and that the cached element count agrees with enumeration. Throws
     * on the first violation. A defensive audit; ordinary operations maintain these invariants.
     * O(s n).
     */
    public validateStructure(): ContextualRankSequenceStatistics {
        const context = this.#context;
        const stateCount = context.stateCount;
        if (context.machine.stateCount !== stateCount) {
            throw new Error("The machine's state count changed after the sequence was built.");
        }
        const entries = [...this.#entries];
        const count = entries.length;
        const measure = this.#entries.measure;
        if (count !== this.size || count !== measure.count) {
            throw new Error("The cached element count disagrees with enumeration.");
        }
        if (measure.nextStates.length !== stateCount || measure.eventCounts.length !== stateCount) {
            throw new Error("The cached contextual summary does not cover every machine state.");
        }

        for (const entry of entries) {
            const fresh = makeEntry(context, entry.element).summary;
            for (let state = 0; state < stateCount; state++) {
                if (fresh.nextStates[state] !== entry.summary.nextStates[state]
                    || fresh.eventCounts[state] !== entry.summary.eventCounts[state]) {
                    throw new Error("A cached element summary disagrees with a direct machine scan.");
                }
            }
        }

        let maximumTotalEventCount = 0;
        for (let initialState = 0; initialState < stateCount; initialState++) {
            let state = initialState;
            let events = 0;
            for (const entry of entries) {
                events += entry.summary.eventCounts[state]!;
                state = entry.summary.nextStates[state]!;
                if (!Number.isSafeInteger(events)) {
                    throw new RangeError("Rescanning the elements overflowed the event total.");
                }
            }
            if (state !== measure.finalState(initialState)) {
                throw new Error("A cached outgoing state disagrees with a direct scan.");
            }
            if (events !== measure.eventCount(initialState)) {
                throw new Error("A cached event count disagrees with a direct scan.");
            }
            maximumTotalEventCount = Math.max(maximumTotalEventCount, events);
        }
        return { count, stateCount, maximumTotalEventCount };
    }

    /** Iterates the stored elements in sequence order. O(n) overall. */
    public *[Symbol.iterator](): IterableIterator<TElement> {
        for (const entry of this.#entries) yield entry.element;
    }

    #entry(item: TElement): Entry<TElement> { return makeEntry(this.#context, item); }

    #wrap(entries: MeasuredSequence<Entry<TElement>, Summary>): ContextualRankSequence<TElement> {
        return new ContextualRankSequence(entries, this.#context);
    }

    #empty(): ContextualRankSequence<TElement> { return ContextualRankSequence.#emptyOf(this.#context); }

    #isValidState(state: number): boolean {
        return Number.isInteger(state) && state >= 0 && state < this.#context.stateCount;
    }

    #checkElementIndex(index: number): void {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) {
            throw new RangeError(`Index ${index} is outside the valid range of the sequence.`);
        }
    }

    #checkBoundaryIndex(index: number): void {
        if (!Number.isInteger(index) || index < 0 || index > this.size) {
            throw new RangeError(`Index ${index} is outside the valid boundary range of the sequence.`);
        }
    }

    #checkRange(index: number, count: number): void {
        if (!Number.isInteger(index) || index < 0) throw new RangeError(`Index ${index} must be a nonnegative integer.`);
        if (!Number.isInteger(count) || count < 0) throw new RangeError(`Count ${count} must be a nonnegative integer.`);
        if (index > this.size) throw new RangeError(`Index ${index} is outside the valid boundary range of the sequence.`);
        if (count > this.size - index) throw new RangeError(`Count ${count} extends past the end of the sequence.`);
    }
}
