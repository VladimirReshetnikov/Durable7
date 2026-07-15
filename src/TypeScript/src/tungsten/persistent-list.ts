import { PersistentDeque } from "../finger-tree/core.js";

export interface PersistentListSplit<T> { readonly left: PersistentList<T>; readonly right: PersistentList<T> }

/** Tungsten-language persistent List vocabulary over the persistent deque. */
export class PersistentList<T> implements Iterable<T> {
    readonly #items: PersistentDeque<T>;
    private constructor(items: PersistentDeque<T>) { this.#items = items; }
    public static empty<T>(): PersistentList<T> { return new PersistentList(PersistentDeque.empty<T>()); }
    public static from<T>(values: Iterable<T>): PersistentList<T> { return new PersistentList(PersistentDeque.from(values)); }
    public get size(): number { return this.#items.size; }
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    public first(): T | undefined { return this.#items.front(); }
    public last(): T | undefined { return this.#items.back(); }
    public get(index: number): T | undefined { return this.#items.get(index); }
    public append(value: T): PersistentList<T> { return new PersistentList(this.#items.append(value)); }
    public prepend(value: T): PersistentList<T> { return new PersistentList(this.#items.prepend(value)); }
    public join(other: PersistentList<T>): PersistentList<T> { if (this.isEmpty) return other; if (other.isEmpty) return this; return new PersistentList(this.#items.concat(other.#items)); }
    public addRange(values: Iterable<T>): PersistentList<T> { return this.join(PersistentList.from(values)); }
    public insert(index: number, value: T): PersistentList<T> | undefined { const next = this.#items.insertAt(index, value); return next === undefined ? undefined : new PersistentList(next); }
    public insertRange(index: number, values: Iterable<T>): PersistentList<T> | undefined { const split = this.#items.splitAt(index); if (split === undefined) return undefined; const inserted = PersistentDeque.from(values); return inserted.isEmpty ? this : new PersistentList(split.left.concat(inserted).concat(split.right)); }
    public removeAt(index: number): PersistentList<T> | undefined { const next = this.#items.removeAt(index); return next === undefined ? undefined : new PersistentList(next); }
    public removeRange(index: number, count: number): PersistentList<T> | undefined { const split = this.#items.splitRange(index, count); return split === undefined ? undefined : new PersistentList(split.before.concat(split.after)); }
    public removeFirst(): PersistentList<T> | undefined { return this.removeAt(0); }
    public removeLast(): PersistentList<T> | undefined { return this.removeAt(this.size - 1); }
    public setItem(index: number, value: T): PersistentList<T> | undefined { const next = this.#items.setItem(index, value); return next === undefined ? undefined : next === this.#items ? this : new PersistentList(next); }
    public updateAt(index: number, updater: (value: T) => T): PersistentList<T> | undefined { if (!Number.isInteger(index) || index < 0 || index >= this.size) return undefined; return this.setItem(index, updater(this.get(index)!)); }
    public getRange(index: number, count: number): PersistentList<T> | undefined { const split = this.#items.splitRange(index, count); return split === undefined ? undefined : new PersistentList(split.range); }
    public take(count: number): PersistentList<T> | undefined { return this.getRange(0, count); }
    public takeLast(count: number): PersistentList<T> | undefined { return !Number.isInteger(count) || count < 0 || count > this.size ? undefined : this.getRange(this.size - count, count); }
    public drop(count: number): PersistentList<T> | undefined { return !Number.isInteger(count) || count < 0 || count > this.size ? undefined : this.getRange(count, this.size - count); }
    public dropLast(count: number): PersistentList<T> | undefined { return !Number.isInteger(count) || count < 0 || count > this.size ? undefined : this.getRange(0, this.size - count); }
    public splitAt(index: number): PersistentListSplit<T> | undefined { const split = this.#items.splitAt(index); return split === undefined ? undefined : { left: new PersistentList(split.left), right: new PersistentList(split.right) }; }
    public reverse(): PersistentList<T> { return this.size <= 1 ? this : PersistentList.from(this.toArray().reverse()); }
    public map<R>(transform: (value: T, index: number) => R): PersistentList<R> { return PersistentList.from(this.toArray().map(transform)); }
    public indexOf(value: T, equivalent: (left: T, right: T) => boolean = Object.is): number { let index = 0; for (const candidate of this) { if (equivalent(candidate, value)) return index; index++; } return -1; }
    public contains(value: T, equivalent: (left: T, right: T) => boolean = Object.is): boolean { return this.indexOf(value, equivalent) >= 0; }
    public toArray(): T[] { return this.#items.toArray(); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}
