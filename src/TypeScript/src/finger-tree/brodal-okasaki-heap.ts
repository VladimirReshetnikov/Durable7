import { defaultComparator, type Comparator } from "./ordering.js";

class Tree<T> {
    public constructor(public readonly rank: number, public readonly value: T, public readonly children: Forest<T> | undefined) {}
}
class Forest<T> {
    public constructor(public readonly head: Tree<T>, public readonly tail: Forest<T> | undefined) {}
}
interface SplitForest<T> { readonly zeros: Forest<T> | undefined; readonly trees: Forest<T> | undefined; readonly embeddedForest: Forest<T> | undefined }
export interface BrodalOkasakiHeapStatistics { readonly count: number; readonly rootForestLength: number; readonly maximumRank: number; readonly maximumDepth: number }
export interface BrodalMinimumView<T> { readonly minimum: T; readonly remainder: BrodalOkasakiHeap<T> }

/** Immutable bootstrapped skew-binomial min-heap. */
export class BrodalOkasakiHeap<T> implements Iterable<T> {
    readonly #root: Tree<T> | undefined;
    public readonly count: number;
    public readonly comparator: Comparator<T>;
    private constructor(root: Tree<T> | undefined, count: number, comparator: Comparator<T>) { this.#root = root; this.count = count; this.comparator = comparator; }
    public static empty<T>(comparator: Comparator<T> = defaultComparator): BrodalOkasakiHeap<T> { return new BrodalOkasakiHeap(undefined, 0, comparator); }
    public static from<T>(items: Iterable<T>, comparator: Comparator<T> = defaultComparator): BrodalOkasakiHeap<T> { let result = this.empty(comparator); for (const item of items) result = result.insert(item); return result; }
    public get isEmpty(): boolean { return this.#root === undefined; }
    public get minimum(): T { if (this.#root === undefined) throw new RangeError("The heap is empty."); return this.#root.value; }
    #le(left: T, right: T): boolean { return this.comparator(left, right) <= 0; }
    #skewLink(zero: Tree<T>, first: Tree<T>, second: Tree<T>): Tree<T> {
        if (first.rank !== second.rank) throw new Error("Invalid skew-link ranks.");
        if (this.#le(first.value, zero.value) && this.#le(first.value, second.value)) return new Tree(first.rank + 1, first.value, new Forest(zero, new Forest(second, first.children)));
        if (this.#le(second.value, zero.value) && this.#le(second.value, first.value)) return new Tree(second.rank + 1, second.value, new Forest(zero, new Forest(first, second.children)));
        return new Tree(first.rank + 1, zero.value, new Forest(first, new Forest(second, zero.children)));
    }
    #skewInsert(tree: Tree<T>, forest: Forest<T> | undefined): Forest<T> {
        const tail = forest?.tail;
        return forest !== undefined && tail !== undefined && forest.head.rank === tail.head.rank
            ? new Forest(this.#skewLink(tree, forest.head, tail.head), tail.tail)
            : new Forest(tree, forest);
    }
    public insert(item: T): BrodalOkasakiHeap<T> {
        if (this.#root === undefined) return new BrodalOkasakiHeap(new Tree(0, item, undefined), 1, this.comparator);
        const root = this.#le(item, this.#root.value)
            ? new Tree(0, item, new Forest(this.#root, undefined))
            : new Tree(0, this.#root.value, this.#skewInsert(new Tree(0, item, undefined), this.#root.children));
        return new BrodalOkasakiHeap(root, this.count + 1, this.comparator);
    }
    public meld(other: BrodalOkasakiHeap<T>): BrodalOkasakiHeap<T> {
        if (this.comparator !== other.comparator) throw new TypeError("Heap melding requires the same comparator object.");
        if (this.#root === undefined) return other; if (other.#root === undefined) return this;
        const root = this.#le(this.#root.value, other.#root.value)
            ? new Tree(0, this.#root.value, this.#skewInsert(other.#root, this.#root.children))
            : new Tree(0, other.#root.value, this.#skewInsert(this.#root, other.#root.children));
        return new BrodalOkasakiHeap(root, this.count + other.count, this.comparator);
    }
    public minimumView(): BrodalMinimumView<T> | undefined { return this.#root === undefined ? undefined : { minimum: this.#root.value, remainder: this.deleteMinimum() }; }
    #link(first: Tree<T>, second: Tree<T>): Tree<T> {
        if (first.rank !== second.rank) throw new Error("Invalid binomial-link ranks.");
        return this.#le(first.value, second.value)
            ? new Tree(first.rank + 1, first.value, new Forest(second, first.children))
            : new Tree(second.rank + 1, second.value, new Forest(first, second.children));
    }
    #minimumTree(forest: Forest<T>): { readonly tree: Tree<T>; readonly remainder: Forest<T> | undefined } {
        let minimum = forest; let cursor = forest.tail;
        while (cursor !== undefined) { if (this.comparator(cursor.head.value, minimum.head.value) < 0) minimum = cursor; cursor = cursor.tail; }
        const prefix: Tree<T>[] = []; cursor = forest;
        while (cursor !== minimum) { prefix.push(cursor!.head); cursor = cursor!.tail; }
        let remainder = minimum.tail;
        for (let index = prefix.length - 1; index >= 0; index--) remainder = new Forest(prefix[index]!, remainder);
        return { tree: minimum.head, remainder };
    }
    #splitForest(rank: number, initial: Forest<T> | undefined): SplitForest<T> {
        let currentRank = rank; let zeros: Forest<T> | undefined; let trees: Forest<T> | undefined; let forest = initial;
        while (true) {
            if (currentRank === 0) return { zeros, trees, embeddedForest: forest };
            if (forest === undefined) throw new Error("Invalid skew-binomial forest invariant.");
            const tail = forest.tail;
            if (currentRank === 1 && tail === undefined) return { zeros, trees: new Forest(forest.head, trees), embeddedForest: undefined };
            if (tail === undefined) throw new Error("Invalid skew-binomial forest invariant.");
            const first = forest.head; const second = tail.head; const rest = tail.tail;
            if (currentRank === 1) return second.rank === 0
                ? { zeros: new Forest(first, zeros), trees: new Forest(second, trees), embeddedForest: rest }
                : { zeros, trees: new Forest(first, trees), embeddedForest: new Forest(second, rest) };
            if (first.rank === second.rank) return { zeros, trees: new Forest(first, new Forest(second, trees)), embeddedForest: rest };
            if (first.rank === 0) { zeros = new Forest(first, zeros); trees = new Forest(second, trees); forest = rest; }
            else { trees = new Forest(first, trees); forest = new Forest(second, rest); }
            currentRank--;
        }
    }
    #insertRanked(tree: Tree<T>, forest: Forest<T> | undefined): Forest<T> {
        if (forest === undefined) return new Forest(tree, undefined);
        if (tree.rank > forest.head.rank) throw new Error("Invalid ranked insertion order.");
        return tree.rank < forest.head.rank ? new Forest(tree, forest) : this.#insertRanked(this.#link(tree, forest.head), forest.tail);
    }
    #uniquify(forest: Forest<T> | undefined): Forest<T> | undefined { return forest === undefined ? undefined : this.#insertRanked(forest.head, this.#uniquify(forest.tail)); }
    #unionUnique(left: Forest<T> | undefined, right: Forest<T> | undefined): Forest<T> | undefined {
        if (left === undefined) return right; if (right === undefined) return left;
        if (left.head.rank < right.head.rank) return new Forest(left.head, this.#unionUnique(left.tail, right));
        if (left.head.rank > right.head.rank) return new Forest(right.head, this.#unionUnique(left, right.tail));
        return this.#insertRanked(this.#link(left.head, right.head), this.#unionUnique(left.tail, right.tail));
    }
    #skewMeld(left: Forest<T> | undefined, right: Forest<T> | undefined): Forest<T> | undefined { return this.#unionUnique(this.#uniquify(left), this.#uniquify(right)); }
    public deleteMinimum(): BrodalOkasakiHeap<T> {
        if (this.#root === undefined) throw new RangeError("The heap is empty.");
        if (this.#root.children === undefined) return BrodalOkasakiHeap.empty(this.comparator);
        const minimum = this.#minimumTree(this.#root.children);
        const split = this.#splitForest(minimum.tree.rank, minimum.tree.children);
        let merged = this.#skewMeld(this.#skewMeld(split.trees, minimum.remainder), split.embeddedForest);
        const zeros: Tree<T>[] = []; let cursor = split.zeros; while (cursor !== undefined) { zeros.push(cursor.head); cursor = cursor.tail; }
        for (let index = zeros.length - 1; index >= 0; index--) merged = this.#skewInsert(zeros[index]!, merged);
        return new BrodalOkasakiHeap(new Tree(0, minimum.tree.value, merged), this.count - 1, this.comparator);
    }
    #validateFused(tree: Tree<T>): Forest<T> | undefined {
        let rank = tree.rank; let forest = tree.children;
        while (rank > 0) {
            if (forest === undefined) throw new Error("Ranked heap tree is missing children.");
            const first = forest.head;
            if (rank === 1) { if (first.rank !== 0) throw new Error("Invalid rank-one child."); const tail = forest.tail; if (tail === undefined) return undefined; return tail.head.rank === 0 ? tail.tail : tail; }
            const tail = forest.tail; if (tail === undefined) throw new Error("Incomplete fused children."); const second = tail.head;
            if (first.rank === second.rank) { if (first.rank !== rank - 1) throw new Error("Invalid skew child ranks."); return tail.tail; }
            if (first.rank === 0) { if (second.rank !== rank - 1) throw new Error("Invalid ranked child."); forest = tail.tail; }
            else { if (first.rank !== rank - 1) throw new Error("Invalid linked child."); forest = tail; }
            rank--;
        }
        return forest;
    }
    #validateForest(initial: Forest<T> | undefined): number {
        let length = 0; let previous = -1; let duplicate = false; let forest = initial;
        while (forest !== undefined) { const rank = forest.head.rank; if (rank < previous || rank < 0) throw new Error("Invalid heap forest rank order."); if (rank === previous) { if (length !== 1 || duplicate) throw new Error("Only first two skew ranks may match."); duplicate = true; } previous = rank; length++; forest = forest.tail; }
        return length;
    }
    public validateStructure(): BrodalOkasakiHeapStatistics {
        if (this.#root === undefined) { if (this.count !== 0) throw new Error("Empty heap count mismatch."); return { count: 0, rootForestLength: 0, maximumRank: 0, maximumDepth: 0 }; }
        if (this.#root.rank !== 0) throw new Error("Global heap root must have rank zero.");
        const rootForestLength = this.#validateForest(this.#root.children); const pending: Array<readonly [Tree<T>, number]> = [[this.#root, 1]];
        let count = 0; let maximumRank = 0; let maximumDepth = 0;
        while (pending.length !== 0) { const [tree, depth] = pending.pop()!; count++; maximumRank = Math.max(maximumRank, tree.rank); maximumDepth = Math.max(maximumDepth, depth); this.#validateForest(this.#validateFused(tree)); let forest = tree.children; while (forest !== undefined) { if (!this.#le(tree.value, forest.head.value)) throw new Error("Heap order invariant failed."); pending.push([forest.head, depth + 1]); forest = forest.tail; } }
        if (count !== this.count) throw new Error("Heap logical count mismatch.");
        return { count, rootForestLength, maximumRank, maximumDepth };
    }
    public sharedTreeCount(other: BrodalOkasakiHeap<T>): number {
        const nodes = new Set<Tree<T>>(); const collect = (root: Tree<T> | undefined, destination: Set<Tree<T>>): void => { const pending = root === undefined ? [] : [root]; while (pending.length !== 0) { const tree = pending.pop()!; if (destination.has(tree)) continue; destination.add(tree); let forest = tree.children; while (forest !== undefined) { pending.push(forest.head); forest = forest.tail; } } };
        collect(this.#root, nodes); const otherNodes = new Set<Tree<T>>(); collect(other.#root, otherNodes); let count = 0; for (const node of otherNodes) if (nodes.has(node)) count++; return count;
    }
    public *[Symbol.iterator](): IterableIterator<T> { const pending = this.#root === undefined ? [] : [this.#root]; while (pending.length !== 0) { const tree = pending.pop()!; let forest = tree.children; while (forest !== undefined) { pending.push(forest.head); forest = forest.tail; } yield tree.value; } }
}
