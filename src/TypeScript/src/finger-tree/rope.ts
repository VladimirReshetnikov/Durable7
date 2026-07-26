/**
 * Persistent chunked sequences, in positional, measured, and text flavors.
 *
 * Elements live in contiguous chunks at the leaves rather than one per leaf, so bulk work runs over
 * slices while the tree above still gives logarithmic indexing, splitting, and concatenation. The
 * text layer caches newline counts, making offset-to-line conversion logarithmic instead of a scan.
 */
import { FingerTree, PersistentDeque } from "./core.js";
import type { MeasurePolicy } from "./measures.js";

/**
 * Materializes a rope range argument exactly once, rejecting a bare `string`.
 *
 * `Rope.fromText` and `TextRope.fromText` split on `""`, so a text rope element is one UTF-16 code
 * unit. Spreading a `string` instead yields code points, which would store an astral character as a
 * single two-unit element and desynchronize every position, size, measure, and line/column derived
 * from the rope. The generic signature cannot split on the caller's behalf because `T` is arbitrary
 * and a rope of whole strings is a legitimate instantiation, so the ambiguous argument is refused
 * rather than guessed: callers pass `text.split("")`, or use `TextRopeCursor.insert`, which splits.
 */
function materializeRopeRange<T>(values: Iterable<T>): T[] {
    if (typeof values === "string") {
        throw new TypeError("Rope ranges are UTF-16 code units; pass text.split(\"\") or use TextRopeCursor.insert.");
    }
    return [...values];
}

/** Persistent positional rope. */
export class Rope<T> implements Iterable<T> {
    readonly #items: PersistentDeque<T>;
    private constructor(items: PersistentDeque<T>) { this.#items = items; }
    /** The empty rope, retaining the supplied policy objects. */
    public static empty<T>(): Rope<T> { return new Rope(PersistentDeque.empty<T>()); }
    /** Build a rope from the given elements. */
    public static from<T>(values: Iterable<T>): Rope<T> { return new Rope(PersistentDeque.from(values)); }
    /** Build a character rope from the given text, one element per code point. */
    public static fromText(text: string): Rope<string> { return Rope.from(text.split("")); }
    /** A cursor at the given gap of the rope. */
    public getCursor(position = 0): RopeCursor<T> { return new RopeCursor(this, position); }
    /** Number of elements. */
    public get size(): number { return this.#items.size; }
    /** Whether the rope holds no elements. */
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    /** The first element, or `undefined` when empty. */
    public front(): T | undefined { return this.#items.front(); }
    /** The last element, or `undefined` when empty. */
    public back(): T | undefined { return this.#items.back(); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(index: number): T | undefined { return this.#items.get(index); }
    /** Copy a run of elements into the destination, reporting whether the range was valid. */
    public copyTo(index: number, destination: T[], destinationIndex: number = destination.length, count: number = this.size - index): boolean {
        if (!Number.isInteger(index) || !Number.isInteger(destinationIndex) || !Number.isInteger(count) || index < 0 || count < 0 || index + count > this.size || destinationIndex < 0) return false;
        destination.splice(destinationIndex, 0, ...this.slice(index, count)!);
        return true;
    }
    /** A rope with the value added at the front. */
    public pushFront(value: T): Rope<T> { return new Rope(this.#items.prepend(value)); }
    /** A rope with the value added at the back. */
    public pushBack(value: T): Rope<T> { return new Rope(this.#items.append(value)); }
    /** A map with the key bound to the value, adding or replacing as needed. */
    public setItem(index: number, value: T): Rope<T> | undefined { const next = this.#items.setItem(index, value); return next === undefined ? undefined : next === this.#items ? this : new Rope(next); }
    /** A rope with the value inserted so that it ends up at the given index. */
    public insertAt(index: number, value: T): Rope<T> | undefined { const next = this.#items.insertAt(index, value); return next === undefined ? undefined : new Rope(next); }
    /** A rope with every value inserted at the given index, in order. */
    public insertRange(index: number, values: Iterable<T>): Rope<T> | undefined {
        const materialized = materializeRopeRange(values);
        const split = this.#items.splitAt(index); if (split === undefined) return undefined;
        return new Rope(split.left.concat(PersistentDeque.from(materialized)).concat(split.right));
    }
    /** A rope without the element at the given index. */
    public removeAt(index: number): Rope<T> | undefined { const next = this.#items.removeAt(index); return next === undefined ? undefined : new Rope(next); }
    /** A rope without the given run of elements. */
    public removeRange(index: number, count: number): Rope<T> | undefined {
        const split = this.#items.splitRange(index, count); return split === undefined ? undefined : new Rope(split.before.concat(split.after));
    }
    /** The given run of elements as a new rope, sharing structure with the receiver. */
    public slice(index: number, count: number): Rope<T> | undefined { const split = this.#items.splitRange(index, count); return split === undefined ? undefined : new Rope(split.range); }
    /**
     * The elements before the index and those from it on; both halves share structure with the
     * receiver.
     */
    public splitAt(index: number): readonly [Rope<T>, Rope<T>] | undefined { const split = this.#items.splitAt(index); return split === undefined ? undefined : [new Rope(split.left), new Rope(split.right)]; }
    /**
     * This rope's elements followed by the other's, joining the two trees rather than copying
     * either.
     */
    public concat(other: Rope<T>): Rope<T> { if (this.isEmpty) return other; if (other.isEmpty) return this; return new Rope(this.#items.concat(other.#items)); }
    /**
     * An equal rope whose chunks have been merged; trades one linear rebuild for cheaper later
     * traversal.
     */
    public compact(): Rope<T> { return Rope.from(this); }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return this.#items.toArray(); }
    /**
     * Whether both ropes share a node by identity. A representation test used to confirm a no-op
     * avoided copying, not an equality test.
     */
    public sharesStorageWith(other: Rope<T>): boolean { return this.#items.sharesStorageWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

/** The two ropes produced by a split, each with its own recomputed measure. */
export interface MeasuredRopeSplit<T, M> { readonly left: MeasuredRope<T, M>; readonly right: MeasuredRope<T, M> }
/** Where a measure-directed search landed, reported without splitting the rope. */
export interface MeasuredRopeLocate<T, M> { readonly index: number; readonly measureBefore: M; readonly value: T | undefined; readonly found: boolean }
/**
 * An element found next to a cursor gap, wrapped so a stored `undefined` stays distinct from
 * "nothing there".
 */
export interface RopeCursorPeek<T> { readonly value: T }
/** The outcome of a measure-directed cursor seek; on a miss the cursor sits at the end. */
export interface MeasuredRopeCursorSearch<T, M> { readonly cursor: MeasuredRopeCursor<T, M>; readonly found: boolean }
/** The outcome of seeking a text cursor to a line and column; on a miss it remains usable. */
export interface TextRopeCursorSearch { readonly cursor: TextRopeCursor; readonly found: boolean }

/** Positional rope carrying a caller-defined cached monoidal measure. */
export class MeasuredRope<T, M> implements Iterable<T> {
    readonly #items: FingerTree<T, M>;
    /** The retained policy defining equivalence. */
    public readonly policy: MeasurePolicy<T, M>;
    private constructor(items: FingerTree<T, M>, policy: MeasurePolicy<T, M>) { this.#items = items; this.policy = policy; }
    /** The empty rope, retaining the supplied policy objects. */
    public static empty<T, M>(policy: MeasurePolicy<T, M>): MeasuredRope<T, M> { return new MeasuredRope(FingerTree.empty(policy), policy); }
    /** Build a rope from the given elements. */
    public static from<T, M>(values: Iterable<T>, policy: MeasurePolicy<T, M>): MeasuredRope<T, M> { return new MeasuredRope(FingerTree.from(values, policy), policy); }
    /** A cursor at the given gap of the rope. */
    public getCursor(position = 0): MeasuredRopeCursor<T, M> { return new MeasuredRopeCursor(this, position); }
    /** A cursor at the first position whose inclusive prefix measure satisfies the predicate. */
    public getCursorByMeasure(predicate: (measure: M) => boolean): MeasuredRopeCursor<T, M> | undefined {
        const located = this.locateByMeasure(predicate); return located.found ? new MeasuredRopeCursor(this, located.index) : undefined;
    }
    /** Seek by measure and report whether any prefix satisfied the predicate. */
    public cursorByMeasure(predicate: (measure: M) => boolean): MeasuredRopeCursorSearch<T, M> {
        const located = this.locateByMeasure(predicate); return { cursor: new MeasuredRopeCursor(this, located.found ? located.index : this.size), found: located.found };
    }
    /** Number of elements. */
    public get size(): number { return this.#items.size; }
    /** Whether the rope holds no elements. */
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    /** The combined measure of every element, read from the cached root measure. */
    public measure(): M { return this.#items.measure(); }
    /** The first element, or `undefined` when empty. */
    public front(): T | undefined { return this.#items.front(); }
    /** The last element, or `undefined` when empty. */
    public back(): T | undefined { return this.#items.back(); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(index: number): T | undefined { return this.#items.get(index); }
    /**
     * The combined measure of the first `count` elements, summed from cached node measures rather
     * than element by element.
     */
    public prefixMeasure(count: number): M | undefined { return this.#items.prefixMeasure(count); }
    /** Copy a run of elements into the destination, reporting whether the range was valid. */
    public copyTo(index: number, destination: T[], destinationIndex: number = destination.length, count: number = this.size - index): boolean {
        const slice = this.slice(index, count); if (slice === undefined || destinationIndex < 0 || !Number.isInteger(destinationIndex)) return false;
        destination.splice(destinationIndex, 0, ...slice); return true;
    }
    /** A rope with the value added at the front. */
    public pushFront(value: T): MeasuredRope<T, M> { return new MeasuredRope(this.#items.prepend(value), this.policy); }
    /** A rope with the value added at the back. */
    public pushBack(value: T): MeasuredRope<T, M> { return new MeasuredRope(this.#items.append(value), this.policy); }
    /** A map with the key bound to the value, adding or replacing as needed. */
    public setItem(index: number, value: T): MeasuredRope<T, M> | undefined { const next = this.#items.setItem(index, value); return next === undefined ? undefined : new MeasuredRope(next, this.policy); }
    /** A rope with the value inserted so that it ends up at the given index. */
    public insertAt(index: number, value: T): MeasuredRope<T, M> | undefined { const next = this.#items.insertAt(index, value); return next === undefined ? undefined : new MeasuredRope(next, this.policy); }
    /** A rope with every value inserted at the given index, in order. */
    public insertRange(index: number, values: Iterable<T>): MeasuredRope<T, M> | undefined {
        const materialized = materializeRopeRange(values);
        const split = this.splitAt(index); if (split === undefined) return undefined;
        return split.left.concat(MeasuredRope.from(materialized, this.policy)).concat(split.right);
    }
    /** A rope without the element at the given index. */
    public removeAt(index: number): MeasuredRope<T, M> | undefined { const next = this.#items.removeAt(index); return next === undefined ? undefined : new MeasuredRope(next, this.policy); }
    /** A rope without the given run of elements. */
    public removeRange(index: number, count: number): MeasuredRope<T, M> | undefined {
        if (!Number.isInteger(index) || !Number.isInteger(count) || index < 0 || count < 0 || index + count > this.size) return undefined;
        const first = this.splitAt(index)!; const second = first.right.splitAt(count)!; return first.left.concat(second.right);
    }
    /** The given run of elements as a new rope, sharing structure with the receiver. */
    public slice(index: number, count: number): MeasuredRope<T, M> | undefined {
        if (!Number.isInteger(index) || !Number.isInteger(count) || index < 0 || count < 0 || index + count > this.size) return undefined;
        return this.splitAt(index)!.right.splitAt(count)!.left;
    }
    /**
     * The elements before the index and those from it on; both halves share structure with the
     * receiver.
     */
    public splitAt(index: number): MeasuredRopeSplit<T, M> | undefined {
        const split = this.#items.splitAtIndex(index); return split === undefined ? undefined : { left: new MeasuredRope(split.left, this.policy), right: new MeasuredRope(split.right, this.policy) };
    }
    /** Split at the first position whose inclusive prefix measure satisfies the predicate. */
    public splitByMeasure(predicate: (measure: M) => boolean): MeasuredRopeSplit<T, M> { const split = this.#items.split(predicate); return { left: new MeasuredRope(split.left, this.policy), right: new MeasuredRope(split.right, this.policy) }; }
    /** Where the first element satisfying the predicate sits, reported without splitting. */
    public locateByMeasure(predicate: (measure: M) => boolean): MeasuredRopeLocate<T, M> { const result = this.#items.tryLocate(predicate); return { index: result.index, measureBefore: result.measureBefore, value: result.item, found: result.found }; }
    /**
     * This rope's elements followed by the other's, joining the two trees rather than copying
     * either.
     */
    public concat(other: MeasuredRope<T, M>): MeasuredRope<T, M> { if (this.isEmpty) return other; if (other.isEmpty) return this; return new MeasuredRope(this.#items.concat(other.#items), this.policy); }
    /**
     * An equal rope whose chunks have been merged; trades one linear rebuild for cheaper later
     * traversal.
     */
    public compact(): MeasuredRope<T, M> { return MeasuredRope.from(this, this.policy); }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return this.#items.toArray(); }
    /**
     * Whether both ropes share a node by identity. A representation test used to confirm a no-op
     * avoided copying, not an equality test.
     */
    public sharesStorageWith(other: MeasuredRope<T, M>): boolean { return this.#items.sharesStorageWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

/** Counts line-feed UTF-16 code units. */
export class NewlineMeasure implements MeasurePolicy<string, number> {
    /** The two-sided identity for `combine`, and the measure of the empty sequence. */
    public readonly identity = 0;
    /** Combine two measures in order. Must be associative; it need not be commutative. */
    public combine(left: number, right: number): number { return left + right; }
    /** The combined measure of every element, read from the cached root measure. */
    public measure(element: string): number { return element === "\n" ? 1 : 0; }
}

const newlineMeasure = new NewlineMeasure();
/** A zero-based line and column position within a text rope. */
export interface LineColumn { readonly line: number; readonly column: number }

/** UTF-16 text rope with line navigation. */
export class TextRope implements Iterable<string> {
    readonly #characters: MeasuredRope<string, number>;
    private constructor(characters: MeasuredRope<string, number>) { this.#characters = characters; }
    /** The empty rope, retaining the supplied policy objects. */
    public static empty(): TextRope { return new TextRope(MeasuredRope.empty(newlineMeasure)); }
    /** Build a text rope from the given text, one element per code point. */
    public static fromText(text: string): TextRope { return new TextRope(MeasuredRope.from(text.split(""), newlineMeasure)); }
    /** Build a text rope from individual characters. */
    public static fromCharacters(characters: Iterable<string>): TextRope { return new TextRope(MeasuredRope.from(characters, newlineMeasure)); }
    /**
     * Wraps an existing newline-measured rope in O(1) without rebuilding it.
     *
     * Unlike {@link fromCharacters}, this allocates no nodes and invokes no measure callbacks, so a
     * cursor edit keeps the structural sharing its underlying persistent edit already established.
     * The argument must carry the shared newline measure policy, which is the only policy under
     * which the cached line counts this facade reads are meaningful.
     */
    public static fromMeasuredRope(characters: MeasuredRope<string, number>): TextRope {
        if (characters.policy !== newlineMeasure) throw new TypeError("TextRope requires the shared newline measure policy.");
        return new TextRope(characters);
    }
    /** A cursor at the given gap of the rope. */
    public getCursor(position = 0): TextRopeCursor { return new TextRopeCursor(this.#characters.getCursor(position), this); }
    /** Number of characters. */
    public get size(): number { return this.#characters.size; }
    /** Whether the rope holds no characters. */
    public get isEmpty(): boolean { return this.#characters.isEmpty; }
    /** Materialize the whole rope as a string. */
    public asString(): string { return this.#characters.toArray().join(""); }
    /**
     * Number of lines, counting the trailing line even when empty. Constant time, because newline
     * counts are cached in the measure.
     */
    public lineCount(): number { return this.#characters.measure() + 1; }
    /**
     * The zero-based line containing the given character offset. The cached newline counts avoid
     * scanning the text.
     */
    public lineOfOffset(offset: number): number | undefined { return this.#characters.prefixMeasure(offset); }
    /** The offset at which the given line begins. */
    public lineStartOffset(line: number): number | undefined {
        if (!Number.isInteger(line) || line < 0 || line >= this.lineCount()) return undefined;
        if (line === 0) return 0;
        const located = this.#characters.locateByMeasure((count) => count >= line);
        return located.found ? located.index + 1 : undefined;
    }
    /** Convert a character offset into a zero-based line and column. */
    public lineColumnOf(offset: number): LineColumn | undefined {
        const line = this.lineOfOffset(offset); if (line === undefined) return undefined;
        const start = this.lineStartOffset(line)!; return { line, column: offset - start };
    }
    /**
     * Convert a zero-based line and column back into a character offset, the inverse of
     * `lineColumnOf`.
     */
    public offsetOf(line: number, column: number): number | undefined {
        if (!Number.isInteger(column) || column < 0) return undefined;
        const start = this.lineStartOffset(line); if (start === undefined) return undefined;
        const end = line + 1 < this.lineCount() ? this.lineStartOffset(line + 1)! - 1 : this.size;
        return start + column <= end ? start + column : undefined;
    }
    /** The text of the given line, without its terminator. */
    public getLine(line: number): string | undefined {
        const start = this.lineStartOffset(line); if (start === undefined) return undefined;
        const end = line + 1 < this.lineCount() ? this.lineStartOffset(line + 1)! - 1 : this.size;
        return this.#characters.slice(start, end - start)!.toArray().join("");
    }
    /** Every line's text, without terminators. */
    public lines(): string[] { return Array.from({ length: this.lineCount() }, (_, line) => this.getLine(line)!); }
    /** The underlying characters as a plain rope, dropping the newline measure. */
    public toRope(): Rope<string> { return Rope.from(this.#characters); }
    /** The underlying characters as a measured rope that keeps the newline measure. */
    public toMeasuredRope(): MeasuredRope<string, number> { return this.#characters; }
    public [Symbol.iterator](): IterableIterator<string> { return this.#characters[Symbol.iterator](); }
}

/** Mutable text accumulator publishing persistent rope snapshots. */
export class RopeBuilder {
    #parts: string[] = [];
    #size = 0;
    /** Number of elements. */
    public get size(): number { return this.#size; }
    /** Whether the rope holds no elements. */
    public get isEmpty(): boolean { return this.#size === 0; }
    /** The accumulated text, without building a rope. */
    public asString(): string { return this.#parts.join(""); }
    /** A rope with the value added at the back. */
    public append(text: string): this { this.#parts.push(text); this.#size += text.length; return this; }
    /** Append one character, returning the builder for chaining. */
    public appendChar(value: string): this { if (value.length !== 1) throw new RangeError("appendChar requires one UTF-16 code unit."); return this.append(value); }
    /** Append the text followed by a line feed, returning the builder for chaining. */
    public appendLine(text = ""): this { return this.append(text).append("\n"); }
    /** An empty rope retaining the same policies; returns the receiver when already empty. */
    public clear(): this { this.#parts = []; this.#size = 0; return this; }
    /** Build a character rope from the accumulated text, leaving the builder usable. */
    public toRope(): Rope<string> { return Rope.fromText(this.asString()); }
    /** Build a text rope from the accumulated text, leaving the builder usable. */
    public toTextRope(): TextRope { return TextRope.fromText(this.asString()); }
}

/** Immutable persistent gap cursor over a positional rope. */
export class RopeCursor<T> {
    public constructor(readonly rope: Rope<T>, readonly position: number = 0) {
        if (!Number.isInteger(position) || position < 0 || position > rope.size) throw new RangeError("Cursor position is outside the rope.");
    }
    /** Number of elements. */
    public get size(): number { return this.rope.size; }
    /** Retained alias for {@link size}, which is how every other cursor in this package spells length. */
    public get count(): number { return this.size; }
    /** Whether the gap precedes the first element. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last element. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The element immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): T | undefined { return this.position === 0 ? undefined : this.rope.get(this.position - 1); }
    /** The element immediately after the gap, or `undefined` at the end. */
    public peekNext(): T | undefined { return this.position === this.size ? undefined : this.rope.get(this.position); }
    /**
     * The element before the gap, wrapped for presence so a stored `undefined` stays distinct from
     * "nothing there".
     */
    public peekPreviousEntry(): RopeCursorPeek<T> | undefined { return this.position === 0 ? undefined : { value: this.rope.get(this.position - 1)! }; }
    /** The element after the gap, wrapped for presence. */
    public peekNextEntry(): RopeCursorPeek<T> | undefined { return this.position === this.size ? undefined : { value: this.rope.get(this.position)! }; }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): RopeCursor<T> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new RopeCursor(this.rope, this.position - 1); }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): RopeCursor<T> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new RopeCursor(this.rope, this.position + 1); }
    /** A cursor at the given position within the same rope version. */
    public seek(position: number): RopeCursor<T> { return position === this.position ? this : new RopeCursor(this.rope, position); }
    /** Insert at the gap and return a cursor positioned after the new element. */
    public insert(value: T): RopeCursor<T> { return new RopeCursor(this.rope.insertAt(this.position, value)!, this.position + 1); }
    /** Insert every value at the gap, in order, returning a cursor after the last. */
    public insertRange(values: Iterable<T>): RopeCursor<T> {
        const materialized = materializeRopeRange(values); if (materialized.length === 0) return this;
        return new RopeCursor(this.rope.insertRange(this.position, materialized)!, this.position + materialized.length);
    }
    /** Remove the element before the gap and return a cursor in its place. */
    public deletePrevious(): RopeCursor<T> { if (this.isAtStart) throw new RangeError("No element precedes the cursor."); return new RopeCursor(this.rope.removeAt(this.position - 1)!, this.position - 1); }
    /** Remove the element after the gap and return a cursor in its place. */
    public deleteNext(): RopeCursor<T> { if (this.isAtEnd) throw new RangeError("No element follows the cursor."); return new RopeCursor(this.rope.removeAt(this.position)!, this.position); }
    /** Replace the element after the gap, keeping the gap where it is. */
    public replaceNext(value: T): RopeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        const withoutCurrent = this.rope.removeAt(this.position)!;
        return new RopeCursor(withoutCurrent.insertAt(this.position, value)!, this.position);
    }
    /** The rope version this cursor is positioned in. */
    public snapshot(): Rope<T> { return this.rope; }
}

/** Immutable persistent gap cursor retaining ordered prefix and suffix measures. */
export class MeasuredRopeCursor<T, M> {
    public constructor(readonly rope: MeasuredRope<T, M>, readonly position: number = 0) {
        if (!Number.isInteger(position) || position < 0 || position > rope.size) throw new RangeError("Cursor position is outside the rope.");
    }
    /** Number of elements. */
    public get size(): number { return this.rope.size; }
    /** Retained alias for {@link size}, which is how every other cursor in this package spells length. */
    public get count(): number { return this.size; }
    /** Whether the gap precedes the first element. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last element. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The combined measure of every element before the gap. */
    public get measureBefore(): M { return this.rope.prefixMeasure(this.position)!; }
    /** The combined measure of every element at or after the gap. */
    public get measureAfter(): M { return this.rope.slice(this.position, this.size - this.position)!.measure(); }
    /** The element immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): T | undefined { return this.position === 0 ? undefined : this.rope.get(this.position - 1); }
    /** The element immediately after the gap, or `undefined` at the end. */
    public peekNext(): T | undefined { return this.position === this.size ? undefined : this.rope.get(this.position); }
    /** The element before the gap, wrapped for presence. */
    public peekPreviousEntry(): RopeCursorPeek<T> | undefined { return this.position === 0 ? undefined : { value: this.rope.get(this.position - 1)! }; }
    /** The element after the gap, wrapped for presence. */
    public peekNextEntry(): RopeCursorPeek<T> | undefined { return this.position === this.size ? undefined : { value: this.rope.get(this.position)! }; }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): MeasuredRopeCursor<T, M> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new MeasuredRopeCursor(this.rope, this.position - 1); }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): MeasuredRopeCursor<T, M> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new MeasuredRopeCursor(this.rope, this.position + 1); }
    /** A cursor at the given position within the same rope version. */
    public seek(position: number): MeasuredRopeCursor<T, M> { return position === this.position ? this : new MeasuredRopeCursor(this.rope, position); }
    /** A cursor at the first position whose inclusive prefix measure satisfies the predicate. */
    public seekByMeasure(predicate: (measure: M) => boolean): MeasuredRopeCursor<T, M> | undefined {
        const located = this.rope.locateByMeasure(predicate); return located.found ? new MeasuredRopeCursor(this.rope, located.index) : undefined;
    }
    /** Seek by measure and report whether any prefix satisfied the predicate. */
    public searchByMeasure(predicate: (measure: M) => boolean): MeasuredRopeCursorSearch<T, M> {
        const located = this.rope.locateByMeasure(predicate);
        return { cursor: this.seek(located.found ? located.index : this.size), found: located.found };
    }
    /** Insert at the gap and return a cursor positioned after the new element. */
    public insert(value: T): MeasuredRopeCursor<T, M> { return new MeasuredRopeCursor(this.rope.insertAt(this.position, value)!, this.position + 1); }
    /** Insert every value at the gap, in order, returning a cursor after the last. */
    public insertRange(values: Iterable<T>): MeasuredRopeCursor<T, M> {
        const materialized = materializeRopeRange(values); if (materialized.length === 0) return this;
        return new MeasuredRopeCursor(this.rope.insertRange(this.position, materialized)!, this.position + materialized.length);
    }
    /** Remove the element before the gap and return a cursor in its place. */
    public deletePrevious(): MeasuredRopeCursor<T, M> { if (this.isAtStart) throw new RangeError("No element precedes the cursor."); return new MeasuredRopeCursor(this.rope.removeAt(this.position - 1)!, this.position - 1); }
    /** Remove the element after the gap and return a cursor in its place. */
    public deleteNext(): MeasuredRopeCursor<T, M> { if (this.isAtEnd) throw new RangeError("No element follows the cursor."); return new MeasuredRopeCursor(this.rope.removeAt(this.position)!, this.position); }
    /** Replace the element after the gap, keeping the gap where it is. */
    public replaceNext(value: T): MeasuredRopeCursor<T, M> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        const withoutCurrent = this.rope.removeAt(this.position)!;
        return new MeasuredRopeCursor(withoutCurrent.insertAt(this.position, value)!, this.position);
    }
    /** The rope version this cursor is positioned in. */
    public snapshot(): MeasuredRope<T, M> { return this.rope; }
}

/** Text-specialized measured cursor with UTF-16 line/column navigation. */
export class TextRopeCursor {
    #snapshot: TextRope | undefined;
    /**
     * Binds a measured character cursor to its own text version.
     *
     * A supplied `snapshot` must be the very version the cursor is anchored to. Both parameters are
     * public, so without this check a caller could pair a cursor with an unrelated document and have
     * {@link snapshot}, {@link column}, {@link seekLineColumn}, and {@link searchLineColumn} answer
     * against text the cursor does not describe. Omitting the argument is always safe: the text
     * facade is then derived on demand.
     */
    public constructor(readonly cursor: MeasuredRopeCursor<string, number>, snapshot?: TextRope) {
        if (snapshot !== undefined && snapshot.toMeasuredRope() !== cursor.snapshot()) {
            throw new TypeError("Text cursor snapshot must be the version the measured cursor is anchored to.");
        }
        this.#snapshot = snapshot;
    }
    /** Number of characters. */
    public get size(): number { return this.cursor.size; }
    /** Retained alias for {@link size}, which is how every other cursor in this package spells length. */
    public get count(): number { return this.size; }
    /** The cursor's gap index. */
    public get position(): number { return this.cursor.position; }
    /** The zero-based line the gap sits on. */
    public get line(): number { return this.cursor.measureBefore; }
    /** The zero-based column the gap sits at within its line. */
    public get column(): number { return this.position - this.snapshot().lineStartOffset(this.line)!; }
    /** Whether the gap precedes the first character. */
    public get isAtStart(): boolean { return this.cursor.isAtStart; }
    /** Whether the gap follows the last character. */
    public get isAtEnd(): boolean { return this.cursor.isAtEnd; }
    /** The character immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): string | undefined { return this.cursor.peekPrevious(); }
    /** The character immediately after the gap, or `undefined` at the end. */
    public peekNext(): string | undefined { return this.cursor.peekNext(); }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): TextRopeCursor { return new TextRopeCursor(this.cursor.movePrevious(), this.#snapshot); }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): TextRopeCursor { return new TextRopeCursor(this.cursor.moveNext(), this.#snapshot); }
    /** A cursor at the given position within the same rope version. */
    public seek(position: number): TextRopeCursor { return position === this.position ? this : new TextRopeCursor(this.cursor.seek(position), this.#snapshot); }
    /** A cursor at the given zero-based line and column. */
    public seekLineColumn(line: number, column: number): TextRopeCursor {
        const offset = this.snapshot().offsetOf(line, column); if (offset === undefined) throw new RangeError("Invalid line/column."); return this.seek(offset);
    }
    /** Inserts `text` as UTF-16 code units; empty text is a no-op that preserves cursor and snapshot identity. */
    public insert(text: string): TextRopeCursor {
        const edited = this.cursor.insertRange(text.split(""));
        return edited === this.cursor ? this : new TextRopeCursor(edited);
    }
    /** Remove the character before the gap and return a cursor in its place. */
    public deletePrevious(): TextRopeCursor { return new TextRopeCursor(this.cursor.deletePrevious()); }
    /** Remove the character after the gap and return a cursor in its place. */
    public deleteNext(): TextRopeCursor { return new TextRopeCursor(this.cursor.deleteNext()); }
    /** Replace the character after the gap, keeping the gap where it is. */
    public replaceNext(value: string): TextRopeCursor {
        if (value.length !== 1) throw new RangeError("Replacement must contain one UTF-16 code unit."); return new TextRopeCursor(this.cursor.replaceNext(value));
    }
    /** Seek to a line and column, reporting whether that position exists. */
    public searchLineColumn(line: number, column: number): TextRopeCursorSearch {
        const offset = this.snapshot().offsetOf(line, column); return offset === undefined ? { cursor: this, found: false } : { cursor: this.seek(offset), found: true };
    }
    /** Returns the retained text version, wrapping the measured rope in O(1) rather than rebuilding it. */
    public snapshot(): TextRope { return this.#snapshot ??= TextRope.fromMeasuredRope(this.cursor.snapshot()); }
}
