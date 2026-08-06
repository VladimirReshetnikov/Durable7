/**
 * A persistent monoid-measured Hinze-Paterson finger tree with memoized lazy spines.
 *
 * This module was previously a measured implicit AVL join tree, which delivered O(log n) endpoint
 * updates and a concatenation bound keyed to the operands' height difference. It is now the same
 * structure the reference workspaces carry: 1-4-item digits at each end, 2-3 nodes below them, and
 * a middle subtree per deep node held behind a **memoized suspension**, which is Hinze and
 * Paterson's lazy finger tree realized in a strict language, exactly as the C#, C++, and C
 * workspaces realize it.
 *
 * Bounds (with O(1) policy operations): `front`/`back` are O(1) worst-case digit reads;
 * `prepend`/`append` are O(1) amortized and O(log n) worst-case per call; `concat` is
 * O(log(min(n, m))) amortized; `splitAt`, `at`, `insertAt`, `setAt`, `removeAt`, `locate`,
 * `prefixMeasure`, `lowerBound`, and `upperBound` are O(log n); `reversedView` is O(1); iteration
 * is O(n). The amortized bounds hold under fully persistent branching histories, not merely
 * ephemeral linear use, because a forced suspension is memoized in a cell shared by every version
 * that references it: work deferred by one version and forced by another is never repeated.
 *
 * Laziness is load-bearing in exactly three places, and eager everywhere else. A digit overflow on
 * `prepend`/`append` pushes a 2-3 node into the middle *inside* a suspension, `concat` recurses
 * into the two middles *inside* a suspension, and `reversedView` mirrors digits eagerly while the
 * middle's reversal rides a suspension; every size is computed strictly at construction, so index
 * arithmetic never forces structure it does not enter. A deep node's measure is memoized
 * separately and forcing it may force the middle spine - the reason `measure` is O(1) amortized
 * rather than worst-case. A suspension whose computation throws publishes nothing and may be
 * retried, so policy failures keep every retained version valid.
 *
 * The suspended operations are defunctionalized - stored as data, not closures - and forced by an
 * explicit-stack interpreter, because organic append loops build Theta(n)-deep pending chains and
 * JavaScript engines overflow at recursion depths far below that. Memoization publishes by plain
 * assignment into the cell: JavaScript's single-threaded execution makes that assignment the
 * atomic publication primitive, where the sibling workspaces need OnceLock, CAS, or GIL notes.
 *
 * Each 2-3 node also caches its first and last descendant elements, which is what keeps
 * `lowerBound`/`upperBound` at O(log n) - each level inspects at most eight digit items by their
 * cached extremes - and makes `front`/`back` worst-case O(1).
 */
import type { MeasurePolicy } from "./measures.js";

/** Marks a suspension or memoized-measure cell that has not been computed yet. */
const PENDING: unique symbol = Symbol("pending");
type Pending = typeof PENDING;

type Policy<M> = MeasurePolicy<unknown, M>;

const OP_READY = 0;
const OP_PUSH_FRONT = 1;
const OP_PUSH_BACK = 2;
const OP_CONCAT = 3;
const OP_REVERSE = 4;

/**
 * A memoized suspension of a tree, defunctionalized.
 *
 * The four deferred operations - push an overflow node into the middle from either end,
 * concatenate two middles around regrouped nodes, and structurally reverse a middle - are stored
 * as data rather than closures, so `force` can interpret an arbitrarily long chain of pending
 * suspensions with an explicit stack. Organic append loops build Theta(n)-deep chains, far past
 * any JavaScript engine's frame budget; a closure-based force would recurse once per link and
 * die.
 *
 * Forcing memoizes the result in this cell by plain assignment - single-threaded JavaScript makes
 * that the atomic publication - so every version sharing the cell reads the same forced tree and
 * deferred work is never repeated. A throwing policy leaves every cell on the failure path
 * pending, so a failed force publishes nothing and is retryable.
 */
class Susp<M> {
    #operation: number;
    #policy: Policy<M> | undefined;
    #first: unknown;
    #second: unknown;
    #value: Tree<M> | Pending;

    private constructor(operation: number, policy: Policy<M> | undefined, first: unknown, second: unknown) {
        this.#operation = operation;
        this.#policy = policy;
        this.#first = first;
        this.#second = second;
        this.#value = PENDING;
    }

    /** A suspension already holding its result; forcing it is a field read. */
    public static ready<M>(value: Tree<M>): Susp<M> {
        const suspension = new Susp<M>(OP_READY, undefined, undefined, undefined);
        suspension.#value = value;
        return suspension;
    }

    /** Defer pushing an overflow node onto the front of the middle. */
    public static pushFront<M>(policy: Policy<M>, item: unknown, middle: Susp<M>): Susp<M> {
        return new Susp(OP_PUSH_FRONT, policy, item, middle);
    }

    /** Defer pushing an overflow node onto the back of the middle. */
    public static pushBack<M>(policy: Policy<M>, middle: Susp<M>, item: unknown): Susp<M> {
        return new Susp(OP_PUSH_BACK, policy, middle, item);
    }

    /** Defer concatenating two middles around the regrouped boundary nodes. */
    public static concat<M>(policy: Policy<M>, left: Susp<M>, between: readonly unknown[], right: Susp<M>): Susp<M> {
        return new Susp(OP_CONCAT, policy, [left, right], between);
    }

    /**
     * Defer structurally reversing the inner tree. Reversing a pending reversal short-circuits to
     * the original cell by identity - the double mirror never builds anything, and both views keep
     * sharing (and memoizing through) the one underlying suspension. A reversal of the shared
     * empty middle is the shared empty middle.
     */
    public static reverseOf<M>(policy: Policy<M>, inner: Susp<M>): Susp<M> {
        if (inner.#operation === OP_REVERSE && inner.#value === PENDING) return inner.#first as Susp<M>;
        if (inner.#value === undefined) return emptySusp<M>();
        return new Susp(OP_REVERSE, policy, inner, undefined);
    }

    /**
     * The memoized result, interpreting any chain of pending suspensions iteratively. An
     * exception from the policy propagates with every cell on the failure path still pending;
     * links that completed before the failure keep their published results.
     */
    public force(): Tree<M> {
        if (this.#value !== PENDING) return this.#value;
        const stack: Susp<M>[] = [this];
        while (stack.length !== 0) {
            const current = stack[stack.length - 1]!;
            if (current.#value !== PENDING) {
                stack.pop();
                continue;
            }
            const operation = current.#operation;
            const policy = current.#policy!;
            if (operation === OP_PUSH_FRONT) {
                const inner = current.#second as Susp<M>;
                if (inner.#value === PENDING) { stack.push(inner); continue; }
                current.#publish(cons(policy, current.#first, inner.#value));
            } else if (operation === OP_PUSH_BACK) {
                const inner = current.#first as Susp<M>;
                if (inner.#value === PENDING) { stack.push(inner); continue; }
                current.#publish(snoc(policy, inner.#value, current.#second));
            } else if (operation === OP_REVERSE) {
                const inner = current.#first as Susp<M>;
                if (inner.#value === PENDING) { stack.push(inner); continue; }
                current.#publish(reverseTree(policy, inner.#value));
            } else {
                const [left, right] = current.#first as readonly [Susp<M>, Susp<M>];
                if (left.#value === PENDING) { stack.push(left); continue; }
                if (right.#value === PENDING) { stack.push(right); continue; }
                current.#publish(app3(policy, left.#value, current.#second as readonly unknown[], right.#value));
            }
        }
        if (this.#value === PENDING) throw new Error("The force loop exited with a pending cell.");
        return this.#value;
    }

    #publish(value: Tree<M>): void {
        this.#value = value;
        this.#operation = OP_READY;
        this.#policy = undefined;
        this.#first = undefined;
        this.#second = undefined;
    }
}

/** A 2-3 node: strict size, measure, and cached first/last descendant elements. */
class Node23<M> {
    public constructor(
        public readonly children: readonly unknown[],
        public readonly size: number,
        public readonly measure: M,
        public readonly first: unknown,
        public readonly last: unknown,
    ) {}
}

/** A one-item tree; the item is an element at the surface, a 2-3 node when pulled up a level. */
class Single {
    public constructor(public readonly item: unknown) {}
}

/**
 * Digits at both ends around a suspended middle of one-level-deeper nodes.
 *
 * `size` and `middleSize` are strict - every construction site knows them arithmetically - so no
 * size read ever forces the middle. The total measure is memoized on first read.
 */
class Deep<M> {
    /** The memoized total measure; reading it may force the middle spine once. */
    public measureCell: M | Pending = PENDING;
    public constructor(
        public readonly size: number,
        public readonly prefix: readonly unknown[],
        public readonly middle: Susp<M>,
        public readonly middleSize: number,
        public readonly suffix: readonly unknown[],
    ) {}
}

type Tree<M> = Single | Deep<M> | undefined;

const sharedEmptySusp = Susp.ready<unknown>(undefined);
function emptySusp<M>(): Susp<M> { return sharedEmptySusp as Susp<M>; }

// --- item helpers: an item is a user element at level zero, a 2-3 node below --------------------

function itemSize(item: unknown): number { return item instanceof Node23 ? item.size : 1; }

function itemMeasure<M>(policy: Policy<M>, item: unknown): M {
    return item instanceof Node23 ? (item as Node23<M>).measure : policy.measure(item);
}

function itemFirst(item: unknown): unknown { return item instanceof Node23 ? item.first : item; }

function itemLast(item: unknown): unknown { return item instanceof Node23 ? item.last : item; }

function makeNode<M>(policy: Policy<M>, children: readonly unknown[]): Node23<M> {
    let size = itemSize(children[0]);
    let measure = itemMeasure(policy, children[0]);
    for (let position = 1; position < children.length; position++) {
        size += itemSize(children[position]);
        measure = policy.combine(measure, itemMeasure(policy, children[position]));
    }
    return new Node23(children, size, measure, itemFirst(children[0]), itemLast(children[children.length - 1]));
}

function digitSize(digit: readonly unknown[]): number {
    let size = 0;
    for (const item of digit) size += itemSize(item);
    return size;
}

function digitMeasure<M>(policy: Policy<M>, digit: readonly unknown[]): M {
    let measure = itemMeasure(policy, digit[0]);
    for (let position = 1; position < digit.length; position++) {
        measure = policy.combine(measure, itemMeasure(policy, digit[position]));
    }
    return measure;
}

// --- tree helpers -------------------------------------------------------------------------------

function treeSize<M>(tree: Tree<M>): number {
    if (tree === undefined) return 0;
    if (tree instanceof Single) return itemSize(tree.item);
    return tree.size;
}

function treeMeasure<M>(policy: Policy<M>, tree: Tree<M>): M {
    if (tree === undefined) return policy.identity;
    if (tree instanceof Single) return itemMeasure(policy, tree.item);
    if (tree.measureCell !== PENDING) return tree.measureCell;
    // Compute fully before publishing, so a throwing policy leaves the cell pending and retryable.
    let measure = digitMeasure(policy, tree.prefix);
    const middle = tree.middle.force();
    if (middle !== undefined) measure = policy.combine(measure, treeMeasure(policy, middle));
    measure = policy.combine(measure, digitMeasure(policy, tree.suffix));
    tree.measureCell = measure;
    return measure;
}

function treeFirst<M>(tree: Single | Deep<M>): unknown {
    return tree instanceof Single ? itemFirst(tree.item) : itemFirst(tree.prefix[0]);
}

function treeLast<M>(tree: Single | Deep<M>): unknown {
    return tree instanceof Single ? itemLast(tree.item) : itemLast(tree.suffix[tree.suffix.length - 1]);
}

function deep<M>(prefix: readonly unknown[], middle: Susp<M>, middleSize: number, suffix: readonly unknown[]): Deep<M> {
    return new Deep(digitSize(prefix) + middleSize + digitSize(suffix), prefix, middle, middleSize, suffix);
}

function digitToTree<M>(digit: readonly unknown[]): Tree<M> {
    if (digit.length === 1) return new Single(digit[0]);
    const half = digit.length >> 1;
    return deep<M>(digit.slice(0, half), emptySusp(), 0, digit.slice(half));
}

// --- endpoint operations ------------------------------------------------------------------------

function cons<M>(policy: Policy<M>, item: unknown, tree: Tree<M>): Tree<M> {
    if (tree === undefined) return new Single(item);
    if (tree instanceof Single) return deep<M>([item], emptySusp(), 0, [tree.item]);
    if (tree.prefix.length < 4) {
        return deep([item, ...tree.prefix], tree.middle, tree.middleSize, tree.suffix);
    }
    const overflow = makeNode(policy, tree.prefix.slice(1));
    return deep(
        [item, tree.prefix[0]],
        // The push into the middle is the suspended step Hinze-Paterson amortization needs.
        Susp.pushFront(policy, overflow, tree.middle),
        tree.middleSize + overflow.size,
        tree.suffix,
    );
}

function snoc<M>(policy: Policy<M>, tree: Tree<M>, item: unknown): Tree<M> {
    if (tree === undefined) return new Single(item);
    if (tree instanceof Single) return deep<M>([tree.item], emptySusp(), 0, [item]);
    if (tree.suffix.length < 4) {
        return deep(tree.prefix, tree.middle, tree.middleSize, [...tree.suffix, item]);
    }
    const overflow = makeNode(policy, tree.suffix.slice(0, 3));
    return deep(
        tree.prefix,
        Susp.pushBack(policy, tree.middle, overflow),
        tree.middleSize + overflow.size,
        [tree.suffix[3], item],
    );
}

function viewLeft<M>(policy: Policy<M>, tree: Single | Deep<M>): readonly [unknown, Tree<M>] {
    if (tree instanceof Single) return [tree.item, undefined];
    const head = tree.prefix[0];
    if (tree.prefix.length > 1) {
        return [head, deep(tree.prefix.slice(1), tree.middle, tree.middleSize, tree.suffix)];
    }
    const middle = tree.middle.force();
    if (middle === undefined) return [head, digitToTree<M>(tree.suffix)];
    const [node, rest] = viewLeft(policy, middle);
    return [head, deep(itemDigit(node), Susp.ready(rest), tree.middleSize - itemSize(node), tree.suffix)];
}

function viewRight<M>(policy: Policy<M>, tree: Single | Deep<M>): readonly [Tree<M>, unknown] {
    if (tree instanceof Single) return [undefined, tree.item];
    const last = tree.suffix[tree.suffix.length - 1];
    if (tree.suffix.length > 1) {
        return [deep(tree.prefix, tree.middle, tree.middleSize, tree.suffix.slice(0, -1)), last];
    }
    const middle = tree.middle.force();
    if (middle === undefined) return [digitToTree<M>(tree.prefix), last];
    const [rest, node] = viewRight(policy, middle);
    return [deep(tree.prefix, Susp.ready(rest), tree.middleSize - itemSize(node), itemDigit(node)), last];
}

// --- concatenation ------------------------------------------------------------------------------

/** Group 2..12 items into 2-3 nodes, standard Hinze-Paterson grouping. */
function groupNodes<M>(policy: Policy<M>, items: readonly unknown[]): Node23<M>[] {
    const nodes: Node23<M>[] = [];
    let index = 0;
    let remaining = items.length;
    while (remaining > 0) {
        if (remaining === 2 || remaining === 4) {
            nodes.push(makeNode(policy, [items[index], items[index + 1]]));
            index += 2;
            remaining -= 2;
        } else {
            nodes.push(makeNode(policy, [items[index], items[index + 1], items[index + 2]]));
            index += 3;
            remaining -= 3;
        }
    }
    return nodes;
}

function app3<M>(policy: Policy<M>, left: Tree<M>, between: readonly unknown[], right: Tree<M>): Tree<M> {
    if (left === undefined) {
        let result: Tree<M> = right;
        for (let position = between.length - 1; position >= 0; position--) result = cons(policy, between[position], result);
        return result;
    }
    if (right === undefined) {
        let result: Tree<M> = left;
        for (const item of between) result = snoc(policy, result, item);
        return result;
    }
    if (left instanceof Single) return cons(policy, left.item, app3(policy, undefined, between, right));
    if (right instanceof Single) return snoc(policy, app3(policy, left, between, undefined), right.item);
    const nodes = groupNodes(policy, [...left.suffix, ...between, ...right.prefix]);
    let nodesSize = 0;
    for (const node of nodes) nodesSize += node.size;
    return deep(
        left.prefix,
        // Concatenation recurses into both middles inside a suspension, which is what makes it
        // O(log(min(n, m))) amortized instead of costing its whole recursion up front.
        Susp.concat(policy, left.middle, nodes, right.middle),
        left.middleSize + nodesSize + right.middleSize,
        right.suffix,
    );
}

// --- index-directed access and splitting --------------------------------------------------------

function itemAt(item: unknown, index: number): unknown {
    while (item instanceof Node23) {
        for (const child of item.children) {
            const childSize = itemSize(child);
            if (index < childSize) { item = child; break; }
            index -= childSize;
        }
    }
    return item;
}

function treeAt<M>(tree: Tree<M>, index: number): unknown {
    outer: for (;;) {
        if (tree === undefined) throw new Error("Index descent left the tree.");
        if (tree instanceof Single) return itemAt(tree.item, index);
        for (const item of tree.prefix) {
            const size = itemSize(item);
            if (index < size) return itemAt(item, index);
            index -= size;
        }
        if (index < tree.middleSize) {
            tree = tree.middle.force();
            continue outer;
        }
        index -= tree.middleSize;
        for (const item of tree.suffix) {
            const size = itemSize(item);
            if (index < size) return itemAt(item, index);
            index -= size;
        }
        throw new Error("Index descent passed the suffix.");
    }
}

interface DigitSplit {
    readonly before: readonly unknown[];
    readonly item: unknown;
    readonly after: readonly unknown[];
    readonly inner: number;
}

/** Split a digit at an element index, returning the item hit and the index into it. */
function splitDigit(digit: readonly unknown[], index: number): DigitSplit {
    for (let position = 0; position < digit.length; position++) {
        const item = digit[position];
        const size = itemSize(item);
        if (index < size) return { before: digit.slice(0, position), item, after: digit.slice(position + 1), inner: index };
        index -= size;
    }
    throw new Error("Digit split index out of range.");
}

function itemDigit(item: unknown): readonly unknown[] {
    return item instanceof Node23 ? item.children : [item];
}

/**
 * Mirror one item, recombining the cached summaries in reversed order. A reversed node's measure
 * is not the cached measure unless the monoid is commutative, so the rebuild pays O(1) combines
 * per node to stay correct under every monoid; the recombination happens only when the enclosing
 * reversal suspension is forced.
 */
function reverseItem<M>(policy: Policy<M>, item: unknown): unknown {
    if (!(item instanceof Node23)) return item;
    const children: unknown[] = [];
    for (let position = item.children.length - 1; position >= 0; position--) {
        children.push(reverseItem(policy, item.children[position]));
    }
    let measure = itemMeasure(policy, children[0]);
    for (let position = 1; position < children.length; position++) {
        measure = policy.combine(measure, itemMeasure(policy, children[position]));
    }
    return new Node23(children, item.size, measure, itemFirst(children[0]), itemLast(children[children.length - 1]));
}

function reverseDigit<M>(policy: Policy<M>, digit: readonly unknown[]): readonly unknown[] {
    const reversed: unknown[] = [];
    for (let position = digit.length - 1; position >= 0; position--) reversed.push(reverseItem(policy, digit[position]));
    return reversed;
}

/** Mirror a tree with the middle's reversal deferred: O(1) immediate work per level. */
function reverseTree<M>(policy: Policy<M>, tree: Tree<M>): Tree<M> {
    if (tree === undefined) return undefined;
    if (tree instanceof Single) return new Single(reverseItem(policy, tree.item));
    return new Deep(
        tree.size,
        reverseDigit(policy, tree.suffix),
        Susp.reverseOf(policy, tree.middle),
        tree.middleSize,
        reverseDigit(policy, tree.prefix),
    );
}

/** Rebuild a tree whose prefix digit may be empty. */
function deepLeft<M>(
    policy: Policy<M>,
    before: readonly unknown[],
    middle: Susp<M>,
    middleSize: number,
    suffix: readonly unknown[],
): Tree<M> {
    if (before.length !== 0) return deep(before, middle, middleSize, suffix);
    const forced = middle.force();
    if (forced === undefined) return digitToTree<M>(suffix);
    const [node, rest] = viewLeft(policy, forced);
    return deep(itemDigit(node), Susp.ready(rest), middleSize - itemSize(node), suffix);
}

/** Rebuild a tree whose suffix digit may be empty. */
function deepRight<M>(
    policy: Policy<M>,
    prefix: readonly unknown[],
    middle: Susp<M>,
    middleSize: number,
    after: readonly unknown[],
): Tree<M> {
    if (after.length !== 0) return deep(prefix, middle, middleSize, after);
    const forced = middle.force();
    if (forced === undefined) return digitToTree<M>(prefix);
    const [rest, node] = viewRight(policy, forced);
    return deep(prefix, Susp.ready(rest), middleSize - itemSize(node), itemDigit(node));
}

interface ItemSplit {
    readonly before: readonly unknown[];
    readonly element: unknown;
    readonly after: readonly unknown[];
}

/** Split an item at an element index, collapsing its path into flat item lists. */
function splitItem(item: unknown, index: number): ItemSplit {
    const before: unknown[] = [];
    let after: unknown[] = [];
    while (item instanceof Node23) {
        let found: unknown = undefined;
        let hit = false;
        for (let position = 0; position < item.children.length; position++) {
            const child = item.children[position];
            if (hit) continue;
            const childSize = itemSize(child);
            if (index < childSize) {
                found = child;
                hit = true;
                after = [...item.children.slice(position + 1), ...after];
            } else {
                index -= childSize;
                before.push(child);
            }
        }
        if (!hit) throw new Error("Item split index out of range.");
        item = found;
    }
    return { before, element: item, after };
}

/** Split at an element index: the elements before it, the element itself, and those after it. */
function splitTree<M>(policy: Policy<M>, tree: Single | Deep<M>, index: number): readonly [Tree<M>, unknown, Tree<M>] {
    if (tree instanceof Single) {
        const split = splitItem(tree.item, index);
        let left: Tree<M> = undefined;
        for (const item of split.before) left = snoc(policy, left, item);
        let right: Tree<M> = undefined;
        for (let position = split.after.length - 1; position >= 0; position--) right = cons(policy, split.after[position], right);
        return [left, split.element, right];
    }
    const prefixSize = digitSize(tree.prefix);
    if (index < prefixSize) {
        const digit = splitDigit(tree.prefix, index);
        const item = splitItem(digit.item, digit.inner);
        let left: Tree<M> = undefined;
        for (const piece of [...digit.before, ...item.before]) left = snoc(policy, left, piece);
        let right = deepLeft(policy, digit.after, tree.middle, tree.middleSize, tree.suffix);
        for (let position = item.after.length - 1; position >= 0; position--) right = cons(policy, item.after[position], right);
        return [left, item.element, right];
    }
    index -= prefixSize;
    if (index < tree.middleSize) {
        // The recursion already collapses down to the element level, so its halves are complete.
        const middle = tree.middle.force();
        if (middle === undefined) throw new Error("A sized middle must force to a tree.");
        const [middleLeft, element, middleRight] = splitTree(policy, middle, index);
        const left = deepRight(policy, tree.prefix, Susp.ready(middleLeft), treeSize(middleLeft), []);
        const right = deepLeft(policy, [], Susp.ready(middleRight), treeSize(middleRight), tree.suffix);
        return [left, element, right];
    }
    index -= tree.middleSize;
    const digit = splitDigit(tree.suffix, index);
    const item = splitItem(digit.item, digit.inner);
    let left = deepRight(policy, tree.prefix, tree.middle, tree.middleSize, digit.before);
    for (const piece of item.before) left = snoc(policy, left, piece);
    let right: Tree<M> = undefined;
    const afterPieces = [...item.after, ...digit.after];
    for (let position = afterPieces.length - 1; position >= 0; position--) right = cons(policy, afterPieces[position], right);
    return [left, item.element, right];
}

/**
 * Build a tree bottom-up with ready middles: bulk construction defers nothing.
 *
 * Deferral pays off when later operations may never force the work; a bulk build's caller almost
 * always consumes the result, so suspending every third append would only add cells to force and
 * then discard. Grouping level by level costs the same O(n) with none of that churn.
 */
function buildEager<M>(policy: Policy<M>, items: readonly unknown[]): Tree<M> {
    const count = items.length;
    if (count === 0) return undefined;
    if (count === 1) return new Single(items[0]);
    if (count <= 8) {
        const half = count >> 1;
        return deep<M>(items.slice(0, half), emptySusp(), 0, items.slice(half));
    }
    const middleItems = groupNodes(policy, items.slice(3, -3));
    const middle = buildEager(policy, middleItems);
    return deep(items.slice(0, 3), Susp.ready(middle), treeSize(middle), items.slice(-3));
}

// --- iteration and diagnostics ------------------------------------------------------------------

function* iterateItem(item: unknown): Generator<unknown, void> {
    if (item instanceof Node23) {
        for (const child of item.children) yield* iterateItem(child);
    } else {
        yield item;
    }
}

function* iterateTree<M>(tree: Tree<M>): Generator<unknown, void> {
    if (tree === undefined) return;
    if (tree instanceof Single) {
        yield* iterateItem(tree.item);
        return;
    }
    for (const item of tree.prefix) yield* iterateItem(item);
    yield* iterateTree(tree.middle.force());
    for (const item of tree.suffix) yield* iterateItem(item);
}

/**
 * Collect every structural node reachable from the tree, forcing suspended middles: deferred
 * structure captures shared subtrees inside pending suspensions where no identity walk can see
 * them, so a non-forcing probe would report false negatives. The shared empty-middle singleton is
 * excluded - it appears in every shallow deep node, and counting it would make any two nonempty
 * sequences "share".
 */
function collectNodes<M>(tree: Tree<M>, into: Set<object>): void {
    const stack: unknown[] = [tree];
    while (stack.length !== 0) {
        const current = stack.pop();
        if (current === undefined) continue;
        if (current instanceof Single) {
            into.add(current);
            if (current.item instanceof Node23) stack.push(current.item);
        } else if (current instanceof Deep) {
            into.add(current);
            if (current.middle !== sharedEmptySusp) into.add(current.middle);
            for (const item of current.prefix) if (item instanceof Node23) stack.push(item);
            for (const item of current.suffix) if (item instanceof Node23) stack.push(item);
            stack.push(current.middle.force());
        } else if (current instanceof Node23) {
            into.add(current);
            for (const child of current.children) if (child instanceof Node23) stack.push(child);
        }
    }
}

function treeLastItem<M>(tree: Single | Deep<M>): unknown {
    return tree instanceof Single ? tree.item : tree.suffix[tree.suffix.length - 1];
}

/** The two sequences produced by a split; both share structure with the original. */
export interface SequenceSplit<T, M> {
    /** The elements before the split point. */
    readonly left: MeasuredSequence<T, M>;
    /** The elements at and after the split point. */
    readonly right: MeasuredSequence<T, M>;
}

/** Where a measure-directed search landed, reported without splitting the sequence. */
export interface SequenceLocate<T, M> {
    /** The located element's index, or the length on a miss. */
    readonly index: number;
    /** The combined measure of every element before the gap. */
    readonly measureBefore: M;
    /** The value. */
    readonly value: T | undefined;
    /** Whether a prefix actually satisfied the predicate. */
    readonly found: boolean;
}

/** Internal persistent measured finger tree shared by TypeScript collection facades. */
export class MeasuredSequence<T, M> implements Iterable<T> {
    readonly #root: Tree<M>;
    /** The retained policy defining equivalence. */
    public readonly policy: MeasurePolicy<T, M>;

    private constructor(root: Tree<M>, policy: MeasurePolicy<T, M>) {
        this.#root = root;
        this.policy = policy;
    }

    /** The empty sequence, retaining the supplied policy objects. */
    public static empty<T, M>(policy: MeasurePolicy<T, M>): MeasuredSequence<T, M> { return new MeasuredSequence(undefined, policy); }
    /** Build a sequence from the given elements in one eager bottom-up O(n) pass. */
    public static from<T, M>(values: Iterable<T>, policy: MeasurePolicy<T, M>): MeasuredSequence<T, M> {
        return new MeasuredSequence(buildEager<M>(policy, Array.from(values)), policy);
    }
    /** Number of elements, read from strict cached sizes. */
    public get size(): number { return treeSize(this.#root); }
    /** Whether the sequence holds no elements. */
    public get isEmpty(): boolean { return this.#root === undefined; }
    /**
     * The combined measure of every element, in sequence order. O(1) amortized: the root's
     * measure is memoized, and the first read of a fresh spine may force a chain of suspended
     * middles before memoizing.
     */
    public get measure(): M { return treeMeasure(this.policy, this.#root); }
    /** The first element, or `undefined` when empty. O(1) worst-case: a digit read. */
    public front(): T | undefined { return this.#root === undefined ? undefined : treeFirst(this.#root) as T; }
    /** The last element, or `undefined` when empty. O(1) worst-case: a digit read. */
    public back(): T | undefined { return this.#root === undefined ? undefined : treeLast(this.#root) as T; }
    /**
     * The element at the given index, or `undefined` when out of range. Descends by strict cached
     * sizes, so it forces only the middles it actually enters.
     */
    public at(index: number): T | undefined {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) return undefined;
        return treeAt(this.#root, index) as T;
    }
    /** A sequence with the value added at the front. O(1) amortized. */
    public prepend(value: T): MeasuredSequence<T, M> { return new MeasuredSequence(cons(this.policy, value, this.#root), this.policy); }
    /** A sequence with the value added at the back. O(1) amortized. */
    public append(value: T): MeasuredSequence<T, M> { return new MeasuredSequence(snoc(this.policy, this.#root, value), this.policy); }
    /**
     * This sequence's elements followed by the other's. O(log(min(n, m))) amortized: the
     * recursion into the two middles is suspended, and only the digits at the boundary are
     * regrouped into 2-3 nodes immediately.
     */
    public concat(other: MeasuredSequence<T, M>): MeasuredSequence<T, M> {
        if (this.policy !== other.policy) throw new TypeError("Sequences must retain the same measure policy object.");
        if (other.isEmpty) return this;
        if (this.isEmpty) return other;
        return new MeasuredSequence(app3(this.policy, this.#root, [], other.#root), this.policy);
    }
    /**
     * The elements before the index and those from it on; both halves share structure with the
     * receiver. Splitting at either end shares the receiver's root rather than rebuilding it.
     */
    public splitAt(index: number): SequenceSplit<T, M> | undefined {
        if (!Number.isInteger(index) || index < 0 || index > this.size) return undefined;
        if (index === 0) return { left: MeasuredSequence.empty(this.policy), right: this };
        if (index === this.size) return { left: this, right: MeasuredSequence.empty(this.policy) };
        const [left, element, right] = splitTree(this.policy, this.#root!, index);
        return {
            left: new MeasuredSequence<T, M>(left, this.policy),
            right: new MeasuredSequence<T, M>(cons(this.policy, element, right), this.policy),
        };
    }
    /** A sequence with the value inserted so that it ends up at the given index. */
    public insertAt(index: number, value: T): MeasuredSequence<T, M> | undefined {
        const split = this.splitAt(index);
        return split === undefined ? undefined : split.left.append(value).concat(split.right);
    }
    /** A sequence with the element at the given index replaced. */
    public setAt(index: number, value: T): MeasuredSequence<T, M> | undefined {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) return undefined;
        const current = this.at(index);
        if (Object.is(current, value)) return this;
        const [left, , right] = splitTree(this.policy, this.#root!, index);
        return new MeasuredSequence(app3(this.policy, snoc(this.policy, left, value), [], right), this.policy);
    }
    /** A sequence without the element at the given index. */
    public removeAt(index: number): MeasuredSequence<T, M> | undefined {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) return undefined;
        const [left, , right] = splitTree(this.policy, this.#root!, index);
        return new MeasuredSequence(app3(this.policy, left, [], right), this.policy);
    }
    /**
     * The combined measure of the first `count` elements, summed from cached node and digit
     * measures on the way down rather than element by element.
     */
    public prefixMeasure(count: number): M | undefined {
        if (!Number.isInteger(count) || count < 0 || count > this.size) return undefined;
        if (count === 0) return this.policy.identity;
        if (count === this.size) return this.measure;
        const policy: Policy<M> = this.policy;
        let result = policy.identity;

        const takeItems = (items: readonly unknown[], remaining: number): number => {
            for (const item of items) {
                if (remaining === 0) return 0;
                const size = itemSize(item);
                if (size <= remaining) {
                    result = policy.combine(result, itemMeasure(policy, item));
                    remaining -= size;
                } else if (item instanceof Node23) {
                    remaining = takeItems(item.children, remaining);
                } else {
                    throw new Error("An element has size one.");
                }
            }
            return remaining;
        };
        const takeTree = (tree: Tree<M>, remaining: number): number => {
            if (tree === undefined || remaining === 0) return remaining;
            if (tree instanceof Single) return takeItems([tree.item], remaining);
            if (tree.size <= remaining) {
                result = policy.combine(result, treeMeasure(policy, tree));
                return remaining - tree.size;
            }
            remaining = takeItems(tree.prefix, remaining);
            if (remaining > 0) remaining = takeTree(tree.middle.force(), remaining);
            if (remaining > 0) remaining = takeItems(tree.suffix, remaining);
            return remaining;
        };
        const leftover = takeTree(this.#root, count);
        if (leftover !== 0) throw new Error("Prefix descent must consume the requested count.");
        return result;
    }
    /**
     * Find the first position whose inclusive prefix measure satisfies the predicate. Because
     * every node caches its measure, the search descends without visiting the elements it skips.
     * The predicate is expected to be monotone.
     */
    public locate(predicate: (prefix: M) => boolean): SequenceLocate<T, M> {
        const policy: Policy<M> = this.policy;
        let before = policy.identity;
        let index = 0;

        const descendItem = (item: unknown): SequenceLocate<T, M> => {
            while (item instanceof Node23) {
                for (const child of item.children) {
                    const withChild = policy.combine(before, itemMeasure(policy, child));
                    if (predicate(withChild)) { item = child; break; }
                    before = withChild;
                    index += itemSize(child);
                }
            }
            return { index, measureBefore: before, value: item as T, found: true };
        };
        const scanItems = (items: readonly unknown[]): SequenceLocate<T, M> | undefined => {
            for (const item of items) {
                const withItem = policy.combine(before, itemMeasure(policy, item));
                if (predicate(withItem)) return descendItem(item);
                before = withItem;
                index += itemSize(item);
            }
            return undefined;
        };
        const scanTree = (tree: Tree<M>): SequenceLocate<T, M> | undefined => {
            if (tree === undefined) return undefined;
            if (tree instanceof Single) return scanItems([tree.item]);
            const withTree = policy.combine(before, treeMeasure(policy, tree));
            if (!predicate(withTree)) {
                before = withTree;
                index += tree.size;
                return undefined;
            }
            let found = scanItems(tree.prefix);
            if (found === undefined) found = scanTree(tree.middle.force());
            if (found === undefined) found = scanItems(tree.suffix);
            return found;
        };
        const found = scanTree(this.#root);
        if (found !== undefined) return found;
        return { index: this.size, measureBefore: before, value: undefined, found: false };
    }
    /**
     * Index of the first element not ordering below the probe, assuming the sequence is already
     * sorted by the given comparison. One descent guided by each item's cached last element:
     * O(log n).
     */
    public lowerBound(probe: T, comparator: (value: T, probe: T) => number): number {
        return this.#bound(probe, comparator, 0);
    }
    /**
     * Index just past the elements equivalent to the probe, assuming the sequence is already
     * sorted.
     */
    public upperBound(probe: T, comparator: (value: T, probe: T) => number): number {
        return this.#bound(probe, comparator, 1);
    }
    #bound(probe: T, comparator: (value: T, probe: T) => number, threshold: number): number {
        const reaches = (item: unknown): boolean => comparator(itemLast(item) as T, probe) >= threshold;
        let index = 0;

        const descendItem = (item: unknown): number => {
            while (item instanceof Node23) {
                for (const child of item.children) {
                    if (reaches(child)) { item = child; break; }
                    index += itemSize(child);
                }
            }
            return index;
        };
        const scanItems = (items: readonly unknown[]): number | undefined => {
            for (const item of items) {
                if (reaches(item)) return descendItem(item);
                index += itemSize(item);
            }
            return undefined;
        };
        const scanTree = (tree: Tree<M>): number | undefined => {
            if (tree === undefined) return undefined;
            if (tree instanceof Single) return scanItems([tree.item]);
            let found = scanItems(tree.prefix);
            if (found !== undefined) return found;
            if (tree.middleSize > 0) {
                const forced = tree.middle.force()!;
                if (reaches(treeLastItem(forced))) {
                    found = scanTree(forced);
                    if (found !== undefined) return found;
                } else {
                    index += tree.middleSize;
                }
            }
            return scanItems(tree.suffix);
        };
        const found = scanTree(this.#root);
        return found === undefined ? this.size : found;
    }
    /**
     * The sequence in reverse order, sharing storage lazily: digits mirror eagerly, the middle's
     * reversal rides a suspension, and reversing a still-pending reversal returns the original
     * cell by identity, so a double mirror rebuilds nothing. O(1) immediate work.
     *
     * Correct under every monoid, commutative or not: a mirrored node's summary is recombined in
     * reversed order when the reversal is actually forced, rather than reusing the cached forward
     * measure (which only a commutative measure would permit, the contract the Python workspace
     * documents instead).
     */
    public reversedView(): MeasuredSequence<T, M> {
        return new MeasuredSequence(reverseTree<M>(this.policy, this.#root), this.policy);
    }
    /**
     * Whether the two sequences have any node in common by identity, used to show that a derived
     * version really shares structure. The walk forces suspended middles: deferred structure
     * captures shared subtrees inside pending suspensions where no identity walk can see them.
     * Forcing replays the deferred construction work - once, memoized, shared by every version -
     * so probing an already-forced spine costs nothing.
     */
    public sharesStructureWith(other: MeasuredSequence<T, M>): boolean {
        if (this.#root === other.#root) return true;
        if (this.#root === undefined || other.#root === undefined) return false;
        const mine = new Set<object>();
        collectNodes(this.#root, mine);
        const theirs = new Set<object>();
        collectNodes(other.#root, theirs);
        for (const node of theirs) if (mine.has(node)) return true;
        return false;
    }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return Array.from(this); }
    public [Symbol.iterator](): IterableIterator<T> { return iterateTree(this.#root) as IterableIterator<T>; }
}
