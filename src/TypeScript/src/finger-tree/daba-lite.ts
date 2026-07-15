import type { Monoid } from "./measures.js";

const chunkCapacity = 64;

class Block<T> {
    public readonly items: (T | undefined)[] = new Array<T | undefined>(chunkCapacity);
    public previous: Block<T> | undefined;
    public next: Block<T> | undefined;
}

interface Cursor<T> { readonly block: Block<T>; readonly index: number }
function sameCursor<T>(left: Cursor<T>, right: Cursor<T>): boolean { return left.block === right.block && left.index === right.index; }

class ChunkedQueue<T> {
    #first = new Block<T>();
    public get begin(): Cursor<T> { return { block: this.#first, index: 0 }; }
    public get firstBlock(): Block<T> { return this.#first; }
    public read(cursor: Cursor<T>): T { return cursor.block.items[cursor.index] as T; }
    public readRaw(cursor: Cursor<T>): T | undefined { return cursor.block.items[cursor.index]; }
    public write(cursor: Cursor<T>, value: T | undefined): void { cursor.block.items[cursor.index] = value; }
    public reset(): Cursor<T> { this.#first = new Block<T>(); return this.begin; }
    public advance(cursor: Cursor<T>): Cursor<T> {
        if (cursor.index + 1 < chunkCapacity) return { block: cursor.block, index: cursor.index + 1 };
        if (cursor.block.next === undefined) throw new Error("Cannot advance beyond the DABA Lite end chunk.");
        return { block: cursor.block.next, index: 0 };
    }
    public advanceEnd(cursor: Cursor<T>): Cursor<T> {
        if (cursor.index + 1 < chunkCapacity) return { block: cursor.block, index: cursor.index + 1 };
        let next = cursor.block.next;
        if (next === undefined) { next = new Block<T>(); next.previous = cursor.block; cursor.block.next = next; }
        return { block: next, index: 0 };
    }
    public retreat(cursor: Cursor<T>): Cursor<T> {
        if (cursor.index !== 0) return { block: cursor.block, index: cursor.index - 1 };
        if (cursor.block.previous === undefined) throw new Error("Cannot retreat before the DABA Lite front chunk.");
        return { block: cursor.block.previous, index: chunkCapacity - 1 };
    }
    public restoreSuccessor(block: Block<T>, successor: Block<T> | undefined): void {
        const current = block.next;
        if (current === successor) return;
        block.next = successor;
        if (successor !== undefined) successor.previous = block;
        if (current !== undefined) current.previous = undefined;
    }
    public trimBefore(front: Cursor<T>): void {
        if (this.#first === front.block) return;
        const predecessor = front.block.previous;
        this.#first = front.block;
        this.#first.previous = undefined;
        if (predecessor !== undefined) predecessor.next = undefined;
    }
}

export interface DabaLiteStatistics {
    readonly count: number;
    readonly frontLength: number;
    readonly backLength: number;
    readonly leftLength: number;
    readonly rightLength: number;
    readonly accumulatorLength: number;
    readonly blockCount: number;
    readonly allocatedSlotCapacity: number;
    readonly slackSlotCount: number;
}

/** Six-cursor DABA Lite mutable FIFO monoid aggregator with bounded callbacks. */
export class DabaLite<T> {
    readonly #monoid: Monoid<T>;
    readonly #queue = new ChunkedQueue<T>();
    #f: Cursor<T>;
    #l: Cursor<T>;
    #r: Cursor<T>;
    #a: Cursor<T>;
    #b: Cursor<T>;
    #e: Cursor<T>;
    #aggregateRA: T;
    #aggregateB: T;
    #count = 0;

    public constructor(monoid: Monoid<T>) {
        this.#monoid = monoid;
        this.#f = this.#queue.begin;
        this.#l = this.#f; this.#r = this.#f; this.#a = this.#f; this.#b = this.#f; this.#e = this.#f;
        this.#aggregateRA = monoid.identity;
        this.#aggregateB = monoid.identity;
    }

    public get count(): number { return this.#count; }
    public get isEmpty(): boolean { return this.#count === 0; }
    public get aggregate(): T { return this.#count === 0 ? this.#monoid.identity : this.#monoid.combine(this.#queue.read(this.#f), this.#aggregateB); }

    public insert(value: T): void {
        if (this.#count === Number.MAX_SAFE_INTEGER) throw new RangeError("The DABA Lite window is too large.");
        const oldEnd = this.#e;
        const oldCount = this.#count;
        const oldAggregateB = this.#aggregateB;
        const oldValue = this.#queue.readRaw(oldEnd);
        const oldSuccessor = oldEnd.block.next;
        const nextAggregateB = this.#monoid.combine(oldAggregateB, value);
        try {
            const newEnd = this.#queue.advanceEnd(oldEnd);
            this.#queue.write(oldEnd, value);
            this.#e = newEnd;
            this.#count = oldCount + 1;
            this.#aggregateB = nextAggregateB;
            this.#fixup();
        } catch (error) {
            this.#queue.write(oldEnd, oldValue);
            this.#queue.restoreSuccessor(oldEnd.block, oldSuccessor);
            this.#e = oldEnd;
            this.#count = oldCount;
            this.#aggregateB = oldAggregateB;
            throw error;
        }
    }

    public evict(): void { if (!this.tryEvict()) throw new Error("Cannot evict from an empty DABA Lite window."); }

    public tryEvict(): boolean {
        if (this.#count === 0) return false;
        const oldFront = this.#f;
        const oldCount = this.#count;
        this.#f = this.#queue.advance(oldFront);
        this.#count--;
        try { this.#fixup(); }
        catch (error) { this.#f = oldFront; this.#count = oldCount; throw error; }
        this.#queue.write(oldFront, undefined);
        this.#queue.trimBefore(this.#f);
        return true;
    }

    public clear(): void {
        if (this.#count === 0) return;
        const identity = this.#monoid.identity;
        const begin = this.#queue.reset();
        this.#f = begin; this.#l = begin; this.#r = begin; this.#a = begin; this.#b = begin; this.#e = begin;
        this.#aggregateRA = identity; this.#aggregateB = identity; this.#count = 0;
    }

    public validateStructure(): DabaLiteStatistics {
        for (const [name, cursor] of [["F", this.#f], ["L", this.#l], ["R", this.#r], ["A", this.#a], ["B", this.#b], ["E", this.#e]] as const) {
            if (cursor.index < 0 || cursor.index >= chunkCapacity) throw new Error(`The DABA Lite ${name} cursor has an invalid index.`);
        }
        const first = this.#queue.firstBlock;
        if (first.previous !== undefined || first !== this.#f.block) throw new Error("Invalid DABA Lite first chunk.");
        const cursors = [this.#f, this.#l, this.#r, this.#a, this.#b, this.#e];
        const positions = new Array<number>(6).fill(-1);
        const seen = new Set<Block<T>>();
        let previous: Block<T> | undefined;
        let block: Block<T> = first;
        let blockBase = 0;
        let blockCount = 0;
        while (true) {
            if (seen.has(block)) throw new Error("The DABA Lite chunk chain contains a cycle.");
            seen.add(block);
            if (block.previous !== previous || (previous !== undefined && previous.next !== block)) throw new Error("Invalid DABA Lite chunk links.");
            cursors.forEach((cursor, index) => { if (cursor.block === block) positions[index] = blockBase + cursor.index; });
            blockCount++;
            if (block === this.#e.block) { if (block.next !== undefined) throw new Error("End cursor is not in the last chunk."); break; }
            previous = block;
            if (block.next === undefined) throw new Error("End cursor is unreachable.");
            block = block.next; blockBase += chunkCapacity;
        }
        if (positions.some((value) => value < 0)) throw new Error("A cursor lies outside the active chain.");
        const [f, l, r, a, b, e] = positions as [number, number, number, number, number, number];
        if (!(f <= l && l <= r && r <= a && a <= b && b <= e)) throw new Error("DABA Lite cursor ordering is invalid.");
        if (e - f !== this.#count) throw new Error("DABA Lite count disagrees with cursor distance.");
        const frontLength = b - f;
        const backLength = e - b;
        const leftLength = r - l;
        const rightLength = a - r;
        const accumulatorLength = b - a;
        if (this.#count === 0) {
            if (!(f === l && l === r && r === a && a === b && b === e)) throw new Error("Empty DABA Lite cursors differ.");
        } else {
            if (leftLength !== rightLength) throw new Error("DABA Lite work-region sizes differ.");
            if (leftLength + rightLength + accumulatorLength + 1 !== frontLength - backLength) throw new Error("DABA Lite size equation is invalid.");
            if (l - f !== backLength + 1) throw new Error("DABA Lite processed-prefix equation is invalid.");
        }
        const expectedBlocks = Math.floor((this.#f.index + this.#count) / chunkCapacity) + 1;
        if (blockCount !== expectedBlocks) throw new Error("DABA Lite block count is invalid.");
        const allocatedSlotCapacity = blockCount * chunkCapacity;
        const slackSlotCount = allocatedSlotCapacity - this.#count;
        if (slackSlotCount < 1 || slackSlotCount >= 2 * chunkCapacity) throw new Error("DABA Lite slack exceeds its bound.");
        return { count: this.#count, frontLength, backLength, leftLength, rightLength, accumulatorLength, blockCount, allocatedSlotCapacity, slackSlotCount };
    }

    #fixup(): void {
        let nextL = this.#l;
        const nextR = this.#r;
        let nextA = this.#a;
        let nextB = this.#b;
        let nextAggregateRA = this.#aggregateRA;
        let nextAggregateB = this.#aggregateB;
        let cachedIdentity: T | undefined;
        let hasIdentity = false;
        if (sameCursor(this.#f, nextB)) {
            const identity = this.#monoid.identity;
            this.#publish(this.#e, this.#e, this.#e, this.#e, identity, identity);
            return;
        }
        if (sameCursor(nextL, nextB)) {
            cachedIdentity = this.#monoid.identity;
            hasIdentity = true;
            nextL = this.#f; nextA = this.#e; nextB = this.#e;
            nextAggregateRA = nextAggregateB;
            nextAggregateB = cachedIdentity;
        }
        if (sameCursor(nextL, nextR)) {
            const identity = hasIdentity ? cachedIdentity as T : this.#monoid.identity;
            this.#publish(this.#queue.advance(nextL), this.#queue.advance(nextR), this.#queue.advance(nextA), nextB, identity, nextAggregateB);
            return;
        }
        const advancedLeft = this.#queue.advance(nextL);
        const beforeA = this.#queue.retreat(nextA);
        const newLeftValue = this.#monoid.combine(this.#queue.read(nextL), nextAggregateRA);
        const productA = sameCursor(nextA, nextB) ? this.#monoid.identity : this.#queue.read(nextA);
        const newAccumulatorValue = this.#monoid.combine(this.#queue.read(beforeA), productA);
        this.#queue.write(nextL, newLeftValue);
        this.#queue.write(beforeA, newAccumulatorValue);
        this.#publish(advancedLeft, nextR, beforeA, nextB, nextAggregateRA, nextAggregateB);
    }

    #publish(l: Cursor<T>, r: Cursor<T>, a: Cursor<T>, b: Cursor<T>, aggregateRA: T, aggregateB: T): void {
        this.#l = l; this.#r = r; this.#a = a; this.#b = b; this.#aggregateRA = aggregateRA; this.#aggregateB = aggregateB;
    }
}
