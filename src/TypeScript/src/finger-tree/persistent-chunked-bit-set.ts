import type { MeasurePolicy } from "./measures.js";
import { MeasuredSequence } from "./measured-sequence.js";

interface BitSetChunk {
    readonly wordIndex: number;
    readonly bits: bigint;
}

interface BitSetSummary {
    readonly chunkCount: number;
    readonly popCount: number;
    readonly lastWordIndex: number | undefined;
}

const emptySummary: BitSetSummary = { chunkCount: 0, popCount: 0, lastWordIndex: undefined };

const bitSetMeasure: MeasurePolicy<BitSetChunk, BitSetSummary> = {
    identity: emptySummary,
    combine: (left, right): BitSetSummary => ({
        chunkCount: left.chunkCount + right.chunkCount,
        popCount: left.popCount + right.popCount,
        lastWordIndex: right.lastWordIndex ?? left.lastWordIndex,
    }),
    measure: (chunk): BitSetSummary => ({
        chunkCount: 1,
        popCount: popCount(chunk.bits),
        lastWordIndex: chunk.wordIndex,
    }),
};

const maximumBitIndex = 0x7fff_ffff;

type BitSetOperation = "union" | "intersect" | "except" | "symmetricExcept";

/** Result of a nonthrowing chunked-bit-set point update. */
export interface ChunkedBitSetUpdateResult {
    readonly changed: boolean;
    readonly set: PersistentChunkedBitSet;
}

/** Structural statistics for the sparse measured representation. */
export interface ChunkedBitSetStatistics {
    readonly chunkCount: number;
    readonly popCount: number;
}

export interface ChunkedBitSetCursorSearch {
    readonly found: boolean;
    readonly cursor: PersistentChunkedBitSetCursor;
}

/**
 * Immutable sparse bit set over the nonnegative signed-32-bit index domain.
 *
 * Only nonzero 64-bit words are represented. Cached ordered-word and population summaries support
 * logarithmic membership, point edits, inclusive rank, and zero-based select in represented words.
 */
export class PersistentChunkedBitSet implements Iterable<number> {
    static readonly #empty = new PersistentChunkedBitSet(MeasuredSequence.empty(bitSetMeasure));
    readonly #chunks: MeasuredSequence<BitSetChunk, BitSetSummary>;

    private constructor(chunks: MeasuredSequence<BitSetChunk, BitSetSummary>) { this.#chunks = chunks; }

    public static empty(): PersistentChunkedBitSet { return PersistentChunkedBitSet.#empty; }

    public static from(bitIndexes: Iterable<number>): PersistentChunkedBitSet {
        if (bitIndexes === null || bitIndexes === undefined) throw new TypeError("bitIndexes must be iterable.");
        const words = new Map<number, bigint>();
        for (const bitIndex of bitIndexes) {
            requireBitIndex(bitIndex);
            const wordIndex = Math.floor(bitIndex / 64);
            const bit = 1n << BigInt(bitIndex & 63);
            words.set(wordIndex, (words.get(wordIndex) ?? 0n) | bit);
        }
        if (words.size === 0) return PersistentChunkedBitSet.empty();
        const chunks = Array.from(words, ([wordIndex, bits]): BitSetChunk => ({ wordIndex, bits }))
            .sort((left, right) => left.wordIndex - right.wordIndex);
        return new PersistentChunkedBitSet(MeasuredSequence.from(chunks, bitSetMeasure));
    }

    public get count(): number { return this.#chunks.measure.popCount; }
    public get size(): number { return this.count; }
    public get chunkCount(): number { return this.#chunks.measure.chunkCount; }
    public get isEmpty(): boolean { return this.#chunks.isEmpty; }

    public contains(bitIndex: number): boolean {
        if (!Number.isInteger(bitIndex) || bitIndex < 0 || bitIndex > maximumBitIndex) return false;
        const wordIndex = Math.floor(bitIndex / 64);
        const located = this.locateWord(wordIndex);
        return located.found && located.value?.wordIndex === wordIndex
            && (located.value.bits & (1n << BigInt(bitIndex & 63))) !== 0n;
    }

    public add(bitIndex: number): PersistentChunkedBitSet {
        requireBitIndex(bitIndex);
        const wordIndex = Math.floor(bitIndex / 64);
        const bit = 1n << BigInt(bitIndex & 63);
        const located = this.locateWord(wordIndex);
        if (located.found && located.value?.wordIndex === wordIndex) {
            const bits = located.value.bits | bit;
            return bits === located.value.bits
                ? this
                : new PersistentChunkedBitSet(this.#chunks.setAt(located.index, { wordIndex, bits })!);
        }
        return new PersistentChunkedBitSet(this.#chunks.insertAt(located.index, { wordIndex, bits: bit })!);
    }

    public tryAdd(bitIndex: number): ChunkedBitSetUpdateResult {
        const set = this.add(bitIndex);
        return { changed: set !== this, set };
    }

    public remove(bitIndex: number): PersistentChunkedBitSet {
        if (!Number.isInteger(bitIndex) || bitIndex < 0 || bitIndex > maximumBitIndex) return this;
        const wordIndex = Math.floor(bitIndex / 64);
        const bit = 1n << BigInt(bitIndex & 63);
        const located = this.locateWord(wordIndex);
        if (!located.found || located.value?.wordIndex !== wordIndex || (located.value.bits & bit) === 0n) return this;
        const bits = located.value.bits & ~bit;
        return new PersistentChunkedBitSet(
            bits === 0n
                ? this.#chunks.removeAt(located.index)!
                : this.#chunks.setAt(located.index, { wordIndex, bits })!,
        );
    }

    public tryRemove(bitIndex: number): ChunkedBitSetUpdateResult {
        const set = this.remove(bitIndex);
        return { changed: set !== this, set };
    }

    /** Counts set indexes less than or equal to `bitIndex`. */
    public rank(bitIndex: number): number {
        if (!Number.isFinite(bitIndex) || bitIndex < 0) return 0;
        if (bitIndex >= maximumBitIndex) return this.count;
        const integral = Math.floor(bitIndex);
        const wordIndex = Math.floor(integral / 64);
        const located = this.locateWord(wordIndex);
        if (!located.found || located.value?.wordIndex !== wordIndex) return located.measureBefore.popCount;
        const offset = integral & 63;
        const mask = offset === 63 ? (1n << 64n) - 1n : (1n << BigInt(offset + 1)) - 1n;
        return located.measureBefore.popCount + popCount(located.value.bits & mask);
    }

    /** Returns the bit at a zero-based population rank. */
    public select(rank: number): number {
        const selected = this.trySelect(rank);
        if (selected === undefined) throw new RangeError("rank must be an integer from zero through count minus one.");
        return selected;
    }

    public trySelect(rank: number): number | undefined {
        if (!Number.isInteger(rank) || rank < 0 || rank >= this.count) return undefined;
        const located = this.#chunks.locate((summary) => summary.popCount > rank);
        if (!located.found || located.value === undefined) return undefined;
        let bits = located.value.bits;
        const localRank = rank - located.measureBefore.popCount;
        for (let index = 0; index < localRank; index++) bits &= bits - 1n;
        let offset = 0;
        while ((bits & 1n) === 0n) { bits >>= 1n; offset++; }
        return located.value.wordIndex * 64 + offset;
    }

    public union(other: PersistentChunkedBitSet): PersistentChunkedBitSet {
        return other === this || other.isEmpty ? this : this.combine(other, "union");
    }

    public intersect(other: PersistentChunkedBitSet): PersistentChunkedBitSet {
        return other === this ? this : this.combine(other, "intersect");
    }

    public except(other: PersistentChunkedBitSet): PersistentChunkedBitSet {
        return other === this ? PersistentChunkedBitSet.empty() : this.combine(other, "except");
    }

    public symmetricExcept(other: PersistentChunkedBitSet): PersistentChunkedBitSet {
        return other === this ? PersistentChunkedBitSet.empty() : this.combine(other, "symmetricExcept");
    }

    public clear(): PersistentChunkedBitSet { return this.isEmpty ? this : PersistentChunkedBitSet.empty(); }

    public cursorAt(position = 0): PersistentChunkedBitSetCursor {
        return new PersistentChunkedBitSetCursor(this, position);
    }

    public cursorAtOrAfter(bitIndex: number): PersistentChunkedBitSetCursor {
        const position = bitIndex <= 0 ? 0 : this.rank(bitIndex - 1);
        return this.cursorAt(position);
    }

    public findCursor(bitIndex: number): ChunkedBitSetCursorSearch {
        const cursor = this.cursorAtOrAfter(bitIndex);
        return { found: bitIndex >= 0 && cursor.peekNext()?.value === bitIndex, cursor };
    }

    public *[Symbol.iterator](): IterableIterator<number> {
        for (const chunk of this.#chunks) {
            let bits = chunk.bits;
            let offset = 0;
            while (bits !== 0n) {
                if ((bits & 1n) !== 0n) yield chunk.wordIndex * 64 + offset;
                bits >>= 1n;
                offset++;
            }
        }
    }

    public sharesStorageWith(other: PersistentChunkedBitSet): boolean {
        return this.#chunks.sharesStructureWith(other.#chunks);
    }

    public validateStructure(): ChunkedBitSetStatistics {
        let chunkCount = 0;
        let total = 0;
        let previous = -1;
        for (const chunk of this.#chunks) {
            if (chunk.wordIndex <= previous || chunk.bits === 0n || chunk.bits >> 64n !== 0n) {
                throw new Error("The chunked bit set has invalid word ordering or contents.");
            }
            previous = chunk.wordIndex;
            chunkCount++;
            total += popCount(chunk.bits);
        }
        const summary = this.#chunks.measure;
        if (summary.chunkCount !== chunkCount || summary.popCount !== total
            || summary.lastWordIndex !== (chunkCount === 0 ? undefined : previous)) {
            throw new Error("The chunked bit-set annotations disagree with its words.");
        }
        return { chunkCount, popCount: total };
    }

    private locateWord(wordIndex: number): ReturnType<MeasuredSequence<BitSetChunk, BitSetSummary>["locate"]> {
        return this.#chunks.locate((summary) => summary.lastWordIndex !== undefined && summary.lastWordIndex >= wordIndex);
    }

    private combine(other: PersistentChunkedBitSet, operation: BitSetOperation): PersistentChunkedBitSet {
        const left = this.#chunks.toArray();
        const right = other.#chunks.toArray();
        const result: BitSetChunk[] = [];
        let leftIndex = 0;
        let rightIndex = 0;
        while (leftIndex < left.length || rightIndex < right.length) {
            const leftChunk = left[leftIndex];
            const rightChunk = right[rightIndex];
            if (rightChunk === undefined || (leftChunk !== undefined && leftChunk.wordIndex < rightChunk.wordIndex)) {
                if (operation === "union" || operation === "except" || operation === "symmetricExcept") result.push(leftChunk!);
                leftIndex++;
                continue;
            }
            if (leftChunk === undefined || rightChunk.wordIndex < leftChunk.wordIndex) {
                if (operation === "union" || operation === "symmetricExcept") result.push(rightChunk);
                rightIndex++;
                continue;
            }
            const bits = operation === "union" ? leftChunk.bits | rightChunk.bits
                : operation === "intersect" ? leftChunk.bits & rightChunk.bits
                    : operation === "except" ? leftChunk.bits & ~rightChunk.bits
                        : leftChunk.bits ^ rightChunk.bits;
            if (bits !== 0n) result.push({ wordIndex: leftChunk.wordIndex, bits });
            leftIndex++;
            rightIndex++;
        }
        if (sameChunks(result, left)) return this;
        if (result.length === 0) return PersistentChunkedBitSet.empty();
        return new PersistentChunkedBitSet(MeasuredSequence.from(result, bitSetMeasure));
    }
}

/** Immutable root-plus-population-rank cursor over the present bits of a sparse bit set. */
export class PersistentChunkedBitSetCursor {
    public constructor(public readonly set: PersistentChunkedBitSet, public readonly position = 0) {
        if (!Number.isInteger(position) || position < 0 || position > set.count) throw new RangeError("Cursor position is outside the chunked bit set.");
    }
    public get count(): number { return this.set.count; }
    public get size(): number { return this.count; }
    public get isAtStart(): boolean { return this.position === 0; }
    public get isAtEnd(): boolean { return this.position === this.count; }
    public peekPrevious(): { readonly value: number } | undefined { return this.isAtStart ? undefined : { value: this.set.select(this.position - 1) }; }
    public peekNext(): { readonly value: number } | undefined { return this.isAtEnd ? undefined : { value: this.set.select(this.position) }; }
    public movePrevious(): PersistentChunkedBitSetCursor { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new PersistentChunkedBitSetCursor(this.set, this.position - 1); }
    public moveNext(): PersistentChunkedBitSetCursor { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new PersistentChunkedBitSetCursor(this.set, this.position + 1); }
    public seekRank(position: number): PersistentChunkedBitSetCursor { return position === this.position ? this : new PersistentChunkedBitSetCursor(this.set, position); }
    public add(bitIndex: number): PersistentChunkedBitSetCursor { const set = this.set.add(bitIndex); if (set === this.set) return this; const position = bitIndex === 0 ? 0 : this.set.rank(bitIndex - 1); return new PersistentChunkedBitSetCursor(set, position + 1); }
    public deletePrevious(): PersistentChunkedBitSetCursor { const bit = this.peekPrevious(); if (bit === undefined) throw new RangeError("No set bit precedes the cursor."); return new PersistentChunkedBitSetCursor(this.set.remove(bit.value), this.position - 1); }
    public deleteNext(): PersistentChunkedBitSetCursor { const bit = this.peekNext(); if (bit === undefined) throw new RangeError("No set bit follows the cursor."); return new PersistentChunkedBitSetCursor(this.set.remove(bit.value), this.position); }
    public snapshot(): PersistentChunkedBitSet { return this.set; }
}

function requireBitIndex(bitIndex: number): void {
    if (!Number.isInteger(bitIndex) || bitIndex < 0 || bitIndex > maximumBitIndex) {
        throw new RangeError("bitIndex must be a nonnegative signed-32-bit integer.");
    }
}

function popCount(value: bigint): number {
    let remaining = value;
    let count = 0;
    while (remaining !== 0n) { remaining &= remaining - 1n; count++; }
    return count;
}

function sameChunks(left: readonly BitSetChunk[], right: readonly BitSetChunk[]): boolean {
    return left.length === right.length
        && left.every((chunk, index) => chunk.wordIndex === right[index]?.wordIndex && chunk.bits === right[index]?.bits);
}
