import { MeasuredSequence } from "./measured-sequence.js";
import { SizeMeasure } from "./measures.js";
import { defaultComparator, type Comparator } from "./ordering.js";

const sortedSizeMeasure = new SizeMeasure<unknown>();
function policy<T>(): SizeMeasure<T> { return sortedSizeMeasure as SizeMeasure<T>; }

function equalValue<T>(left: T, right: T): boolean { return Object.is(left, right); }

export class SortedDuplicateKeyError extends Error {
    public constructor(message = "An equivalent key is already present.") { super(message); this.name = "SortedDuplicateKeyError"; }
}

export interface SortedAddResult<T> { readonly value: T; readonly added: boolean }
export interface SortedMapEntry<K, V> { readonly key: K; readonly value: V }
export interface SortedMapRemoveResult<K, V> { readonly map: SortedMap<K, V>; readonly value: V }

/** Immutable order-statistic sorted multiset. */
export class SortedBag<T> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, number>;
    public readonly comparator: Comparator<T>;
    private constructor(items: MeasuredSequence<T, number>, comparator: Comparator<T>) { this.#items = items; this.comparator = comparator; }
    public static empty<T>(comparator: Comparator<T> = defaultComparator): SortedBag<T> { return new SortedBag(MeasuredSequence.empty(policy<T>()), comparator); }
    public static from<T>(values: Iterable<T>, comparator: Comparator<T> = defaultComparator): SortedBag<T> {
        return new SortedBag(MeasuredSequence.from(Array.from(values).sort(comparator), policy<T>()), comparator);
    }
    public get size(): number { return this.#items.size; }
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    public min(): T | undefined { return this.#items.front(); }
    public max(): T | undefined { return this.#items.back(); }
    public get(rank: number): T | undefined { return this.#items.at(rank); }
    public contains(value: T): boolean { return this.countOf(value) !== 0; }
    public countLessThan(value: T): number { return this.#items.lowerBound(value, this.comparator); }
    public countAtMost(value: T): number { return this.#items.upperBound(value, this.comparator); }
    public countOf(value: T): number { return this.countAtMost(value) - this.countLessThan(value); }
    public add(value: T): SortedBag<T> { return new SortedBag(this.#items.insertAt(this.countAtMost(value), value)!, this.comparator); }
    public addRange(values: Iterable<T>): SortedBag<T> { let result: SortedBag<T> = this; for (const value of values) result = result.add(value); return result; }
    public remove(value: T): SortedBag<T> {
        const index = this.countLessThan(value);
        return index >= this.size || this.comparator(this.#items.at(index)!, value) !== 0 ? this : new SortedBag(this.#items.removeAt(index)!, this.comparator);
    }
    public removeAll(value: T): SortedBag<T> {
        const start = this.countLessThan(value);
        const end = this.countAtMost(value);
        if (start === end) return this;
        const first = this.#items.splitAt(start)!;
        const second = first.right.splitAt(end - start)!;
        return new SortedBag(first.left.concat(second.right), this.comparator);
    }
    public getRange(start: number, count: number): SortedBag<T> | undefined {
        if (!Number.isInteger(start) || !Number.isInteger(count) || start < 0 || count < 0 || start + count > this.size) return undefined;
        const first = this.#items.splitAt(start)!;
        return new SortedBag(first.right.splitAt(count)!.left, this.comparator);
    }
    public getValueRange(low: T, high: T): SortedBag<T> {
        if (this.comparator(low, high) > 0) return SortedBag.empty(this.comparator);
        return this.getRange(this.countLessThan(low), this.countAtMost(high) - this.countLessThan(low))!;
    }
    public toArray(): T[] { return this.#items.toArray(); }
    public sharesStorageWith(other: SortedBag<T>): boolean { return this.#items.sharesStructureWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

/** Immutable order-statistic sorted set. */
export class SortedSet<T> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, number>;
    public readonly comparator: Comparator<T>;
    private constructor(items: MeasuredSequence<T, number>, comparator: Comparator<T>) { this.#items = items; this.comparator = comparator; }
    public static empty<T>(comparator: Comparator<T> = defaultComparator): SortedSet<T> { return new SortedSet(MeasuredSequence.empty(policy<T>()), comparator); }
    public static from<T>(values: Iterable<T>, comparator: Comparator<T> = defaultComparator): SortedSet<T> {
        let result = SortedSet.empty(comparator);
        for (const value of values) result = result.add(value);
        return result;
    }
    public get size(): number { return this.#items.size; }
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    public min(): T | undefined { return this.#items.front(); }
    public max(): T | undefined { return this.#items.back(); }
    public get(rank: number): T | undefined { return this.#items.at(rank); }
    public indexOf(value: T): number | undefined {
        const index = this.#items.lowerBound(value, this.comparator);
        return index < this.size && this.comparator(this.#items.at(index)!, value) === 0 ? index : undefined;
    }
    public contains(value: T): boolean { return this.indexOf(value) !== undefined; }
    public add(value: T): SortedSet<T> {
        const index = this.#items.lowerBound(value, this.comparator);
        return index < this.size && this.comparator(this.#items.at(index)!, value) === 0 ? this : new SortedSet(this.#items.insertAt(index, value)!, this.comparator);
    }
    public union(values: Iterable<T>): SortedSet<T> { let result: SortedSet<T> = this; for (const value of values) result = result.add(value); return result; }
    public remove(value: T): SortedSet<T> {
        const index = this.indexOf(value);
        return index === undefined ? this : new SortedSet(this.#items.removeAt(index)!, this.comparator);
    }
    public floor(value: T): T | undefined { const index = this.#items.upperBound(value, this.comparator) - 1; return this.get(index); }
    public ceiling(value: T): T | undefined { return this.get(this.#items.lowerBound(value, this.comparator)); }
    public lower(value: T): T | undefined { return this.get(this.#items.lowerBound(value, this.comparator) - 1); }
    public higher(value: T): T | undefined { return this.get(this.#items.upperBound(value, this.comparator)); }
    public getRange(start: number, count: number): SortedSet<T> | undefined {
        if (!Number.isInteger(start) || !Number.isInteger(count) || start < 0 || count < 0 || start + count > this.size) return undefined;
        const first = this.#items.splitAt(start)!;
        return new SortedSet(first.right.splitAt(count)!.left, this.comparator);
    }
    public getValueRange(low: T, high: T): SortedSet<T> {
        if (this.comparator(low, high) > 0) return SortedSet.empty(this.comparator);
        const start = this.#items.lowerBound(low, this.comparator);
        return this.getRange(start, this.#items.upperBound(high, this.comparator) - start)!;
    }
    public intersect(values: Iterable<T>): SortedSet<T> { const other = SortedSet.from(values, this.comparator); let result = SortedSet.empty<T>(this.comparator); for (const value of this) if (other.contains(value)) result = result.add(value); return result.setEquals(this) ? this : result; }
    public except(values: Iterable<T>): SortedSet<T> { let result: SortedSet<T> = this; for (const value of values) result = result.remove(value); return result; }
    public symmetricExcept(values: Iterable<T>): SortedSet<T> { const other = SortedSet.from(values, this.comparator); let result: SortedSet<T> = this; for (const value of other) result = result.contains(value) ? result.remove(value) : result.add(value); return result; }
    public isSubsetOf(values: Iterable<T>): boolean { const other = SortedSet.from(values, this.comparator); return this.size <= other.size && Array.from(this).every((value) => other.contains(value)); }
    public isProperSubsetOf(values: Iterable<T>): boolean { const other = SortedSet.from(values, this.comparator); return this.size < other.size && this.isSubsetOf(other); }
    public isSupersetOf(values: Iterable<T>): boolean { for (const value of values) if (!this.contains(value)) return false; return true; }
    public isProperSupersetOf(values: Iterable<T>): boolean { const other = SortedSet.from(values, this.comparator); return this.size > other.size && this.isSupersetOf(other); }
    public overlaps(values: Iterable<T>): boolean { for (const value of values) if (this.contains(value)) return true; return false; }
    public setEquals(values: Iterable<T>): boolean { const other = values instanceof SortedSet && values.comparator === this.comparator ? values : SortedSet.from(values, this.comparator); return this.size === other.size && this.isSubsetOf(other); }
    public toArray(): T[] { return this.#items.toArray(); }
    public sharesStorageWith(other: SortedSet<T>): boolean { return this.#items.sharesStructureWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

/** Immutable order-statistic sorted map preserving comparator-equivalent representatives. */
export class SortedMap<K, V> implements Iterable<SortedMapEntry<K, V>> {
    readonly #entries: MeasuredSequence<SortedMapEntry<K, V>, number>;
    public readonly comparator: Comparator<K>;
    private constructor(entries: MeasuredSequence<SortedMapEntry<K, V>, number>, comparator: Comparator<K>) { this.#entries = entries; this.comparator = comparator; }
    public static empty<K, V>(comparator: Comparator<K> = defaultComparator): SortedMap<K, V> { return new SortedMap(MeasuredSequence.empty(policy<SortedMapEntry<K, V>>()), comparator); }
    public static from<K, V>(items: Iterable<readonly [K, V] | SortedMapEntry<K, V>>, comparator: Comparator<K> = defaultComparator): SortedMap<K, V> {
        let result = SortedMap.empty<K, V>(comparator);
        for (const item of items) {
            if (Array.isArray(item)) {
                const pair = item as readonly [K, V];
                result = result.setItem(pair[0], pair[1]);
            } else {
                const entry = item as SortedMapEntry<K, V>;
                result = result.setItem(entry.key, entry.value);
            }
        }
        return result;
    }
    public get size(): number { return this.#entries.size; }
    public get isEmpty(): boolean { return this.#entries.isEmpty; }
    #entryComparator = (entry: SortedMapEntry<K, V>, probe: SortedMapEntry<K, V>): number => this.comparator(entry.key, probe.key);
    #probe(key: K): SortedMapEntry<K, V> { return { key, value: undefined as V }; }
    public indexOfKey(key: K): number | undefined {
        const index = this.#entries.lowerBound(this.#probe(key), this.#entryComparator);
        return index < this.size && this.comparator(this.#entries.at(index)!.key, key) === 0 ? index : undefined;
    }
    public containsKey(key: K): boolean { return this.indexOfKey(key) !== undefined; }
    public get(key: K): V | undefined { const index = this.indexOfKey(key); return index === undefined ? undefined : this.#entries.at(index)!.value; }
    public entryAt(rank: number): SortedMapEntry<K, V> | undefined { return this.#entries.at(rank); }
    public minEntry(): SortedMapEntry<K, V> | undefined { return this.#entries.front(); }
    public maxEntry(): SortedMapEntry<K, V> | undefined { return this.#entries.back(); }
    public setItem(key: K, value: V): SortedMap<K, V> {
        const probe = this.#probe(key);
        const index = this.#entries.lowerBound(probe, this.#entryComparator);
        if (index < this.size && this.comparator(this.#entries.at(index)!.key, key) === 0) {
            const current = this.#entries.at(index)!;
            if (equalValue(current.value, value)) return this;
            return new SortedMap(this.#entries.setAt(index, { key: current.key, value })!, this.comparator);
        }
        return new SortedMap(this.#entries.insertAt(index, { key, value })!, this.comparator);
    }
    public insert(key: K, value: V): SortedMap<K, V> { const result = this.tryInsert(key, value); if (!result.added) throw new SortedDuplicateKeyError(); return result.value; }
    public tryInsert(key: K, value: V): SortedAddResult<SortedMap<K, V>> { return this.containsKey(key) ? { value: this, added: false } : { value: this.setItem(key, value), added: true }; }
    public remove(key: K): SortedMap<K, V> { return this.tryRemove(key)?.map ?? this; }
    public tryRemove(key: K): SortedMapRemoveResult<K, V> | undefined { const index = this.indexOfKey(key); if (index === undefined) return undefined; const value = this.#entries.at(index)!.value; return { map: new SortedMap(this.#entries.removeAt(index)!, this.comparator), value }; }
    #lowerBound(key: K): number { return this.#entries.lowerBound(this.#probe(key), this.#entryComparator); }
    #upperBound(key: K): number { return this.#entries.upperBound(this.#probe(key), this.#entryComparator); }
    public floorEntry(key: K): SortedMapEntry<K, V> | undefined { return this.entryAt(this.#upperBound(key) - 1); }
    public ceilingEntry(key: K): SortedMapEntry<K, V> | undefined { return this.entryAt(this.#lowerBound(key)); }
    public lowerEntry(key: K): SortedMapEntry<K, V> | undefined { return this.entryAt(this.#lowerBound(key) - 1); }
    public higherEntry(key: K): SortedMapEntry<K, V> | undefined { return this.entryAt(this.#upperBound(key)); }
    public getRange(start: number, count: number): SortedMap<K, V> | undefined { if (!Number.isInteger(start) || !Number.isInteger(count) || start < 0 || count < 0 || start + count > this.size) return undefined; const first = this.#entries.splitAt(start)!; return new SortedMap(first.right.splitAt(count)!.left, this.comparator); }
    public getKeyRange(low: K, high: K): SortedMap<K, V> { if (this.comparator(low, high) > 0) return SortedMap.empty(this.comparator); const start = this.#lowerBound(low); return this.getRange(start, this.#upperBound(high) - start)!; }
    public toArray(): SortedMapEntry<K, V>[] { return this.#entries.toArray(); }
    public keys(): K[] { return this.toArray().map((entry) => entry.key); }
    public values(): V[] { return this.toArray().map((entry) => entry.value); }
    public sharesStorageWith(other: SortedMap<K, V>): boolean { return this.#entries.sharesStructureWith(other.#entries); }
    public [Symbol.iterator](): IterableIterator<SortedMapEntry<K, V>> { return this.#entries[Symbol.iterator](); }
}

/** Mutable bulk builder that publishes isolated immutable sorted-set snapshots. */
export class SortedSetBuilder<T> {
    #value: SortedSet<T>;
    public constructor(comparator: Comparator<T> = defaultComparator) { this.#value = SortedSet.empty(comparator); }
    public get size(): number { return this.#value.size; }
    public add(value: T): this { this.#value = this.#value.add(value); return this; }
    public remove(value: T): boolean { const next = this.#value.remove(value); const changed = next !== this.#value; this.#value = next; return changed; }
    public clear(): void { this.#value = SortedSet.empty(this.#value.comparator); }
    public toImmutable(): SortedSet<T> { return this.#value; }
}

/** Mutable bulk builder that publishes isolated immutable sorted-map snapshots. */
export class SortedMapBuilder<K, V> {
    #value: SortedMap<K, V>;
    public constructor(comparator: Comparator<K> = defaultComparator) { this.#value = SortedMap.empty(comparator); }
    public get size(): number { return this.#value.size; }
    public set(key: K, value: V): this { this.#value = this.#value.setItem(key, value); return this; }
    public remove(key: K): boolean { const next = this.#value.remove(key); const changed = next !== this.#value; this.#value = next; return changed; }
    public clear(): void { this.#value = SortedMap.empty(this.#value.comparator); }
    public toImmutable(): SortedMap<K, V> { return this.#value; }
}
