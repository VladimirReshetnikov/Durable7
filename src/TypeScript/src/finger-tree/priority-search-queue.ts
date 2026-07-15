import { defaultComparator, type Comparator } from "./ordering.js";

export interface PrioritySearchEntry<K, P, V> { readonly key: K; readonly priority: P; readonly value: V }
export interface PrioritySearchQueueStatistics { readonly count: number; readonly height: number; readonly maximumAbsoluteBalanceFactor: number }
export interface PrioritySearchAddResult<K, P, V> { readonly added: boolean; readonly queue: PrioritySearchQueue<K, P, V> }
export interface PrioritySearchRemoveResult<K, P, V> { readonly removed: boolean; readonly entry: PrioritySearchEntry<K, P, V> | undefined; readonly queue: PrioritySearchQueue<K, P, V> }
export interface PrioritySearchMinimumView<K, P, V> { readonly entry: PrioritySearchEntry<K, P, V>; readonly remainder: PrioritySearchQueue<K, P, V> }

class Node<K, P, V> {
    public readonly height: number;
    public readonly count: number;
    public constructor(
        public readonly entry: PrioritySearchEntry<K, P, V>,
        public readonly left: Node<K, P, V> | undefined,
        public readonly right: Node<K, P, V> | undefined,
        public readonly winner: PrioritySearchEntry<K, P, V>,
    ) { this.height = 1 + Math.max(left?.height ?? 0, right?.height ?? 0); this.count = 1 + (left?.count ?? 0) + (right?.count ?? 0); }
}

/** Persistent key-ordered winner-cached AVL priority-search queue. */
export class PrioritySearchQueue<K, P, V> implements Iterable<PrioritySearchEntry<K, P, V>> {
    readonly #root: Node<K, P, V> | undefined;
    public readonly keyComparator: Comparator<K>;
    public readonly priorityComparator: Comparator<P>;
    private constructor(root: Node<K, P, V> | undefined, keyComparator: Comparator<K>, priorityComparator: Comparator<P>) { this.#root = root; this.keyComparator = keyComparator; this.priorityComparator = priorityComparator; }
    public static empty<K, P, V>(keyComparator: Comparator<K> = defaultComparator, priorityComparator: Comparator<P> = defaultComparator): PrioritySearchQueue<K, P, V> { return new PrioritySearchQueue(undefined, keyComparator, priorityComparator); }
    public static from<K, P, V>(entries: Iterable<PrioritySearchEntry<K, P, V>>, keyComparator: Comparator<K> = defaultComparator, priorityComparator: Comparator<P> = defaultComparator): PrioritySearchQueue<K, P, V> { let result = this.empty<K, P, V>(keyComparator, priorityComparator); for (const entry of entries) result = result.setItem(entry.key, entry.priority, entry.value); return result; }
    public get count(): number { return this.#root?.count ?? 0; }
    public get size(): number { return this.count; }
    public get isEmpty(): boolean { return this.#root === undefined; }
    public get height(): number { return this.#root?.height ?? 0; }
    public get minimum(): PrioritySearchEntry<K, P, V> { if (this.#root === undefined) throw new RangeError("The priority search queue is empty."); return this.#root.winner; }
    public minimumOrUndefined(): PrioritySearchEntry<K, P, V> | undefined { return this.#root?.winner; }
    #find(key: K): Node<K, P, V> | undefined { let node = this.#root; while (node !== undefined) { const comparison = this.keyComparator(key, node.entry.key); if (comparison === 0) return node; node = comparison < 0 ? node.left : node.right; } return undefined; }
    public containsKey(key: K): boolean { return this.#find(key) !== undefined; }
    public getEntry(key: K): PrioritySearchEntry<K, P, V> | undefined { return this.#find(key)?.entry; }
    #before(left: PrioritySearchEntry<K, P, V>, right: PrioritySearchEntry<K, P, V>): boolean { const priority = this.priorityComparator(left.priority, right.priority); return priority < 0 || priority === 0 && this.keyComparator(left.key, right.key) < 0; }
    #winner(entry: PrioritySearchEntry<K, P, V>, left: Node<K, P, V> | undefined, right: Node<K, P, V> | undefined): PrioritySearchEntry<K, P, V> { let winner = entry; if (left !== undefined && this.#before(left.winner, winner)) winner = left.winner; if (right !== undefined && this.#before(right.winner, winner)) winner = right.winner; return winner; }
    #node(entry: PrioritySearchEntry<K, P, V>, left: Node<K, P, V> | undefined, right: Node<K, P, V> | undefined): Node<K, P, V> { return new Node(entry, left, right, this.#winner(entry, left, right)); }
    #rotateLeft(node: Node<K, P, V>): Node<K, P, V> { const pivot = node.right!; return this.#node(pivot.entry, this.#node(node.entry, node.left, pivot.left), pivot.right); }
    #rotateRight(node: Node<K, P, V>): Node<K, P, V> { const pivot = node.left!; return this.#node(pivot.entry, pivot.left, this.#node(node.entry, pivot.right, node.right)); }
    #balance(node: Node<K, P, V>): Node<K, P, V> {
        const factor = (node.left?.height ?? 0) - (node.right?.height ?? 0);
        if (factor > 1) { const left = node.left!; return (left.left?.height ?? 0) < (left.right?.height ?? 0) ? this.#rotateRight(this.#node(node.entry, this.#rotateLeft(left), node.right)) : this.#rotateRight(node); }
        if (factor < -1) { const right = node.right!; return (right.right?.height ?? 0) < (right.left?.height ?? 0) ? this.#rotateLeft(this.#node(node.entry, node.left, this.#rotateRight(right))) : this.#rotateLeft(node); }
        return node;
    }
    #set(node: Node<K, P, V> | undefined, entry: PrioritySearchEntry<K, P, V>, overwrite: boolean): { readonly node: Node<K, P, V>; readonly added: boolean; readonly changed: boolean } {
        if (node === undefined) return { node: this.#node(entry, undefined, undefined), added: true, changed: true };
        const comparison = this.keyComparator(entry.key, node.entry.key);
        if (comparison === 0) {
            if (!overwrite || this.priorityComparator(entry.priority, node.entry.priority) === 0 && Object.is(entry.priority, node.entry.priority) && Object.is(entry.value, node.entry.value)) return { node, added: false, changed: false };
            return { node: this.#node({ key: node.entry.key, priority: entry.priority, value: entry.value }, node.left, node.right), added: false, changed: true };
        }
        if (comparison < 0) { const result = this.#set(node.left, entry, overwrite); return result.changed ? { node: this.#balance(this.#node(node.entry, result.node, node.right)), added: result.added, changed: true } : { node, added: result.added, changed: false }; }
        const result = this.#set(node.right, entry, overwrite); return result.changed ? { node: this.#balance(this.#node(node.entry, node.left, result.node)), added: result.added, changed: true } : { node, added: result.added, changed: false };
    }
    public setItem(key: K, priority: P, value: V): PrioritySearchQueue<K, P, V> { const result = this.#set(this.#root, { key, priority, value }, true); return result.node === this.#root ? this : new PrioritySearchQueue(result.node, this.keyComparator, this.priorityComparator); }
    public tryAdd(key: K, priority: P, value: V): PrioritySearchAddResult<K, P, V> { const result = this.#set(this.#root, { key, priority, value }, false); return { added: result.added, queue: result.added ? new PrioritySearchQueue(result.node, this.keyComparator, this.priorityComparator) : this }; }
    #minimumKey(node: Node<K, P, V>): Node<K, P, V> { let current = node; while (current.left !== undefined) current = current.left; return current; }
    #remove(node: Node<K, P, V> | undefined, key: K): { readonly node: Node<K, P, V> | undefined; readonly entry: PrioritySearchEntry<K, P, V> | undefined } {
        if (node === undefined) return { node: undefined, entry: undefined };
        const comparison = this.keyComparator(key, node.entry.key);
        if (comparison === 0) {
            if (node.left === undefined) return { node: node.right, entry: node.entry };
            if (node.right === undefined) return { node: node.left, entry: node.entry };
            const successor = this.#minimumKey(node.right); const right = this.#remove(node.right, successor.entry.key);
            return { node: this.#balance(this.#node(successor.entry, node.left, right.node)), entry: node.entry };
        }
        if (comparison < 0) { const result = this.#remove(node.left, key); return result.entry === undefined ? { node, entry: undefined } : { node: this.#balance(this.#node(node.entry, result.node, node.right)), entry: result.entry }; }
        const result = this.#remove(node.right, key); return result.entry === undefined ? { node, entry: undefined } : { node: this.#balance(this.#node(node.entry, node.left, result.node)), entry: result.entry };
    }
    public remove(key: K): PrioritySearchQueue<K, P, V> { const result = this.#remove(this.#root, key); return result.entry === undefined ? this : new PrioritySearchQueue(result.node, this.keyComparator, this.priorityComparator); }
    public tryRemove(key: K): PrioritySearchRemoveResult<K, P, V> { const result = this.#remove(this.#root, key); return { removed: result.entry !== undefined, entry: result.entry, queue: result.entry === undefined ? this : new PrioritySearchQueue(result.node, this.keyComparator, this.priorityComparator) }; }
    public deleteMinimum(): PrioritySearchMinimumView<K, P, V> { const entry = this.minimum; const result = this.#remove(this.#root, entry.key); if (result.entry === undefined) throw new Error("Cached winner missing by key."); return { entry, remainder: new PrioritySearchQueue(result.node, this.keyComparator, this.priorityComparator) }; }
    public minimumView(): PrioritySearchMinimumView<K, P, V> | undefined { return this.isEmpty ? undefined : this.deleteMinimum(); }
    public *enumerateAtMost(minimumKey: K, maximumKey: K, maximumPriority: P): Generator<PrioritySearchEntry<K, P, V>, void> {
        if (this.keyComparator(minimumKey, maximumKey) > 0) throw new RangeError("The minimum key must not follow the maximum key.");
        if (this.#root === undefined) return;
        const pending: Array<readonly [Node<K, P, V>, boolean]> = [[this.#root, false]];
        while (pending.length !== 0) {
            const [node, emit] = pending.pop()!; if (emit) { yield node.entry; continue; }
            if (this.priorityComparator(node.winner.priority, maximumPriority) > 0) continue;
            const lower = this.keyComparator(node.entry.key, minimumKey); const upper = this.keyComparator(node.entry.key, maximumKey);
            if (upper < 0 && node.right !== undefined) pending.push([node.right, false]);
            if (lower >= 0 && upper <= 0 && this.priorityComparator(node.entry.priority, maximumPriority) <= 0) pending.push([node, true]);
            if (lower > 0 && node.left !== undefined) pending.push([node.left, false]);
        }
    }
    public validateStructure(): PrioritySearchQueueStatistics {
        if (this.#root === undefined) return { count: 0, height: 0, maximumAbsoluteBalanceFactor: 0 };
        const pending: Array<readonly [Node<K, P, V>, K | undefined, K | undefined]> = [[this.#root, undefined, undefined]];
        let count = 0; let maximumBalance = 0;
        while (pending.length !== 0) { const [node, lower, upper] = pending.pop()!; if (lower !== undefined && this.keyComparator(node.entry.key, lower) <= 0 || upper !== undefined && this.keyComparator(node.entry.key, upper) >= 0) throw new Error("PSQ key order invariant failed."); const balance = (node.left?.height ?? 0) - (node.right?.height ?? 0); if (Math.abs(balance) > 1 || node.height !== 1 + Math.max(node.left?.height ?? 0, node.right?.height ?? 0) || node.count !== 1 + (node.left?.count ?? 0) + (node.right?.count ?? 0)) throw new Error("PSQ AVL invariant failed."); const winner = this.#winner(node.entry, node.left, node.right); if (winner !== node.winner) throw new Error("PSQ winner cache invariant failed."); count++; maximumBalance = Math.max(maximumBalance, Math.abs(balance)); if (node.right !== undefined) pending.push([node.right, node.entry.key, upper]); if (node.left !== undefined) pending.push([node.left, lower, node.entry.key]); }
        if (count !== this.count) throw new Error("PSQ count invariant failed."); return { count, height: this.height, maximumAbsoluteBalanceFactor: maximumBalance };
    }
    public sharedNodeCount(other: PrioritySearchQueue<K, P, V>): number { const nodes = new Set<Node<K, P, V>>(); const collect = (root: Node<K, P, V> | undefined, destination: Set<Node<K, P, V>>): void => { const pending = root === undefined ? [] : [root]; while (pending.length !== 0) { const node = pending.pop()!; if (destination.has(node)) continue; destination.add(node); if (node.left !== undefined) pending.push(node.left); if (node.right !== undefined) pending.push(node.right); } }; collect(this.#root, nodes); const otherNodes = new Set<Node<K, P, V>>(); collect(other.#root, otherNodes); let count = 0; for (const node of otherNodes) if (nodes.has(node)) count++; return count; }
    public *[Symbol.iterator](): IterableIterator<PrioritySearchEntry<K, P, V>> { const pending: Node<K, P, V>[] = []; let current = this.#root; while (current !== undefined || pending.length !== 0) { while (current !== undefined) { pending.push(current); current = current.left; } const node = pending.pop()!; yield node.entry; current = node.right; } }
}
