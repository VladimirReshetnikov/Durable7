/**
 * Persistent priority search queue: a search tree and a priority queue over the same entries.
 *
 * Indexes each entry by a unique key and orders it by priority, so lookup, insertion, deletion, and
 * re-prioritization by key all work alongside find-and-remove-the-minimum. A plain priority queue
 * can do only the second; a plain sorted map only the first.
 */
import { defaultComparator, type Comparator } from "./ordering.js";

/** One queue entry: the unique key it is addressed by, its priority, and its payload. */
export interface PrioritySearchEntry<K, P, V> { readonly key: K; readonly priority: P; readonly value: V }
/** Shape measurements returned by a successful structural audit. */
export interface PrioritySearchQueueStatistics { readonly count: number; readonly height: number; readonly maximumAbsoluteBalanceFactor: number }
/** The outcome of a non-overwriting insertion; the queue is the receiver when nothing was added. */
export interface PrioritySearchAddResult<K, P, V> { readonly added: boolean; readonly queue: PrioritySearchQueue<K, P, V> }
/**
 * The outcome of a keyed removal. The explicit flag keeps an entry whose payload is `undefined`
 * distinct from nothing having been removed.
 */
export interface PrioritySearchRemoveResult<K, P, V> { readonly removed: boolean; readonly entry: PrioritySearchEntry<K, P, V> | undefined; readonly queue: PrioritySearchQueue<K, P, V> }
/** The minimum-priority entry together with the queue that remains once it is removed. */
export interface PrioritySearchMinimumView<K, P, V> { readonly entry: PrioritySearchEntry<K, P, V>; readonly remainder: PrioritySearchQueue<K, P, V> }
/** The outcome of seeking a cursor to a key; on a miss it sits at the lower bound. */
export interface PrioritySearchCursorSearch<K, P, V> { readonly found: boolean; readonly cursor: PrioritySearchQueueCursor<K, P, V> }

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
    /** The retained comparator ordering keys. */
    public readonly keyComparator: Comparator<K>;
    /** The retained comparator ordering priorities. */
    public readonly priorityComparator: Comparator<P>;
    private constructor(root: Node<K, P, V> | undefined, keyComparator: Comparator<K>, priorityComparator: Comparator<P>) { this.#root = root; this.keyComparator = keyComparator; this.priorityComparator = priorityComparator; }
    /** The empty queue, retaining the supplied policy objects. */
    public static empty<K, P, V>(keyComparator: Comparator<K> = defaultComparator, priorityComparator: Comparator<P> = defaultComparator): PrioritySearchQueue<K, P, V> { return new PrioritySearchQueue(undefined, keyComparator, priorityComparator); }
    /** Build a queue from the given entries. */
    public static from<K, P, V>(entries: Iterable<PrioritySearchEntry<K, P, V>>, keyComparator: Comparator<K> = defaultComparator, priorityComparator: Comparator<P> = defaultComparator): PrioritySearchQueue<K, P, V> { let result = this.empty<K, P, V>(keyComparator, priorityComparator); for (const entry of entries) result = result.setItem(entry.key, entry.priority, entry.value); return result; }
    /** Number of entries. */
    public get count(): number { return this.#root?.count ?? 0; }
    /** Number of entries. */
    public get size(): number { return this.count; }
    /** Whether the queue holds no entries. */
    public get isEmpty(): boolean { return this.#root === undefined; }
    /** Tree height, exposed for structural diagnostics. */
    public get height(): number { return this.#root?.height ?? 0; }
    /** The smallest-priority entry, read from the cached root winner rather than searched for. */
    public get minimum(): PrioritySearchEntry<K, P, V> { if (this.#root === undefined) throw new RangeError("The priority search queue is empty."); return this.#root.winner; }
    /** The smallest-priority entry, or `undefined` when empty. */
    public minimumOrUndefined(): PrioritySearchEntry<K, P, V> | undefined { return this.#root?.winner; }
    #find(key: K): Node<K, P, V> | undefined { let node = this.#root; while (node !== undefined) { const comparison = this.keyComparator(key, node.entry.key); if (comparison === 0) return node; node = comparison < 0 ? node.left : node.right; } return undefined; }
    /** Whether the key is present. */
    public containsKey(key: K): boolean { return this.#find(key) !== undefined; }
    /** The stored key representative and value, or `undefined` when absent. */
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
    /** A map with the key bound to the value, adding or replacing as needed. */
    public setItem(key: K, priority: P, value: V): PrioritySearchQueue<K, P, V> { const result = this.#set(this.#root, { key, priority, value }, true); return result.node === this.#root ? this : new PrioritySearchQueue(result.node, this.keyComparator, this.priorityComparator); }
    /** Add the entry, reporting whether it was added rather than throwing on a duplicate. */
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
    /** A queue without that entry; returns the receiver when it is absent. */
    public remove(key: K): PrioritySearchQueue<K, P, V> { const result = this.#remove(this.#root, key); return result.entry === undefined ? this : new PrioritySearchQueue(result.node, this.keyComparator, this.priorityComparator); }
    /** Remove the entry and report what was removed, or `undefined` when absent. */
    public tryRemove(key: K): PrioritySearchRemoveResult<K, P, V> { const result = this.#remove(this.#root, key); return { removed: result.entry !== undefined, entry: result.entry, queue: result.entry === undefined ? this : new PrioritySearchQueue(result.node, this.keyComparator, this.priorityComparator) }; }
    /** Remove the smallest-priority entry, returning it with the remaining queue. */
    public deleteMinimum(): PrioritySearchMinimumView<K, P, V> { const entry = this.minimum; const result = this.#remove(this.#root, entry.key); if (result.entry === undefined) throw new Error("Cached winner missing by key."); return { entry, remainder: new PrioritySearchQueue(result.node, this.keyComparator, this.priorityComparator) }; }
    /** The smallest-priority entry together with the queue that remains once it is removed. */
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
    /**
     * Walk the whole queue and check its invariants, throwing on the first violation. A defensive
     * audit, not part of normal use.
     */
    public validateStructure(): PrioritySearchQueueStatistics {
        if (this.#root === undefined) return { count: 0, height: 0, maximumAbsoluteBalanceFactor: 0 };
        const pending: Array<readonly [Node<K, P, V>, K | undefined, K | undefined]> = [[this.#root, undefined, undefined]];
        let count = 0; let maximumBalance = 0;
        while (pending.length !== 0) { const [node, lower, upper] = pending.pop()!; if (lower !== undefined && this.keyComparator(node.entry.key, lower) <= 0 || upper !== undefined && this.keyComparator(node.entry.key, upper) >= 0) throw new Error("PSQ key order invariant failed."); const balance = (node.left?.height ?? 0) - (node.right?.height ?? 0); if (Math.abs(balance) > 1 || node.height !== 1 + Math.max(node.left?.height ?? 0, node.right?.height ?? 0) || node.count !== 1 + (node.left?.count ?? 0) + (node.right?.count ?? 0)) throw new Error("PSQ AVL invariant failed."); const winner = this.#winner(node.entry, node.left, node.right); if (winner !== node.winner) throw new Error("PSQ winner cache invariant failed."); count++; maximumBalance = Math.max(maximumBalance, Math.abs(balance)); if (node.right !== undefined) pending.push([node.right, node.entry.key, upper]); if (node.left !== undefined) pending.push([node.left, lower, node.entry.key]); }
        if (count !== this.count) throw new Error("PSQ count invariant failed."); return { count, height: this.height, maximumAbsoluteBalanceFactor: maximumBalance };
    }
    /**
     * Number of nodes the two queues have in common by identity, used to show that a derived
     * version really shares structure.
     */
    public sharedNodeCount(other: PrioritySearchQueue<K, P, V>): number { const nodes = new Set<Node<K, P, V>>(); const collect = (root: Node<K, P, V> | undefined, destination: Set<Node<K, P, V>>): void => { const pending = root === undefined ? [] : [root]; while (pending.length !== 0) { const node = pending.pop()!; if (destination.has(node)) continue; destination.add(node); if (node.left !== undefined) pending.push(node.left); if (node.right !== undefined) pending.push(node.right); } }; collect(this.#root, nodes); const otherNodes = new Set<Node<K, P, V>>(); collect(other.#root, otherNodes); let count = 0; for (const node of otherNodes) if (nodes.has(node)) count++; return count; }
    /** A cursor at the given gap of the queue. */
    public cursorAt(position = 0): PrioritySearchQueueCursor<K, P, V> { return new PrioritySearchQueueCursor(this, position); }
    /** A cursor before the first entry not below the probe, where it would be inserted. */
    public cursorAtLowerBound(key: K): PrioritySearchQueueCursor<K, P, V> { return this.cursorAt(this.#boundRank(key, false)); }
    /** A cursor after any entry equivalent to the probe. */
    public cursorAtUpperBound(key: K): PrioritySearchQueueCursor<K, P, V> { return this.cursorAt(this.#boundRank(key, true)); }
    /**
     * Seek to the probe and report whether it is present. On a miss the cursor sits where the probe
     * would be inserted and remains usable.
     */
    public findCursor(key: K): PrioritySearchCursorSearch<K, P, V> {
        const cursor = this.cursorAtLowerBound(key); const next = cursor.peekNext();
        return { found: next !== undefined && this.keyComparator(next.value.key, key) === 0, cursor };
    }
    /**
     * A cursor at the minimum-priority entry. This is the bridge between the queue's two indexes:
     * the entry is found by priority, and the cursor walks key order from there.
     */
    public cursorAtMinimumPriority(): PrioritySearchQueueCursor<K, P, V> { return this.#root === undefined ? this.cursorAt() : this.cursorAtLowerBound(this.#root.winner.key); }
    #boundRank(key: K, upper: boolean): number {
        let rank = 0; let node = this.#root;
        while (node !== undefined) { const comparison = this.keyComparator(node.entry.key, key); if (comparison < 0 || upper && comparison === 0) { rank += (node.left?.count ?? 0) + 1; node = node.right; } else node = node.left; }
        return rank;
    }
    public *[Symbol.iterator](): IterableIterator<PrioritySearchEntry<K, P, V>> { const pending: Node<K, P, V>[] = []; let current = this.#root; while (current !== undefined || pending.length !== 0) { while (current !== undefined) { pending.push(current); current = current.left; } const node = pending.pop()!; yield node.entry; current = node.right; } }
}

/** Immutable key-order root-plus-rank cursor over a priority-search queue. */
export class PrioritySearchQueueCursor<K, P, V> {
    public constructor(public readonly queue: PrioritySearchQueue<K, P, V>, public readonly position = 0) {
        if (!Number.isInteger(position) || position < 0 || position > queue.size) throw new RangeError("Cursor position is outside the priority-search queue.");
    }
    /** Number of entries. */
    public get size(): number { return this.queue.size; }
    /** Whether the gap precedes the first entry. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last entry. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The entry immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): { readonly value: PrioritySearchEntry<K, P, V> } | undefined { return this.isAtStart ? undefined : { value: Array.from(this.queue)[this.position - 1]! }; }
    /** The entry immediately after the gap, or `undefined` at the end. */
    public peekNext(): { readonly value: PrioritySearchEntry<K, P, V> } | undefined { return this.isAtEnd ? undefined : { value: Array.from(this.queue)[this.position]! }; }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): PrioritySearchQueueCursor<K, P, V> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new PrioritySearchQueueCursor(this.queue, this.position - 1); }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): PrioritySearchQueueCursor<K, P, V> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new PrioritySearchQueueCursor(this.queue, this.position + 1); }
    /** A cursor at the given rank within the same queue version. */
    public seekRank(position: number): PrioritySearchQueueCursor<K, P, V> { return position === this.position ? this : new PrioritySearchQueueCursor(this.queue, position); }
    /** Insert at the gap and return a cursor positioned after the new entry. */
    public insert(key: K, priority: P, value: V): PrioritySearchQueueCursor<K, P, V> { const location = this.queue.cursorAtLowerBound(key); const result = this.queue.tryAdd(key, priority, value); if (!result.added) throw new TypeError("An equivalent key is already present."); return new PrioritySearchQueueCursor(result.queue, location.position + 1); }
    /**
     * Insert, reporting whether an entry was added. On a duplicate the returned cursor focuses the
     * retained entry.
     */
    public tryInsert(key: K, priority: P, value: V): { readonly added: boolean; readonly cursor: PrioritySearchQueueCursor<K, P, V> } { const location = this.queue.cursorAtLowerBound(key); const result = this.queue.tryAdd(key, priority, value); return { added: result.added, cursor: result.added ? new PrioritySearchQueueCursor(result.queue, location.position + 1) : location }; }
    /** A map with the key bound to the value, adding or replacing as needed. */
    public setItem(key: K, priority: P, value: V): PrioritySearchQueueCursor<K, P, V> { const location = this.queue.findCursor(key); return new PrioritySearchQueueCursor(this.queue.setItem(key, priority, value), location.found ? location.cursor.position : location.cursor.position + 1); }
    /**
     * Replace the priority and payload of the entry after the gap, keeping its key and position.
     */
    public setNext(priority: P, value: V): PrioritySearchQueueCursor<K, P, V> { const entry = this.peekNext(); if (entry === undefined) throw new RangeError("No entry follows the cursor."); return new PrioritySearchQueueCursor(this.queue.setItem(entry.value.key, priority, value), this.position); }
    /** Remove the entry before the gap and return a cursor in its place. */
    public deletePrevious(): PrioritySearchQueueCursor<K, P, V> { const entry = this.peekPrevious(); if (entry === undefined) throw new RangeError("No entry precedes the cursor."); return new PrioritySearchQueueCursor(this.queue.remove(entry.value.key), this.position - 1); }
    /** Remove the entry after the gap and return a cursor in its place. */
    public deleteNext(): PrioritySearchQueueCursor<K, P, V> { const entry = this.peekNext(); if (entry === undefined) throw new RangeError("No entry follows the cursor."); return new PrioritySearchQueueCursor(this.queue.remove(entry.value.key), this.position); }
    /** The queue version this cursor is positioned in. */
    public snapshot(): PrioritySearchQueue<K, P, V> { return this.queue; }
}
