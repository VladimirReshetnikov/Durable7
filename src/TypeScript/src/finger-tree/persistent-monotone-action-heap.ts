/**
 * Persistent Brodal-Okasaki heap with lazily composable monotone priority actions.
 *
 * The action-tagged sibling of `BrodalOkasakiHeap`. It keeps that module's bootstrapped
 * skew-binomial representation - a global minimum root above a skew-binomial forest - and adds one
 * immutable pending action to every tree and to every forest spine cell. A tree tag applies to that
 * tree and all of its descendants; a forest tag applies uniformly to every tree in that suffix.
 *
 * The point of the tags is `transformAll`, which applies one monotone action to every priority
 * *currently* in a version by composing a single tag at the root: O(1) worst-case time, allocating
 * O(1) new structure. Its semantics are deliberately temporal - entries inserted later, and entries
 * arriving through a later meld, are not retroactively transformed - so two independently
 * transformed heaps still meld in O(1) worst-case time even when the actions have no inverse, which
 * a clamp does not.
 *
 * Composition is directional everywhere: `compose(outer, inner)` denotes `outer(inner(priority))`,
 * so the *newer* action is always the outer one. Every tag-pushdown site in this module - the tree
 * site and the forest-spine site alike - composes the newer action over the older one in that order.
 *
 * The ordinary heap bounds are those of the untagged sibling, whose core is a real bootstrapped
 * skew-binomial forest with skew links and split-forest decomposition: insert, minimum, and meld are
 * O(1) worst case; delete-minimum is O(log n) worst case; enumeration and `validateStructure` are
 * Theta(n); retaining a version as a fork is O(1). Those bounds include the O(1)-time action-policy
 * calls and priority comparisons they perform, so a policy whose `compose`, `apply`, or `isIdentity`
 * is not O(1), or whose composed actions are not of fixed size, invalidates them.
 *
 * The action policy is trusted. Violating its identity, associative-composition, monotonicity, or
 * O(1)-operation contract invalidates both the semantic and the complexity guarantees. Every update
 * is persistent and leaves all source versions usable.
 */
import { defaultComparator, type Comparator } from "./ordering.js";

/**
 * A retained runtime policy supplying a constant-time-composable monotone action family together
 * with the priority ordering its actions are monotone for.
 *
 * `compose(outer, inner)` must denote `outer(inner(priority))` and must be associative with
 * `identity` as its unit. Every action must be monotone for `comparator`: if `p <= q` then
 * `apply(a, p) <= apply(a, q)`. `isIdentity`, `compose`, and `apply` are part of the heap's
 * advertised bounds and must each take O(1) time over fixed-size actions. Results must stay
 * deterministic and semantically stable for the lifetime of every heap version.
 *
 * A heap retains one policy object by identity; only heaps retaining the exact same object may meld.
 */
export interface MonotoneHeapAction<TPriority, TAction> {
    /** The exact ordering this action family promises monotonicity for. */
    readonly comparator: Comparator<TPriority>;
    /** The semantic identity action. Not a sentinel: `isIdentity` decides which values qualify. */
    readonly identity: TAction;
    /** Whether the action acts as the identity, so storing it would be pointless. O(1). */
    isIdentity(action: TAction): boolean;
    /** The action equivalent to `outer(inner(priority))`. Directional, not assumed commutative. */
    compose(outer: TAction, inner: TAction): TAction;
    /** Apply one action to one priority. O(1). */
    apply(action: TAction, priority: TPriority): TPriority;
}

/**
 * The raw components of an `OrderClamp`. A key that is absent denotes an absent component, so a
 * legitimately `undefined` priority value stays distinguishable from a missing bound.
 */
export interface OrderClampParts<T> {
    /** Present when the clamp is an exact constant function. */
    readonly constantValue?: T;
    /** Present when the clamp has a lower bound; absence denotes negative infinity. */
    readonly lowerBound?: T;
    /** Present when the clamp has an upper bound; absence denotes positive infinity. */
    readonly upperBound?: T;
}

/**
 * A lower bound, an upper bound, both bounds, an exact constant, or the identity over an ordered
 * domain.
 *
 * Values are normally created by `OrderClampPolicy`, so that two-sided clamps are validated with the
 * same comparator later used for application and composition. The policy never produces a value that
 * is both constant and bounded; the raw constructor can build one only so that the
 * constant-then-lower-then-upper application order stays observable. Every accessor is O(1).
 */
export class OrderClamp<T> {
    readonly #isConstant: boolean;
    readonly #constant: T | undefined;
    readonly #hasLowerBound: boolean;
    readonly #lowerBound: T | undefined;
    readonly #hasUpperBound: boolean;
    readonly #upperBound: T | undefined;

    /** Builds a clamp from raw components; prefer the `OrderClampPolicy` factories. O(1). */
    public constructor(parts: OrderClampParts<T> = {}) {
        this.#isConstant = "constantValue" in parts;
        this.#constant = parts.constantValue;
        this.#hasLowerBound = "lowerBound" in parts;
        this.#lowerBound = parts.lowerBound;
        this.#hasUpperBound = "upperBound" in parts;
        this.#upperBound = parts.upperBound;
    }

    /** The identity clamp, which is neither constant nor bounded. O(1). */
    public static identity<T>(): OrderClamp<T> { return new OrderClamp<T>(); }
    /** A floor action `x -> max(x, lowerBound)`. O(1). */
    public static atLeast<T>(lowerBound: T): OrderClamp<T> { return new OrderClamp<T>({ lowerBound }); }
    /** A cap action `x -> min(x, upperBound)`. O(1). */
    public static atMost<T>(upperBound: T): OrderClamp<T> { return new OrderClamp<T>({ upperBound }); }
    /** An exact constant action `x -> value`, returning that exact representative. O(1). */
    public static constant<T>(value: T): OrderClamp<T> { return new OrderClamp<T>({ constantValue: value }); }
    /**
     * A two-sided clamp validated with the supplied comparator. O(1).
     *
     * @throws {RangeError} The lower bound compares greater than the upper bound.
     */
    public static between<T>(lowerBound: T, upperBound: T, comparator: Comparator<T>): OrderClamp<T> {
        if (comparator(lowerBound, upperBound) > 0) throw new RangeError("The lower bound must not compare greater than the upper bound.");
        return new OrderClamp<T>({ lowerBound, upperBound });
    }

    /** Whether this action is an exact constant function. O(1). */
    public get isConstant(): boolean { return this.#isConstant; }
    /** Whether this action has a lower bound. O(1). */
    public get hasLowerBound(): boolean { return this.#hasLowerBound; }
    /** Whether this action has an upper bound. O(1). */
    public get hasUpperBound(): boolean { return this.#hasUpperBound; }
    /** The exact constant result. @throws {RangeError} The action is not constant. */
    public get constantValue(): T { if (!this.#isConstant) throw new RangeError("The clamp is not constant."); return this.#constant as T; }
    /** The lower bound. @throws {RangeError} The action has no lower bound. */
    public get lowerBound(): T { if (!this.#hasLowerBound) throw new RangeError("The clamp has no lower bound."); return this.#lowerBound as T; }
    /** The upper bound. @throws {RangeError} The action has no upper bound. */
    public get upperBound(): T { if (!this.#hasUpperBound) throw new RangeError("The clamp has no upper bound."); return this.#upperBound as T; }

    /**
     * Whether both clamps carry the same components, comparing present components by `Object.is`.
     * A representation test, not a semantic-equivalence test: two clamps that agree on every input
     * may still differ here. O(1).
     */
    public equals(other: OrderClamp<T>): boolean {
        if (this.#isConstant !== other.#isConstant || this.#hasLowerBound !== other.#hasLowerBound || this.#hasUpperBound !== other.#hasUpperBound) return false;
        if (this.#isConstant && !Object.is(this.#constant, other.#constant)) return false;
        if (this.#hasLowerBound && !Object.is(this.#lowerBound, other.#lowerBound)) return false;
        return !this.#hasUpperBound || Object.is(this.#upperBound, other.#upperBound);
    }
}

/**
 * The closed monotone action family `x -> clamp(x, lower, upper)`, plus the exact constant function.
 *
 * A missing bound denotes negative or positive infinity, and composition always yields either one
 * constant-sized clamp or an explicit constant function, so `compose` and `apply` are O(1). The
 * explicit constant case preserves exact results even when the comparator treats distinct priority
 * values as equal: an inclusive `[k, k]` clamp preserves an input equivalent to `k`, whereas the
 * constant function must hand back the exact `k` representative.
 *
 * Composition is non-commutative and deliberately so. Two overlapping clamps intersect, keeping the
 * *inner* (older) representative when two boundary values compare equal; two disjoint clamps
 * collapse to a constant carrying the *outer* (newer) boundary, which is the only value the
 * composite can ever return. A cap composed after a floor therefore collapses to the cap's boundary
 * while the opposite order collapses to the floor's.
 *
 * The retained comparator is the exact ordering this family is monotone for; a heap adopts it.
 */
export class OrderClampPolicy<T> implements MonotoneHeapAction<T, OrderClamp<T>> {
    readonly #identity: OrderClamp<T> = OrderClamp.identity<T>();
    /** The exact ordering this clamp family is monotone for. */
    public readonly comparator: Comparator<T>;
    /** A clamp family monotone for the supplied comparator, retained by object identity. */
    public constructor(comparator: Comparator<T> = defaultComparator) { this.comparator = comparator; }
    /** The identity clamp, which has neither bound and is not constant. O(1). */
    public get identity(): OrderClamp<T> { return this.#identity; }
    /** A floor action `x -> max(x, lowerBound)`. O(1). */
    public atLeast(lowerBound: T): OrderClamp<T> { return OrderClamp.atLeast(lowerBound); }
    /** A cap action `x -> min(x, upperBound)`. O(1). */
    public atMost(upperBound: T): OrderClamp<T> { return OrderClamp.atMost(upperBound); }
    /** An exact constant action `x -> value`. O(1). */
    public constant(value: T): OrderClamp<T> { return OrderClamp.constant(value); }
    /**
     * A two-sided clamp validated with the retained comparator. O(1).
     *
     * @throws {RangeError} The lower bound compares greater than the upper bound.
     */
    public between(lowerBound: T, upperBound: T): OrderClamp<T> { return OrderClamp.between(lowerBound, upperBound, this.comparator); }
    /** Whether the action is neither constant nor bounded, and so changes nothing. O(1). */
    public isIdentity(action: OrderClamp<T>): boolean { return !action.isConstant && !action.hasLowerBound && !action.hasUpperBound; }

    /**
     * The clamp equivalent to applying `inner` first and then `outer`. O(1).
     *
     * Identity short-circuits; an outer constant wins outright; an inner constant has the outer
     * clamp applied to it; disjoint bounds collapse to a constant carrying the outer (newer)
     * boundary; overlapping bounds intersect, keeping the inner (older) representative when two
     * boundaries compare equal.
     */
    public compose(outer: OrderClamp<T>, inner: OrderClamp<T>): OrderClamp<T> {
        if (this.isIdentity(outer)) return inner;
        if (this.isIdentity(inner)) return outer;
        if (outer.isConstant) return outer;
        if (inner.isConstant) return this.constant(this.apply(outer, inner.constantValue));
        if (inner.hasUpperBound && outer.hasLowerBound && this.comparator(inner.upperBound, outer.lowerBound) < 0) return this.constant(outer.lowerBound);
        if (inner.hasLowerBound && outer.hasUpperBound && this.comparator(inner.lowerBound, outer.upperBound) > 0) return this.constant(outer.upperBound);
        const parts: { lowerBound?: T; upperBound?: T } = {};
        if (inner.hasLowerBound) parts.lowerBound = !outer.hasLowerBound || this.comparator(inner.lowerBound, outer.lowerBound) >= 0 ? inner.lowerBound : outer.lowerBound;
        else if (outer.hasLowerBound) parts.lowerBound = outer.lowerBound;
        if (inner.hasUpperBound) parts.upperBound = !outer.hasUpperBound || this.comparator(inner.upperBound, outer.upperBound) <= 0 ? inner.upperBound : outer.upperBound;
        else if (outer.hasUpperBound) parts.upperBound = outer.upperBound;
        return new OrderClamp<T>(parts);
    }

    /** Apply one clamp, testing the constant, then the lower bound, then the upper bound. O(1). */
    public apply(action: OrderClamp<T>, priority: T): T {
        if (action.isConstant) return action.constantValue;
        if (action.hasLowerBound && this.comparator(priority, action.lowerBound) < 0) return action.lowerBound;
        if (action.hasUpperBound && this.comparator(priority, action.upperBound) > 0) return action.upperBound;
        return priority;
    }
}

/** A payload paired with its current logical priority, every pending action already applied. */
export interface MonotoneHeapEntry<E, P> { readonly element: E; readonly priority: P }
/** The minimum entry together with the heap that remains once it is removed. */
export interface MonotoneActionMinimumView<E, P, A> { readonly minimum: MonotoneHeapEntry<E, P>; readonly remainder: PersistentMonotoneActionHeap<E, P, A> }
/**
 * Shape measurements returned by a successful structural audit.
 *
 * `taggedComponentCount` counts pending tree- and forest-tag observations made during the walk. It
 * is not a distinct-object count: exposing one uniform forest tag produces several tagged logical
 * views of the same retained cells.
 */
export interface PersistentMonotoneActionHeapStatistics {
    readonly count: number;
    readonly rootForestLength: number;
    readonly maximumRank: number;
    readonly maximumDepth: number;
    readonly taggedComponentCount: number;
}

/** One skew-binomial tree carrying a pending action for itself and all of its descendants. */
class Tree<E, P, A> {
    public constructor(
        public readonly rank: number,
        public readonly element: E,
        /** Raw priority; the logical priority is `apply(action, priority)`. */
        public readonly priority: P,
        /** Raw children; the logical children are `tagForest(children, action)`. */
        public readonly children: Forest<E, P, A> | undefined,
        public readonly action: A,
    ) {}
}
/** One forest spine cell carrying a pending action for its whole suffix. */
class Forest<E, P, A> {
    public constructor(
        /** Raw head; the logical head is `tagTree(rawHead, action)`. */
        public readonly rawHead: Tree<E, P, A>,
        /** Raw tail; the logical tail is `tagForest(rawTail, action)`. */
        public readonly rawTail: Forest<E, P, A> | undefined,
        public readonly action: A,
    ) {}
}
interface SplitForest<E, P, A> { readonly zeros: Forest<E, P, A> | undefined; readonly trees: Forest<E, P, A> | undefined; readonly embeddedForest: Forest<E, P, A> | undefined }
interface MinimumSplit<E, P, A> { readonly tree: Tree<E, P, A>; readonly remainder: Forest<E, P, A> | undefined }

/**
 * Immutable bootstrapped skew-binomial min-heap with pending monotone priority actions.
 *
 * Insert, minimum, and meld are O(1) worst case; `transformAll` is O(1) worst case and allocates
 * O(1) new structure; delete-minimum is O(log n) worst case; enumeration and `validateStructure` are
 * Theta(n). Every operation publishes a new version and leaves every earlier one usable.
 */
export class PersistentMonotoneActionHeap<E, P, A> implements Iterable<MonotoneHeapEntry<E, P>> {
    readonly #root: Tree<E, P, A> | undefined;
    /** Number of entries, counting duplicates separately. */
    public readonly count: number;
    /** The retained action policy; only heaps retaining this exact object may meld. */
    public readonly actionPolicy: MonotoneHeapAction<P, A>;
    private constructor(root: Tree<E, P, A> | undefined, count: number, actionPolicy: MonotoneHeapAction<P, A>) { this.#root = root; this.count = count; this.actionPolicy = actionPolicy; }

    /** The empty heap, retaining the supplied action policy so empty results stay usable. O(1). */
    public static empty<E, P, A>(actionPolicy: MonotoneHeapAction<P, A>): PersistentMonotoneActionHeap<E, P, A> {
        return new PersistentMonotoneActionHeap<E, P, A>(undefined, 0, actionPolicy);
    }
    /**
     * Builds a heap in O(n) time by repeated worst-case-O(1) insertion. Entries may be
     * `MonotoneHeapEntry` values or plain `[element, priority]` pairs.
     */
    public static from<E, P, A>(entries: Iterable<MonotoneHeapEntry<E, P> | readonly [E, P]>, actionPolicy: MonotoneHeapAction<P, A>): PersistentMonotoneActionHeap<E, P, A> {
        let result = PersistentMonotoneActionHeap.empty<E, P, A>(actionPolicy);
        for (const entry of entries) {
            if (Array.isArray(entry)) { const pair = entry as readonly [E, P]; result = result.insert(pair[0], pair[1]); }
            else { const single = entry as MonotoneHeapEntry<E, P>; result = result.insert(single.element, single.priority); }
        }
        return result;
    }

    /** The retained priority ordering, taken from the action policy. O(1). */
    public get comparator(): Comparator<P> { return this.actionPolicy.comparator; }
    /** Whether the heap holds no entries. O(1). */
    public get isEmpty(): boolean { return this.#root === undefined; }
    /**
     * A minimum entry with every pending action applied, or `undefined` when empty. O(1) worst case:
     * the bootstrapped root is read directly rather than searched for.
     */
    public get minimum(): MonotoneHeapEntry<E, P> | undefined { return this.#root === undefined ? undefined : this.#entryOf(this.#root); }

    /**
     * A heap with the element added at the given priority. O(1) worst case.
     *
     * The new entry joins untransformed: actions applied by an earlier `transformAll` never reach it.
     * A tie against the current root favours the *new* entry, which becomes the new bootstrapped
     * root; otherwise the old root is exposed *before* the new singleton is skew-inserted, so the old
     * root's pending action cannot leak onto an entry that did not exist when it was applied.
     */
    public insert(element: E, priority: P): PersistentMonotoneActionHeap<E, P, A> {
        const singleton = new Tree<E, P, A>(0, element, priority, undefined, this.actionPolicy.identity);
        const root = this.#root;
        if (root === undefined) return this.#derive(singleton, 1);
        if (this.#le(singleton, root)) return this.#derive(new Tree(0, element, priority, this.#cons(root, undefined), this.actionPolicy.identity), this.count + 1);
        const exposed = this.#expose(root);
        return this.#derive(new Tree(0, exposed.element, exposed.priority, this.#skewInsert(singleton, exposed.children), this.actionPolicy.identity), this.count + 1);
    }

    /**
     * A heap with the action applied to every priority *currently* present. O(1) worst case,
     * allocating O(1) new structure: one tag is composed at the root and the whole child forest is
     * reused by identity.
     *
     * Later insertions and later melds are not retroactively transformed. An empty heap, or an action
     * the policy recognizes as the identity, returns the receiver.
     */
    public transformAll(action: A): PersistentMonotoneActionHeap<E, P, A> {
        if (this.#root === undefined || this.actionPolicy.isIdentity(action)) return this;
        return this.#derive(this.#tagTree(this.#root, action), this.count);
    }

    /**
     * A heap holding every entry of both operands. O(1) worst case.
     *
     * Each operand keeps its own action history: neither heap's pending actions reach the other's
     * entries, even for actions with no inverse. A tie between the two roots favours the *receiver*.
     * Melding a heap with itself duplicates every logical occurrence.
     *
     * @throws {TypeError} The two heaps retain different action-policy objects.
     */
    public meld(other: PersistentMonotoneActionHeap<E, P, A>): PersistentMonotoneActionHeap<E, P, A> {
        if (this.actionPolicy !== other.actionPolicy) throw new TypeError("Heap melding requires the same action-policy object.");
        const left = this.#root;
        const right = other.#root;
        if (left === undefined) return other;
        if (right === undefined) return this;
        const leftWins = this.#le(left, right);
        const winner = this.#expose(leftWins ? left : right);
        const loser = leftWins ? right : left;
        return this.#derive(new Tree(0, winner.element, winner.priority, this.#skewInsert(loser, winner.children), this.actionPolicy.identity), this.count + other.count);
    }

    /**
     * The heap without one minimum entry, or `undefined` when empty. O(log n) worst case.
     *
     * Unlike insertion and melding this rebuilds the root's forest, so it is the one operation whose
     * cost grows with the heap's rank.
     */
    public deleteMinimum(): PersistentMonotoneActionHeap<E, P, A> | undefined {
        if (this.#root === undefined) return undefined;
        const root = this.#expose(this.#root);
        if (root.children === undefined) return this.#derive(undefined, 0);
        const found = this.#getMinimum(root.children);
        const minimum = this.#expose(found.tree);
        const split = this.#splitForest(minimum.rank, minimum.children);
        let merged = this.#skewMeld(this.#skewMeld(split.trees, found.remainder), split.embeddedForest);
        const zeros = this.#forestToArray(split.zeros);
        // Re-inserted in first-encountered order: the decomposition prepends each zero, so walking
        // the collected list backwards restores the order in which they were split off.
        for (let index = zeros.length - 1; index >= 0; index--) merged = this.#skewInsert(zeros[index]!, merged);
        return this.#derive(new Tree(0, minimum.element, minimum.priority, merged, this.actionPolicy.identity), this.count - 1);
    }

    /** The minimum entry and the heap that remains, or `undefined` when empty. O(log n) worst case. */
    public minimumView(): MonotoneActionMinimumView<E, P, A> | undefined {
        const root = this.#root;
        if (root === undefined) return undefined;
        const remainder = this.deleteMinimum();
        if (remainder === undefined) throw new Error("A present root must permit minimum deletion.");
        return { minimum: this.#entryOf(root), remainder };
    }

    /**
     * Number of retained tree nodes reachable from both heaps by object identity. A representation
     * probe used to show that a derived version really shares structure rather than having been
     * rebuilt, not an equality test. Theta(n).
     */
    public sharedTreeCount(other: PersistentMonotoneActionHeap<E, P, A>): number {
        const collect = (root: Tree<E, P, A> | undefined): Set<Tree<E, P, A>> => {
            const nodes = new Set<Tree<E, P, A>>();
            const pending: Tree<E, P, A>[] = root === undefined ? [] : [root];
            while (pending.length !== 0) {
                const tree = pending.pop()!;
                if (nodes.has(tree)) continue;
                nodes.add(tree);
                for (let forest = tree.children; forest !== undefined; forest = forest.rawTail) pending.push(forest.rawHead);
            }
            return nodes;
        };
        const mine = collect(this.#root);
        let shared = 0;
        for (const node of collect(other.#root)) if (mine.has(node)) shared++;
        return shared;
    }

    /** Whether both versions retain the exact same root node, two empty heaps included. O(1). */
    public sharesRootWith(other: PersistentMonotoneActionHeap<E, P, A>): boolean { return this.#root === other.#root; }
    /**
     * Whether both versions' roots retain the exact same raw child forest, two empty heaps included.
     * A whole-heap transform composes one tag at the root, so its successor must reuse the receiver's
     * entire child forest; this is the structural half of the O(1)-allocation claim. O(1).
     */
    public sharesRootChildrenWith(other: PersistentMonotoneActionHeap<E, P, A>): boolean {
        const mine = this.#root;
        const theirs = other.#root;
        if (mine === undefined || theirs === undefined) return mine === theirs;
        return mine.children === theirs.children;
    }

    /**
     * Walks the whole representation and returns its shape measurements, throwing on the first
     * violation. Theta(n), using an explicit worklist rather than recursion.
     *
     * Independently checks the bootstrapped root's rank, the fused primitive-child encoding of every
     * tree, the skew-binomial forest rank rules (nondecreasing, and only the first two may be equal),
     * the logical heap order between every tree and its children *through* their composed pending
     * actions, and agreement with the cached count. A defensive audit, not part of normal use.
     */
    public validateStructure(): PersistentMonotoneActionHeapStatistics {
        const root = this.#root;
        if (root === undefined) {
            if (this.count !== 0) throw new Error("Empty action-heap count mismatch.");
            return { count: 0, rootForestLength: 0, maximumRank: 0, maximumDepth: 0, taggedComponentCount: 0 };
        }
        if (root.rank !== 0) throw new Error("Global action-heap root must have rank zero.");
        let taggedComponentCount = 0;
        const rootForestLength = this.#validateSkewForest(this.#expose(root).children);
        const pending: Array<readonly [Tree<E, P, A>, number]> = [[root, 1]];
        let count = 0;
        let maximumRank = 0;
        let maximumDepth = 0;
        while (pending.length !== 0) {
            const [tagged, depth] = pending.pop()!;
            if (!this.actionPolicy.isIdentity(tagged.action)) taggedComponentCount++;
            const tree = this.#expose(tagged);
            if (tree.rank < 0) throw new Error("An action-heap tree has a negative rank.");
            count++;
            maximumRank = Math.max(maximumRank, tree.rank);
            maximumDepth = Math.max(maximumDepth, depth);
            this.#validateSkewForest(this.#validateFusedChildren(tree));
            for (let forest = tree.children; forest !== undefined; forest = this.#forestTail(forest)) {
                if (!this.actionPolicy.isIdentity(forest.action)) taggedComponentCount++;
                const child = this.#forestHead(forest);
                if (!this.#le(tree, child)) throw new Error("An action-heap child outranks its parent.");
                pending.push([child, depth + 1]);
            }
        }
        if (count !== this.count) throw new Error("Action-heap logical count mismatch.");
        return { count, rootForestLength, maximumRank, maximumDepth, taggedComponentCount };
    }

    /**
     * Iterates every entry in Theta(n), in unspecified structural order. Every yielded priority is
     * fully composed: children are reached only through the tag-composing accessors, so a pending
     * action is never skipped and never applied twice. Repeated `deleteMinimum` gives ascending order.
     */
    public *[Symbol.iterator](): IterableIterator<MonotoneHeapEntry<E, P>> {
        const pending: Tree<E, P, A>[] = this.#root === undefined ? [] : [this.#root];
        while (pending.length !== 0) {
            const tree = this.#expose(pending.pop()!);
            for (let forest = tree.children; forest !== undefined; forest = this.#forestTail(forest)) pending.push(this.#forestHead(forest));
            yield { element: tree.element, priority: tree.priority };
        }
    }

    // -- action plumbing -------------------------------------------------------------------------

    #derive(root: Tree<E, P, A> | undefined, count: number): PersistentMonotoneActionHeap<E, P, A> {
        return new PersistentMonotoneActionHeap<E, P, A>(root, count, this.actionPolicy);
    }
    #entryOf(tree: Tree<E, P, A>): MonotoneHeapEntry<E, P> { return { element: tree.element, priority: this.actionPolicy.apply(tree.action, tree.priority) }; }
    #le(left: Tree<E, P, A>, right: Tree<E, P, A>): boolean {
        return this.comparator(this.actionPolicy.apply(left.action, left.priority), this.actionPolicy.apply(right.action, right.priority)) <= 0;
    }
    /** Pushes a newer action onto one tree, composing it over the tree's own pending action. */
    #tagTree(tree: Tree<E, P, A>, outer: A): Tree<E, P, A> {
        if (this.actionPolicy.isIdentity(outer)) return tree;
        return new Tree(tree.rank, tree.element, tree.priority, tree.children, this.actionPolicy.compose(outer, tree.action));
    }
    /** Pushes a newer action onto one forest suffix, composing it over the suffix's own action. */
    #tagForest(forest: Forest<E, P, A>, outer: A): Forest<E, P, A> {
        if (this.actionPolicy.isIdentity(outer)) return forest;
        return new Forest(forest.rawHead, forest.rawTail, this.actionPolicy.compose(outer, forest.action));
    }
    /** Materializes one tree's pending action into its own priority and its child forest. */
    #expose(tree: Tree<E, P, A>): Tree<E, P, A> {
        const action = tree.action;
        if (this.actionPolicy.isIdentity(action)) return tree;
        const children = tree.children;
        return new Tree(tree.rank, tree.element, this.actionPolicy.apply(action, tree.priority), children === undefined ? undefined : this.#tagForest(children, action), this.actionPolicy.identity);
    }
    /** Builds an identity-tagged spine cell, so an attached tree keeps only its own history. */
    #cons(head: Tree<E, P, A>, tail: Forest<E, P, A> | undefined): Forest<E, P, A> { return new Forest(head, tail, this.actionPolicy.identity); }
    #forestHead(forest: Forest<E, P, A>): Tree<E, P, A> { return this.#tagTree(forest.rawHead, forest.action); }
    #forestTail(forest: Forest<E, P, A>): Forest<E, P, A> | undefined { return forest.rawTail === undefined ? undefined : this.#tagForest(forest.rawTail, forest.action); }
    #forestToArray(forest: Forest<E, P, A> | undefined): Tree<E, P, A>[] {
        const trees: Tree<E, P, A>[] = [];
        for (let cursor = forest; cursor !== undefined; cursor = this.#forestTail(cursor)) trees.push(this.#forestHead(cursor));
        return trees;
    }

    // -- skew-binomial kernel over logical, tag-composed views ------------------------------------

    #skewInsert(tree: Tree<E, P, A>, forest: Forest<E, P, A> | undefined): Forest<E, P, A> {
        // Ranks are tag-invariant, so the raw tail's rank decides the link before any tagged tail
        // cell is materialized. Only the linking branch pays for the tagged tail.
        const rawTail = forest?.rawTail;
        if (forest !== undefined && rawTail !== undefined && forest.rawHead.rank === rawTail.rawHead.rank) {
            const tail = this.#tagForest(rawTail, forest.action);
            return this.#cons(this.#skewLink(tree, this.#forestHead(forest), this.#forestHead(tail)), this.#forestTail(tail));
        }
        return this.#cons(tree, forest);
    }

    #skewLink(zero: Tree<E, P, A>, first: Tree<E, P, A>, second: Tree<E, P, A>): Tree<E, P, A> {
        if (first.rank !== second.rank) throw new Error("Invalid skew-link ranks.");
        // Only the logical winner is exposed; the losing trees attach through identity-tagged spine
        // cells so they keep their own action histories.
        if (this.#le(first, zero) && this.#le(first, second)) {
            const winner = this.#expose(first);
            return new Tree(first.rank + 1, winner.element, winner.priority, this.#cons(zero, this.#cons(second, winner.children)), this.actionPolicy.identity);
        }
        if (this.#le(second, zero) && this.#le(second, first)) {
            const winner = this.#expose(second);
            return new Tree(second.rank + 1, winner.element, winner.priority, this.#cons(zero, this.#cons(first, winner.children)), this.actionPolicy.identity);
        }
        const winner = this.#expose(zero);
        return new Tree(first.rank + 1, winner.element, winner.priority, this.#cons(first, this.#cons(second, winner.children)), this.actionPolicy.identity);
    }

    #link(first: Tree<E, P, A>, second: Tree<E, P, A>): Tree<E, P, A> {
        if (first.rank !== second.rank) throw new Error("Invalid binomial-link ranks.");
        const firstWins = this.#le(first, second);
        const winner = this.#expose(firstWins ? first : second);
        const loser = firstWins ? second : first;
        return new Tree(winner.rank + 1, winner.element, winner.priority, this.#cons(loser, winner.children), this.actionPolicy.identity);
    }

    /**
     * The minimum tree of a spine and the spine that remains. A tie favours the *earliest* spine
     * occurrence: the fold runs from the last cell back to the first, and an earlier head only has to
     * compare less than *or equal*.
     */
    #getMinimum(forest: Forest<E, P, A>): MinimumSplit<E, P, A> {
        const cells: Array<readonly [Tree<E, P, A>, Forest<E, P, A> | undefined]> = [];
        for (let cursor: Forest<E, P, A> | undefined = forest; cursor !== undefined;) {
            const tail = this.#forestTail(cursor);
            cells.push([this.#forestHead(cursor), tail]);
            cursor = tail;
        }
        let tree = cells[cells.length - 1]![0];
        let remainder: Forest<E, P, A> | undefined;
        for (let index = cells.length - 2; index >= 0; index--) {
            const [head, tail] = cells[index]!;
            if (this.#le(head, tree)) { tree = head; remainder = tail; }
            else remainder = this.#cons(head, remainder);
        }
        return { tree, remainder };
    }

    /** Decomposes a minimum tree's fused children into zeros, ranked trees, and the embedded rest. */
    #splitForest(initialRank: number, initial: Forest<E, P, A> | undefined): SplitForest<E, P, A> {
        let rank = initialRank;
        let zeros: Forest<E, P, A> | undefined;
        let trees: Forest<E, P, A> | undefined;
        let forest = initial;
        while (true) {
            if (rank === 0) return { zeros, trees, embeddedForest: forest };
            if (forest === undefined) throw new Error("Invalid skew-binomial forest invariant.");
            const first = this.#forestHead(forest);
            const tail = this.#forestTail(forest);
            if (rank === 1 && tail === undefined) return { zeros, trees: this.#cons(first, trees), embeddedForest: undefined };
            if (tail === undefined) throw new Error("Invalid skew-binomial forest invariant.");
            const second = this.#forestHead(tail);
            const rest = this.#forestTail(tail);
            if (rank === 1) {
                // The rank-zero ambiguity is resolved in favour of the structural prefix.
                return second.rank === 0
                    ? { zeros: this.#cons(first, zeros), trees: this.#cons(second, trees), embeddedForest: rest }
                    : { zeros, trees: this.#cons(first, trees), embeddedForest: this.#cons(second, rest) };
            }
            if (first.rank === second.rank) return { zeros, trees: this.#cons(first, this.#cons(second, trees)), embeddedForest: rest };
            if (first.rank === 0) { zeros = this.#cons(first, zeros); trees = this.#cons(second, trees); forest = rest; }
            else { trees = this.#cons(first, trees); forest = this.#cons(second, rest); }
            rank--;
        }
    }

    #skewMeld(left: Forest<E, P, A> | undefined, right: Forest<E, P, A> | undefined): Forest<E, P, A> | undefined { return this.#unionUnique(this.#uniquify(left), this.#uniquify(right)); }
    #uniquify(forest: Forest<E, P, A> | undefined): Forest<E, P, A> | undefined { return forest === undefined ? undefined : this.#insertRanked(this.#forestHead(forest), this.#uniquify(this.#forestTail(forest))); }
    #insertRanked(tree: Tree<E, P, A>, forest: Forest<E, P, A> | undefined): Forest<E, P, A> {
        if (forest === undefined) return this.#cons(tree, undefined);
        const head = this.#forestHead(forest);
        if (tree.rank > head.rank) throw new Error("Invalid ranked insertion order.");
        return tree.rank < head.rank ? this.#cons(tree, forest) : this.#insertRanked(this.#link(tree, head), this.#forestTail(forest));
    }
    #unionUnique(left: Forest<E, P, A> | undefined, right: Forest<E, P, A> | undefined): Forest<E, P, A> | undefined {
        if (left === undefined) return right;
        if (right === undefined) return left;
        const leftHead = this.#forestHead(left);
        const rightHead = this.#forestHead(right);
        if (leftHead.rank < rightHead.rank) return this.#cons(leftHead, this.#unionUnique(this.#forestTail(left), right));
        if (leftHead.rank > rightHead.rank) return this.#cons(rightHead, this.#unionUnique(left, this.#forestTail(right)));
        return this.#insertRanked(this.#link(leftHead, rightHead), this.#unionUnique(this.#forestTail(left), this.#forestTail(right)));
    }

    // -- validation ------------------------------------------------------------------------------

    /** Walks one exposed tree's fused primitive-child encoding and returns its embedded forest. */
    #validateFusedChildren(tree: Tree<E, P, A>): Forest<E, P, A> | undefined {
        let rank = tree.rank;
        let forest = tree.children;
        while (rank > 0) {
            if (forest === undefined) throw new Error("A ranked action-heap tree is missing children.");
            const first = this.#forestHead(forest);
            const tail = this.#forestTail(forest);
            if (rank === 1) {
                if (first.rank !== 0) throw new Error("A rank-one tree must begin with a rank-zero child.");
                if (tail === undefined) return undefined;
                return this.#forestHead(tail).rank === 0 ? this.#forestTail(tail) : tail;
            }
            if (tail === undefined) throw new Error("A ranked action-heap tree has incomplete children.");
            const second = this.#forestHead(tail);
            if (first.rank === second.rank) {
                if (first.rank !== rank - 1) throw new Error("A skew-linked tree has invalid child ranks.");
                return this.#forestTail(tail);
            }
            if (first.rank === 0) {
                if (second.rank !== rank - 1) throw new Error("A skew-linked tree has an invalid ranked child.");
                forest = this.#forestTail(tail);
            } else {
                if (first.rank !== rank - 1) throw new Error("A linked tree has an invalid ranked child.");
                forest = tail;
            }
            rank--;
        }
        return forest;
    }

    #validateSkewForest(initial: Forest<E, P, A> | undefined): number {
        let length = 0;
        let previousRank = -1;
        let duplicate = false;
        for (let forest = initial; forest !== undefined; forest = this.#forestTail(forest)) {
            // Ranks are tag-invariant, so the raw head answers without materializing a tagged one.
            const rank = forest.rawHead.rank;
            if (rank < 0) throw new Error("An action-heap forest contains a negative rank.");
            if (rank < previousRank) throw new Error("Action-heap forest ranks are not nondecreasing.");
            if (rank === previousRank) {
                if (length !== 1 || duplicate) throw new Error("Only the first two forest ranks may be equal.");
                duplicate = true;
            }
            previousRank = rank;
            length++;
        }
        return length;
    }
}
