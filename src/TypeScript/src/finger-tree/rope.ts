import { FingerTree, PersistentDeque } from "./core.js";
import type { MeasurePolicy } from "./measures.js";

/** Persistent positional rope. */
export class Rope<T> implements Iterable<T> {
    readonly #items: PersistentDeque<T>;
    private constructor(items: PersistentDeque<T>) { this.#items = items; }
    public static empty<T>(): Rope<T> { return new Rope(PersistentDeque.empty<T>()); }
    public static from<T>(values: Iterable<T>): Rope<T> { return new Rope(PersistentDeque.from(values)); }
    public static fromText(text: string): Rope<string> { return Rope.from(text.split("")); }
    public getCursor(position = 0): RopeCursor<T> { return new RopeCursor(this, position); }
    public get size(): number { return this.#items.size; }
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    public front(): T | undefined { return this.#items.front(); }
    public back(): T | undefined { return this.#items.back(); }
    public get(index: number): T | undefined { return this.#items.get(index); }
    public copyTo(index: number, destination: T[], destinationIndex: number = destination.length, count: number = this.size - index): boolean {
        if (!Number.isInteger(index) || !Number.isInteger(destinationIndex) || !Number.isInteger(count) || index < 0 || count < 0 || index + count > this.size || destinationIndex < 0) return false;
        destination.splice(destinationIndex, 0, ...this.slice(index, count)!);
        return true;
    }
    public pushFront(value: T): Rope<T> { return new Rope(this.#items.prepend(value)); }
    public pushBack(value: T): Rope<T> { return new Rope(this.#items.append(value)); }
    public setItem(index: number, value: T): Rope<T> | undefined { const next = this.#items.setItem(index, value); return next === undefined ? undefined : next === this.#items ? this : new Rope(next); }
    public insertAt(index: number, value: T): Rope<T> | undefined { const next = this.#items.insertAt(index, value); return next === undefined ? undefined : new Rope(next); }
    public insertRange(index: number, values: Iterable<T>): Rope<T> | undefined {
        const split = this.#items.splitAt(index); if (split === undefined) return undefined;
        return new Rope(split.left.concat(PersistentDeque.from(values)).concat(split.right));
    }
    public removeAt(index: number): Rope<T> | undefined { const next = this.#items.removeAt(index); return next === undefined ? undefined : new Rope(next); }
    public removeRange(index: number, count: number): Rope<T> | undefined {
        const split = this.#items.splitRange(index, count); return split === undefined ? undefined : new Rope(split.before.concat(split.after));
    }
    public slice(index: number, count: number): Rope<T> | undefined { const split = this.#items.splitRange(index, count); return split === undefined ? undefined : new Rope(split.range); }
    public splitAt(index: number): readonly [Rope<T>, Rope<T>] | undefined { const split = this.#items.splitAt(index); return split === undefined ? undefined : [new Rope(split.left), new Rope(split.right)]; }
    public concat(other: Rope<T>): Rope<T> { if (this.isEmpty) return other; if (other.isEmpty) return this; return new Rope(this.#items.concat(other.#items)); }
    public compact(): Rope<T> { return Rope.from(this); }
    public toArray(): T[] { return this.#items.toArray(); }
    public sharesStorageWith(other: Rope<T>): boolean { return this.#items.sharesStorageWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

export interface MeasuredRopeSplit<T, M> { readonly left: MeasuredRope<T, M>; readonly right: MeasuredRope<T, M> }
export interface MeasuredRopeLocate<T, M> { readonly index: number; readonly measureBefore: M; readonly value: T | undefined; readonly found: boolean }
export interface RopeCursorPeek<T> { readonly value: T }
export interface MeasuredRopeCursorSearch<T, M> { readonly cursor: MeasuredRopeCursor<T, M>; readonly found: boolean }
export interface TextRopeCursorSearch { readonly cursor: TextRopeCursor; readonly found: boolean }

/** Positional rope carrying a caller-defined cached monoidal measure. */
export class MeasuredRope<T, M> implements Iterable<T> {
    readonly #items: FingerTree<T, M>;
    public readonly policy: MeasurePolicy<T, M>;
    private constructor(items: FingerTree<T, M>, policy: MeasurePolicy<T, M>) { this.#items = items; this.policy = policy; }
    public static empty<T, M>(policy: MeasurePolicy<T, M>): MeasuredRope<T, M> { return new MeasuredRope(FingerTree.empty(policy), policy); }
    public static from<T, M>(values: Iterable<T>, policy: MeasurePolicy<T, M>): MeasuredRope<T, M> { return new MeasuredRope(FingerTree.from(values, policy), policy); }
    public getCursor(position = 0): MeasuredRopeCursor<T, M> { return new MeasuredRopeCursor(this, position); }
    public getCursorByMeasure(predicate: (measure: M) => boolean): MeasuredRopeCursor<T, M> | undefined {
        const located = this.locateByMeasure(predicate); return located.found ? new MeasuredRopeCursor(this, located.index) : undefined;
    }
    public cursorByMeasure(predicate: (measure: M) => boolean): MeasuredRopeCursorSearch<T, M> {
        const located = this.locateByMeasure(predicate); return { cursor: new MeasuredRopeCursor(this, located.found ? located.index : this.size), found: located.found };
    }
    public get size(): number { return this.#items.size; }
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    public measure(): M { return this.#items.measure(); }
    public front(): T | undefined { return this.#items.front(); }
    public back(): T | undefined { return this.#items.back(); }
    public get(index: number): T | undefined { return this.#items.get(index); }
    public prefixMeasure(count: number): M | undefined { return this.#items.prefixMeasure(count); }
    public copyTo(index: number, destination: T[], destinationIndex: number = destination.length, count: number = this.size - index): boolean {
        const slice = this.slice(index, count); if (slice === undefined || destinationIndex < 0 || !Number.isInteger(destinationIndex)) return false;
        destination.splice(destinationIndex, 0, ...slice); return true;
    }
    public pushFront(value: T): MeasuredRope<T, M> { return new MeasuredRope(this.#items.prepend(value), this.policy); }
    public pushBack(value: T): MeasuredRope<T, M> { return new MeasuredRope(this.#items.append(value), this.policy); }
    public setItem(index: number, value: T): MeasuredRope<T, M> | undefined { const next = this.#items.setItem(index, value); return next === undefined ? undefined : new MeasuredRope(next, this.policy); }
    public insertAt(index: number, value: T): MeasuredRope<T, M> | undefined { const next = this.#items.insertAt(index, value); return next === undefined ? undefined : new MeasuredRope(next, this.policy); }
    public insertRange(index: number, values: Iterable<T>): MeasuredRope<T, M> | undefined {
        const split = this.splitAt(index); if (split === undefined) return undefined;
        return split.left.concat(MeasuredRope.from(values, this.policy)).concat(split.right);
    }
    public removeAt(index: number): MeasuredRope<T, M> | undefined { const next = this.#items.removeAt(index); return next === undefined ? undefined : new MeasuredRope(next, this.policy); }
    public removeRange(index: number, count: number): MeasuredRope<T, M> | undefined {
        if (!Number.isInteger(index) || !Number.isInteger(count) || index < 0 || count < 0 || index + count > this.size) return undefined;
        const first = this.splitAt(index)!; const second = first.right.splitAt(count)!; return first.left.concat(second.right);
    }
    public slice(index: number, count: number): MeasuredRope<T, M> | undefined {
        if (!Number.isInteger(index) || !Number.isInteger(count) || index < 0 || count < 0 || index + count > this.size) return undefined;
        return this.splitAt(index)!.right.splitAt(count)!.left;
    }
    public splitAt(index: number): MeasuredRopeSplit<T, M> | undefined {
        const split = this.#items.splitAtIndex(index); return split === undefined ? undefined : { left: new MeasuredRope(split.left, this.policy), right: new MeasuredRope(split.right, this.policy) };
    }
    public splitByMeasure(predicate: (measure: M) => boolean): MeasuredRopeSplit<T, M> { const split = this.#items.split(predicate); return { left: new MeasuredRope(split.left, this.policy), right: new MeasuredRope(split.right, this.policy) }; }
    public locateByMeasure(predicate: (measure: M) => boolean): MeasuredRopeLocate<T, M> { const result = this.#items.tryLocate(predicate); return { index: result.index, measureBefore: result.measureBefore, value: result.item, found: result.found }; }
    public concat(other: MeasuredRope<T, M>): MeasuredRope<T, M> { if (this.isEmpty) return other; if (other.isEmpty) return this; return new MeasuredRope(this.#items.concat(other.#items), this.policy); }
    public compact(): MeasuredRope<T, M> { return MeasuredRope.from(this, this.policy); }
    public toArray(): T[] { return this.#items.toArray(); }
    public sharesStorageWith(other: MeasuredRope<T, M>): boolean { return this.#items.sharesStorageWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

/** Counts line-feed UTF-16 code units. */
export class NewlineMeasure implements MeasurePolicy<string, number> {
    public readonly identity = 0;
    public combine(left: number, right: number): number { return left + right; }
    public measure(element: string): number { return element === "\n" ? 1 : 0; }
}

const newlineMeasure = new NewlineMeasure();
export interface LineColumn { readonly line: number; readonly column: number }

/** UTF-16 text rope with line navigation. */
export class TextRope implements Iterable<string> {
    readonly #characters: MeasuredRope<string, number>;
    private constructor(characters: MeasuredRope<string, number>) { this.#characters = characters; }
    public static empty(): TextRope { return new TextRope(MeasuredRope.empty(newlineMeasure)); }
    public static fromText(text: string): TextRope { return new TextRope(MeasuredRope.from(text.split(""), newlineMeasure)); }
    public static fromCharacters(characters: Iterable<string>): TextRope { return new TextRope(MeasuredRope.from(characters, newlineMeasure)); }
    public getCursor(position = 0): TextRopeCursor { return new TextRopeCursor(this.#characters.getCursor(position), this); }
    public get size(): number { return this.#characters.size; }
    public get isEmpty(): boolean { return this.#characters.isEmpty; }
    public asString(): string { return this.#characters.toArray().join(""); }
    public lineCount(): number { return this.#characters.measure() + 1; }
    public lineOfOffset(offset: number): number | undefined { return this.#characters.prefixMeasure(offset); }
    public lineStartOffset(line: number): number | undefined {
        if (!Number.isInteger(line) || line < 0 || line >= this.lineCount()) return undefined;
        if (line === 0) return 0;
        const located = this.#characters.locateByMeasure((count) => count >= line);
        return located.found ? located.index + 1 : undefined;
    }
    public lineColumnOf(offset: number): LineColumn | undefined {
        const line = this.lineOfOffset(offset); if (line === undefined) return undefined;
        const start = this.lineStartOffset(line)!; return { line, column: offset - start };
    }
    public offsetOf(line: number, column: number): number | undefined {
        if (!Number.isInteger(column) || column < 0) return undefined;
        const start = this.lineStartOffset(line); if (start === undefined) return undefined;
        const end = line + 1 < this.lineCount() ? this.lineStartOffset(line + 1)! - 1 : this.size;
        return start + column <= end ? start + column : undefined;
    }
    public getLine(line: number): string | undefined {
        const start = this.lineStartOffset(line); if (start === undefined) return undefined;
        const end = line + 1 < this.lineCount() ? this.lineStartOffset(line + 1)! - 1 : this.size;
        return this.#characters.slice(start, end - start)!.toArray().join("");
    }
    public lines(): string[] { return Array.from({ length: this.lineCount() }, (_, line) => this.getLine(line)!); }
    public toRope(): Rope<string> { return Rope.from(this.#characters); }
    public toMeasuredRope(): MeasuredRope<string, number> { return this.#characters; }
    public [Symbol.iterator](): IterableIterator<string> { return this.#characters[Symbol.iterator](); }
}

/** Mutable text accumulator publishing persistent rope snapshots. */
export class RopeBuilder {
    #parts: string[] = [];
    #size = 0;
    public get size(): number { return this.#size; }
    public get isEmpty(): boolean { return this.#size === 0; }
    public asString(): string { return this.#parts.join(""); }
    public append(text: string): this { this.#parts.push(text); this.#size += text.length; return this; }
    public appendChar(value: string): this { if (value.length !== 1) throw new RangeError("appendChar requires one UTF-16 code unit."); return this.append(value); }
    public appendLine(text = ""): this { return this.append(text).append("\n"); }
    public clear(): this { this.#parts = []; this.#size = 0; return this; }
    public toRope(): Rope<string> { return Rope.fromText(this.asString()); }
    public toTextRope(): TextRope { return TextRope.fromText(this.asString()); }
}

/** Immutable persistent gap cursor over a positional rope. */
export class RopeCursor<T> {
    public constructor(readonly rope: Rope<T>, readonly position: number = 0) {
        if (!Number.isInteger(position) || position < 0 || position > rope.size) throw new RangeError("Cursor position is outside the rope.");
    }
    public get count(): number { return this.rope.size; }
    public get isAtStart(): boolean { return this.position === 0; }
    public get isAtEnd(): boolean { return this.position === this.count; }
    public peekPrevious(): T | undefined { return this.position === 0 ? undefined : this.rope.get(this.position - 1); }
    public peekNext(): T | undefined { return this.position === this.count ? undefined : this.rope.get(this.position); }
    public peekPreviousEntry(): RopeCursorPeek<T> | undefined { return this.position === 0 ? undefined : { value: this.rope.get(this.position - 1)! }; }
    public peekNextEntry(): RopeCursorPeek<T> | undefined { return this.position === this.count ? undefined : { value: this.rope.get(this.position)! }; }
    public movePrevious(): RopeCursor<T> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new RopeCursor(this.rope, this.position - 1); }
    public moveNext(): RopeCursor<T> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new RopeCursor(this.rope, this.position + 1); }
    public seek(position: number): RopeCursor<T> { return position === this.position ? this : new RopeCursor(this.rope, position); }
    public insert(value: T): RopeCursor<T> { return new RopeCursor(this.rope.insertAt(this.position, value)!, this.position + 1); }
    public insertRange(values: Iterable<T>): RopeCursor<T> {
        const materialized = [...values]; if (materialized.length === 0) return this;
        return new RopeCursor(this.rope.insertRange(this.position, materialized)!, this.position + materialized.length);
    }
    public deletePrevious(): RopeCursor<T> { if (this.isAtStart) throw new RangeError("No element precedes the cursor."); return new RopeCursor(this.rope.removeAt(this.position - 1)!, this.position - 1); }
    public deleteNext(): RopeCursor<T> { if (this.isAtEnd) throw new RangeError("No element follows the cursor."); return new RopeCursor(this.rope.removeAt(this.position)!, this.position); }
    public replaceNext(value: T): RopeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        const withoutCurrent = this.rope.removeAt(this.position)!;
        return new RopeCursor(withoutCurrent.insertAt(this.position, value)!, this.position);
    }
    public snapshot(): Rope<T> { return this.rope; }
}

/** Immutable persistent gap cursor retaining ordered prefix and suffix measures. */
export class MeasuredRopeCursor<T, M> {
    public constructor(readonly rope: MeasuredRope<T, M>, readonly position: number = 0) {
        if (!Number.isInteger(position) || position < 0 || position > rope.size) throw new RangeError("Cursor position is outside the rope.");
    }
    public get count(): number { return this.rope.size; }
    public get isAtStart(): boolean { return this.position === 0; }
    public get isAtEnd(): boolean { return this.position === this.count; }
    public get measureBefore(): M { return this.rope.prefixMeasure(this.position)!; }
    public get measureAfter(): M { return this.rope.slice(this.position, this.count - this.position)!.measure(); }
    public peekPrevious(): T | undefined { return this.position === 0 ? undefined : this.rope.get(this.position - 1); }
    public peekNext(): T | undefined { return this.position === this.count ? undefined : this.rope.get(this.position); }
    public peekPreviousEntry(): RopeCursorPeek<T> | undefined { return this.position === 0 ? undefined : { value: this.rope.get(this.position - 1)! }; }
    public peekNextEntry(): RopeCursorPeek<T> | undefined { return this.position === this.count ? undefined : { value: this.rope.get(this.position)! }; }
    public movePrevious(): MeasuredRopeCursor<T, M> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new MeasuredRopeCursor(this.rope, this.position - 1); }
    public moveNext(): MeasuredRopeCursor<T, M> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new MeasuredRopeCursor(this.rope, this.position + 1); }
    public seek(position: number): MeasuredRopeCursor<T, M> { return position === this.position ? this : new MeasuredRopeCursor(this.rope, position); }
    public seekByMeasure(predicate: (measure: M) => boolean): MeasuredRopeCursor<T, M> | undefined {
        const located = this.rope.locateByMeasure(predicate); return located.found ? new MeasuredRopeCursor(this.rope, located.index) : undefined;
    }
    public searchByMeasure(predicate: (measure: M) => boolean): MeasuredRopeCursorSearch<T, M> {
        const located = this.rope.locateByMeasure(predicate);
        return { cursor: this.seek(located.found ? located.index : this.count), found: located.found };
    }
    public insert(value: T): MeasuredRopeCursor<T, M> { return new MeasuredRopeCursor(this.rope.insertAt(this.position, value)!, this.position + 1); }
    public insertRange(values: Iterable<T>): MeasuredRopeCursor<T, M> {
        const materialized = [...values]; if (materialized.length === 0) return this;
        return new MeasuredRopeCursor(this.rope.insertRange(this.position, materialized)!, this.position + materialized.length);
    }
    public deletePrevious(): MeasuredRopeCursor<T, M> { if (this.isAtStart) throw new RangeError("No element precedes the cursor."); return new MeasuredRopeCursor(this.rope.removeAt(this.position - 1)!, this.position - 1); }
    public deleteNext(): MeasuredRopeCursor<T, M> { if (this.isAtEnd) throw new RangeError("No element follows the cursor."); return new MeasuredRopeCursor(this.rope.removeAt(this.position)!, this.position); }
    public replaceNext(value: T): MeasuredRopeCursor<T, M> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        const withoutCurrent = this.rope.removeAt(this.position)!;
        return new MeasuredRopeCursor(withoutCurrent.insertAt(this.position, value)!, this.position);
    }
    public snapshot(): MeasuredRope<T, M> { return this.rope; }
}

/** Text-specialized measured cursor with UTF-16 line/column navigation. */
export class TextRopeCursor {
    #snapshot: TextRope | undefined;
    public constructor(readonly cursor: MeasuredRopeCursor<string, number>, snapshot?: TextRope) { this.#snapshot = snapshot; }
    public get count(): number { return this.cursor.count; }
    public get position(): number { return this.cursor.position; }
    public get line(): number { return this.cursor.measureBefore; }
    public get column(): number { return this.position - this.snapshot().lineStartOffset(this.line)!; }
    public get isAtStart(): boolean { return this.cursor.isAtStart; }
    public get isAtEnd(): boolean { return this.cursor.isAtEnd; }
    public peekPrevious(): string | undefined { return this.cursor.peekPrevious(); }
    public peekNext(): string | undefined { return this.cursor.peekNext(); }
    public movePrevious(): TextRopeCursor { return new TextRopeCursor(this.cursor.movePrevious(), this.#snapshot); }
    public moveNext(): TextRopeCursor { return new TextRopeCursor(this.cursor.moveNext(), this.#snapshot); }
    public seek(position: number): TextRopeCursor { return position === this.position ? this : new TextRopeCursor(this.cursor.seek(position), this.#snapshot); }
    public seekLineColumn(line: number, column: number): TextRopeCursor {
        const offset = this.snapshot().offsetOf(line, column); if (offset === undefined) throw new RangeError("Invalid line/column."); return this.seek(offset);
    }
    public insert(text: string): TextRopeCursor { return new TextRopeCursor(this.cursor.insertRange(text.split(""))); }
    public deletePrevious(): TextRopeCursor { return new TextRopeCursor(this.cursor.deletePrevious()); }
    public deleteNext(): TextRopeCursor { return new TextRopeCursor(this.cursor.deleteNext()); }
    public replaceNext(value: string): TextRopeCursor {
        if (value.length !== 1) throw new RangeError("Replacement must contain one UTF-16 code unit."); return new TextRopeCursor(this.cursor.replaceNext(value));
    }
    public searchLineColumn(line: number, column: number): TextRopeCursorSearch {
        const offset = this.snapshot().offsetOf(line, column); return offset === undefined ? { cursor: this, found: false } : { cursor: this.seek(offset), found: true };
    }
    public snapshot(): TextRope { return this.#snapshot ??= TextRope.fromCharacters(this.cursor.snapshot()); }
}
