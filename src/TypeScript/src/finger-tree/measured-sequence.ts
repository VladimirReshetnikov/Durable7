/**
 * The measured implicit AVL sequence that most of this package is built on.
 *
 * Caches a monoidal measure at every node, so choosing a different measure specializes the same
 * tree into a different structure: counting gives positional access, summing gives cumulative-
 * weight search, tracking a maximum gives a priority queue.
 */
import type { MeasurePolicy } from "./measures.js";

interface SequenceNode<T, M> {
    readonly left: SequenceNode<T, M> | undefined;
    readonly value: T;
    readonly right: SequenceNode<T, M> | undefined;
    readonly height: number;
    readonly size: number;
    readonly measure: M;
}

function height<T, M>(node: SequenceNode<T, M> | undefined): number { return node?.height ?? 0; }
function size<T, M>(node: SequenceNode<T, M> | undefined): number { return node?.size ?? 0; }

function makeNode<T, M>(
    left: SequenceNode<T, M> | undefined,
    value: T,
    right: SequenceNode<T, M> | undefined,
    policy: MeasurePolicy<T, M>,
): SequenceNode<T, M> {
    return {
        left,
        value,
        right,
        height: Math.max(height(left), height(right)) + 1,
        size: size(left) + size(right) + 1,
        measure: policy.combine(policy.combine(left?.measure ?? policy.identity, policy.measure(value)), right?.measure ?? policy.identity),
    };
}

function balance<T, M>(
    left: SequenceNode<T, M> | undefined,
    value: T,
    right: SequenceNode<T, M> | undefined,
    policy: MeasurePolicy<T, M>,
): SequenceNode<T, M> {
    if (height(left) > height(right) + 1) {
        if (left === undefined) throw new Error("Invalid AVL left imbalance.");
        if (height(left.left) >= height(left.right)) {
            return makeNode(left.left, left.value, makeNode(left.right, value, right, policy), policy);
        }
        const middle = left.right;
        if (middle === undefined) throw new Error("Invalid AVL double rotation.");
        return makeNode(
            makeNode(left.left, left.value, middle.left, policy),
            middle.value,
            makeNode(middle.right, value, right, policy),
            policy,
        );
    }
    if (height(right) > height(left) + 1) {
        if (right === undefined) throw new Error("Invalid AVL right imbalance.");
        if (height(right.right) >= height(right.left)) {
            return makeNode(makeNode(left, value, right.left, policy), right.value, right.right, policy);
        }
        const middle = right.left;
        if (middle === undefined) throw new Error("Invalid AVL double rotation.");
        return makeNode(
            makeNode(left, value, middle.left, policy),
            middle.value,
            makeNode(middle.right, right.value, right.right, policy),
            policy,
        );
    }
    return makeNode(left, value, right, policy);
}

interface RemovedMin<T, M> {
    readonly value: T;
    readonly rest: SequenceNode<T, M> | undefined;
}

function removeMin<T, M>(node: SequenceNode<T, M>, policy: MeasurePolicy<T, M>): RemovedMin<T, M> {
    if (node.left === undefined) return { value: node.value, rest: node.right };
    const removed = removeMin(node.left, policy);
    return { value: removed.value, rest: balance(removed.rest, node.value, node.right, policy) };
}

function join<T, M>(
    left: SequenceNode<T, M> | undefined,
    value: T,
    right: SequenceNode<T, M> | undefined,
    policy: MeasurePolicy<T, M>,
): SequenceNode<T, M> {
    if (height(left) > height(right) + 1) {
        if (left === undefined) throw new Error("Invalid AVL join.");
        return balance(left.left, left.value, join(left.right, value, right, policy), policy);
    }
    if (height(right) > height(left) + 1) {
        if (right === undefined) throw new Error("Invalid AVL join.");
        return balance(join(left, value, right.left, policy), right.value, right.right, policy);
    }
    return makeNode(left, value, right, policy);
}

function concatNodes<T, M>(
    left: SequenceNode<T, M> | undefined,
    right: SequenceNode<T, M> | undefined,
    policy: MeasurePolicy<T, M>,
): SequenceNode<T, M> | undefined {
    if (left === undefined) return right;
    if (right === undefined) return left;
    const removed = removeMin(right, policy);
    return join(left, removed.value, removed.rest, policy);
}

interface SplitNodes<T, M> {
    readonly left: SequenceNode<T, M> | undefined;
    readonly right: SequenceNode<T, M> | undefined;
}

function splitNodes<T, M>(
    node: SequenceNode<T, M> | undefined,
    index: number,
    policy: MeasurePolicy<T, M>,
): SplitNodes<T, M> {
    if (node === undefined) return { left: undefined, right: undefined };
    const leftSize = size(node.left);
    if (index <= leftSize) {
        const split = splitNodes(node.left, index, policy);
        return { left: split.left, right: join(split.right, node.value, node.right, policy) };
    }
    const split = splitNodes(node.right, index - leftSize - 1, policy);
    return { left: join(node.left, node.value, split.left, policy), right: split.right };
}

function buildBalanced<T, M>(
    values: readonly T[],
    start: number,
    count: number,
    policy: MeasurePolicy<T, M>,
): SequenceNode<T, M> | undefined {
    if (count === 0) return undefined;
    const leftCount = Math.floor(count / 2);
    const value = values[start + leftCount];
    if (value === undefined && !(start + leftCount in values)) throw new Error("Sparse input sequences are not supported.");
    return makeNode(
        buildBalanced(values, start, leftCount, policy),
        value as T,
        buildBalanced(values, start + leftCount + 1, count - leftCount - 1, policy),
        policy,
    );
}

function* iterateNodes<T, M>(root: SequenceNode<T, M>): Generator<T, void> {
    const stack: SequenceNode<T, M>[] = [];
    let current: SequenceNode<T, M> | undefined = root;
    while (current !== undefined || stack.length !== 0) {
        while (current !== undefined) { stack.push(current); current = current.left; }
        const node = stack.pop();
        if (node === undefined) break;
        yield node.value;
        current = node.right;
    }
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

/** Internal persistent measured AVL sequence shared by TypeScript collection facades. */
export class MeasuredSequence<T, M> implements Iterable<T> {
    readonly #root: SequenceNode<T, M> | undefined;
    /** The retained policy defining equivalence. */
    public readonly policy: MeasurePolicy<T, M>;

    private constructor(root: SequenceNode<T, M> | undefined, policy: MeasurePolicy<T, M>) {
        this.#root = root;
        this.policy = policy;
    }

    /** The empty sequence, retaining the supplied policy objects. */
    public static empty<T, M>(policy: MeasurePolicy<T, M>): MeasuredSequence<T, M> { return new MeasuredSequence(undefined, policy); }
    /** Build a sequence from the given elements. */
    public static from<T, M>(values: Iterable<T>, policy: MeasurePolicy<T, M>): MeasuredSequence<T, M> {
        const materialized = Array.from(values);
        return new MeasuredSequence(buildBalanced(materialized, 0, materialized.length, policy), policy);
    }
    /** Number of elements. */
    public get size(): number { return size(this.#root); }
    /** Whether the sequence holds no elements. */
    public get isEmpty(): boolean { return this.#root === undefined; }
    /** The combined measure of every element, read from the cached root measure. */
    public get measure(): M { return this.#root?.measure ?? this.policy.identity; }
    /** The first element, or `undefined` when empty. */
    public front(): T | undefined { return this.at(0); }
    /** The last element, or `undefined` when empty. */
    public back(): T | undefined { return this.at(this.size - 1); }
    /** The element at the given index, or `undefined` when out of range. */
    public at(index: number): T | undefined {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) return undefined;
        let current = this.#root;
        let remaining = index;
        while (current !== undefined) {
            const leftSize = size(current.left);
            if (remaining < leftSize) current = current.left;
            else if (remaining === leftSize) return current.value;
            else { remaining -= leftSize + 1; current = current.right; }
        }
        return undefined;
    }
    /** A sequence with the value added at the front. */
    public prepend(value: T): MeasuredSequence<T, M> { return new MeasuredSequence(join(undefined, value, this.#root, this.policy), this.policy); }
    /** A sequence with the value added at the back. */
    public append(value: T): MeasuredSequence<T, M> { return new MeasuredSequence(join(this.#root, value, undefined, this.policy), this.policy); }
    /**
     * This sequence's elements followed by the other's, joining the two trees rather than copying
     * either.
     */
    public concat(other: MeasuredSequence<T, M>): MeasuredSequence<T, M> {
        if (this.policy !== other.policy) throw new TypeError("Sequences must retain the same measure policy object.");
        if (other.isEmpty) return this;
        if (this.isEmpty) return other;
        return new MeasuredSequence(concatNodes(this.#root, other.#root, this.policy), this.policy);
    }
    /**
     * The elements before the index and those from it on; both halves share structure with the
     * receiver.
     */
    public splitAt(index: number): SequenceSplit<T, M> | undefined {
        if (!Number.isInteger(index) || index < 0 || index > this.size) return undefined;
        if (index === 0) return { left: MeasuredSequence.empty(this.policy), right: this };
        if (index === this.size) return { left: this, right: MeasuredSequence.empty(this.policy) };
        const split = splitNodes(this.#root, index, this.policy);
        return { left: new MeasuredSequence(split.left, this.policy), right: new MeasuredSequence(split.right, this.policy) };
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
        const first = this.splitAt(index)!;
        const second = first.right.splitAt(1)!;
        return first.left.append(value).concat(second.right);
    }
    /** A sequence without the element at the given index. */
    public removeAt(index: number): MeasuredSequence<T, M> | undefined {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) return undefined;
        const first = this.splitAt(index)!;
        const second = first.right.splitAt(1)!;
        return first.left.concat(second.right);
    }
    /**
     * The combined measure of the first `count` elements, summed from cached node measures rather
     * than element by element.
     */
    public prefixMeasure(count: number): M | undefined {
        if (!Number.isInteger(count) || count < 0 || count > this.size) return undefined;
        let result = this.policy.identity;
        let remaining = count;
        let current = this.#root;
        while (current !== undefined && remaining > 0) {
            const leftSize = size(current.left);
            if (remaining <= leftSize) current = current.left;
            else {
                result = this.policy.combine(result, current.left?.measure ?? this.policy.identity);
                if (remaining === leftSize + 1) return this.policy.combine(result, this.policy.measure(current.value));
                result = this.policy.combine(result, this.policy.measure(current.value));
                remaining -= leftSize + 1;
                current = current.right;
            }
        }
        return result;
    }
    /**
     * Find the first position whose inclusive prefix measure satisfies the predicate. Because every
     * node caches its measure, the search descends without visiting the elements it skips. The
     * predicate is expected to be monotone.
     */
    public locate(predicate: (prefix: M) => boolean): SequenceLocate<T, M> {
        let before = this.policy.identity;
        let index = 0;
        let current = this.#root;
        while (current !== undefined) {
            const throughLeft = this.policy.combine(before, current.left?.measure ?? this.policy.identity);
            if (current.left !== undefined && predicate(throughLeft)) { current = current.left; continue; }
            index += size(current.left);
            const throughValue = this.policy.combine(throughLeft, this.policy.measure(current.value));
            if (predicate(throughValue)) return { index, measureBefore: throughLeft, value: current.value, found: true };
            before = throughValue;
            index++;
            current = current.right;
        }
        return { index: this.size, measureBefore: before, value: undefined, found: false };
    }
    /**
     * Index of the first element not ordering below the probe, assuming the sequence is already
     * sorted by the given comparison.
     */
    public lowerBound(probe: T, comparator: (value: T, probe: T) => number): number {
        let current = this.#root;
        let base = 0;
        let result = this.size;
        while (current !== undefined) {
            const index = base + size(current.left);
            if (comparator(current.value, probe) < 0) {
                base = index + 1;
                current = current.right;
            } else {
                result = index;
                current = current.left;
            }
        }
        return result;
    }
    /**
     * Index just past the elements equivalent to the probe, assuming the sequence is already
     * sorted.
     */
    public upperBound(probe: T, comparator: (value: T, probe: T) => number): number {
        let current = this.#root;
        let base = 0;
        let result = this.size;
        while (current !== undefined) {
            const index = base + size(current.left);
            if (comparator(current.value, probe) <= 0) {
                base = index + 1;
                current = current.right;
            } else {
                result = index;
                current = current.left;
            }
        }
        return result;
    }
    /**
     * Whether the two sequences have any node in common by identity, used to show that a derived
     * version really shares structure.
     */
    public sharesStructureWith(other: MeasuredSequence<T, M>): boolean {
        if (this.#root === other.#root) return true;
        if (this.#root === undefined || other.#root === undefined) return false;
        const nodes = new Set<SequenceNode<T, M>>();
        const stack = [this.#root];
        while (stack.length !== 0) {
            const node = stack.pop()!;
            nodes.add(node);
            if (node.left !== undefined) stack.push(node.left);
            if (node.right !== undefined) stack.push(node.right);
        }
        const probe = [other.#root];
        while (probe.length !== 0) {
            const node = probe.pop()!;
            if (nodes.has(node)) return true;
            if (node.left !== undefined) probe.push(node.left);
            if (node.right !== undefined) probe.push(node.right);
        }
        return false;
    }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return Array.from(this); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#root === undefined ? [][Symbol.iterator]() : iterateNodes(this.#root); }
}
