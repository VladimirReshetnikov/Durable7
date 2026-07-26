/**
 * Order-statistic sorted bag, set, and map over the measured sequence.
 *
 * The measure caches both element counts and the largest key of each subtree, so one split answers
 * ordered questions and positional questions alike - rank and select as well as lookup, which an
 * ordinary balanced tree cannot do without extra bookkeeping.
 */
import { MeasuredSequence } from "./measured-sequence.js";
import { SizeMeasure } from "./measures.js";
import { defaultComparator, type Comparator } from "./ordering.js";

const sortedSizeMeasure = new SizeMeasure<unknown>();
function policy<T>(): SizeMeasure<T> { return sortedSizeMeasure as SizeMeasure<T>; }

function equalValue<T>(left: T, right: T): boolean { return Object.is(left, right); }

/** Raised when duplicate-rejecting insertion sees an equivalent key. */
export class SortedDuplicateKeyError extends Error {
    public constructor(message = "An equivalent key is already present.") { super(message); this.name = "SortedDuplicateKeyError"; }
}

/** The outcome of a non-overwriting insertion; the value is the receiver when nothing was added. */
export interface SortedAddResult<T> { readonly value: T; readonly added: boolean }
/** One key-value pair as the sorted map stores and presents it. */
export interface SortedMapEntry<K, V> { readonly key: K; readonly value: V }
/** The map remaining after a removal, together with the value that was removed. */
export interface SortedMapRemoveResult<K, V> { readonly map: SortedMap<K, V>; readonly value: V }
/**
 * The outcome of seeking a cursor. On a miss the cursor sits at the lower bound, where the value
 * would be inserted, and remains usable.
 */
export interface OrderedCursorSearch<C> { readonly found: boolean; readonly cursor: C }

/** Immutable order-statistic sorted multiset. */
export class SortedBag<T> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, number>;
    /** The retained comparator defining the ordering. */
    public readonly comparator: Comparator<T>;
    private constructor(items: MeasuredSequence<T, number>, comparator: Comparator<T>) { this.#items = items; this.comparator = comparator; }
    /** The empty bag, retaining the supplied policy objects. */
    public static empty<T>(comparator: Comparator<T> = defaultComparator): SortedBag<T> { return new SortedBag(MeasuredSequence.empty(policy<T>()), comparator); }
    /** Build a bag from the given elements. */
    public static from<T>(values: Iterable<T>, comparator: Comparator<T> = defaultComparator): SortedBag<T> {
        return new SortedBag(MeasuredSequence.from(Array.from(values).sort(comparator), policy<T>()), comparator);
    }
    /** Number of elements. */
    public get size(): number { return this.#items.size; }
    /** Whether the bag holds no elements. */
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    /** The smallest element, or `undefined` when empty. */
    public min(): T | undefined { return this.#items.front(); }
    /** The largest element, or `undefined` when empty. */
    public max(): T | undefined { return this.#items.back(); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(rank: number): T | undefined { return this.#items.at(rank); }
    /** Whether the element is present. */
    public contains(value: T): boolean { return this.countOf(value) !== 0; }
    /** How many elements are strictly below the probe, that is, its rank. */
    public countLessThan(value: T): number { return this.#items.lowerBound(value, this.comparator); }
    /** How many elements are at most the probe. */
    public countAtMost(value: T): number { return this.#items.upperBound(value, this.comparator); }
    /** The multiplicity of the element, or zero when absent. */
    public countOf(value: T): number { return this.countAtMost(value) - this.countLessThan(value); }
    /** A bag containing the given element; returns the receiver when it is already present. */
    public add(value: T): SortedBag<T> { return new SortedBag(this.#items.insertAt(this.countAtMost(value), value)!, this.comparator); }
    /** Insert every element of the given sequence, publishing only the final bag. */
    public addRange(values: Iterable<T>): SortedBag<T> { let result: SortedBag<T> = this; for (const value of values) result = result.add(value); return result; }
    /** A bag without that element; returns the receiver when it is absent. */
    public remove(value: T): SortedBag<T> {
        const index = this.countLessThan(value);
        return index >= this.size || this.comparator(this.#items.at(index)!, value) !== 0 ? this : new SortedBag(this.#items.removeAt(index)!, this.comparator);
    }
    /** A bag without that class, whatever its multiplicity. */
    public removeAll(value: T): SortedBag<T> {
        const start = this.countLessThan(value);
        const end = this.countAtMost(value);
        if (start === end) return this;
        const first = this.#items.splitAt(start)!;
        const second = first.right.splitAt(end - start)!;
        return new SortedBag(first.left.concat(second.right), this.comparator);
    }
    /** The given run of elements as a new bag, preserving their order. */
    public getRange(start: number, count: number): SortedBag<T> | undefined {
        if (!Number.isInteger(start) || !Number.isInteger(count) || start < 0 || count < 0 || start + count > this.size) return undefined;
        const first = this.#items.splitAt(start)!;
        return new SortedBag(first.right.splitAt(count)!.left, this.comparator);
    }
    /**
     * The elements in the inclusive value range as a new bag. An inverted range yields an empty
     * bag.
     */
    public getValueRange(low: T, high: T): SortedBag<T> {
        if (this.comparator(low, high) > 0) return SortedBag.empty(this.comparator);
        return this.getRange(this.countLessThan(low), this.countAtMost(high) - this.countLessThan(low))!;
    }
    /**
     * Removes the occurrence at `rank` through the ordinary measured-sequence primitive.
     *
     * @internal Exists so `SortedBagCursor` can delete by rank in O(log n) with path copying; the
     * private element sequence is otherwise unreachable from the cursor class. Element order is
     * already sorted, so no re-sort or rebuild is implied. Returns `undefined` for an out-of-range
     * rank, exactly as the underlying sequence does.
     */
    public cursorRemoveAt(rank: number): SortedBag<T> | undefined {
        const next = this.#items.removeAt(rank);
        return next === undefined ? undefined : new SortedBag(next, this.comparator);
    }
    /** A cursor at the given gap of the bag. */
    public cursorAt(position = 0): SortedBagCursor<T> { return new SortedBagCursor(this, position); }
    /** A cursor before the first entry not below the probe, where it would be inserted. */
    public cursorAtLowerBound(value: T): SortedBagCursor<T> { return this.cursorAt(this.countLessThan(value)); }
    /** A cursor after any entry equivalent to the probe. */
    public cursorAtUpperBound(value: T): SortedBagCursor<T> { return this.cursorAt(this.countAtMost(value)); }
    /**
     * Seek to the probe and report whether it is present. On a miss the cursor sits where the probe
     * would be inserted and remains usable.
     */
    public findCursor(value: T): OrderedCursorSearch<SortedBagCursor<T>> {
        const cursor = this.cursorAtLowerBound(value);
        const candidate = cursor.peekNext();
        return { found: candidate !== undefined && this.comparator(candidate.value, value) === 0, cursor };
    }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return this.#items.toArray(); }
    /**
     * Whether both bags share a node by identity. A representation test used to confirm a no-op
     * avoided copying, not an equality test.
     */
    public sharesStorageWith(other: SortedBag<T>): boolean { return this.#items.sharesStructureWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

/** Immutable order-statistic sorted set. */
export class SortedSet<T> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, number>;
    /** The retained comparator defining the ordering. */
    public readonly comparator: Comparator<T>;
    private constructor(items: MeasuredSequence<T, number>, comparator: Comparator<T>) { this.#items = items; this.comparator = comparator; }
    /** The empty set, retaining the supplied policy objects. */
    public static empty<T>(comparator: Comparator<T> = defaultComparator): SortedSet<T> { return new SortedSet(MeasuredSequence.empty(policy<T>()), comparator); }
    /** Build a set from the given elements. */
    public static from<T>(values: Iterable<T>, comparator: Comparator<T> = defaultComparator): SortedSet<T> {
        let result = SortedSet.empty(comparator);
        for (const value of values) result = result.add(value);
        return result;
    }
    /** Number of elements. */
    public get size(): number { return this.#items.size; }
    /** Whether the set holds no elements. */
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    /** The smallest element, or `undefined` when empty. */
    public min(): T | undefined { return this.#items.front(); }
    /** The largest element, or `undefined` when empty. */
    public max(): T | undefined { return this.#items.back(); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(rank: number): T | undefined { return this.#items.at(rank); }
    /** The ordinal rank of the element, or `-1` when absent. */
    public indexOf(value: T): number | undefined {
        const index = this.#items.lowerBound(value, this.comparator);
        return index < this.size && this.comparator(this.#items.at(index)!, value) === 0 ? index : undefined;
    }
    /** Whether the element is present. */
    public contains(value: T): boolean { return this.indexOf(value) !== undefined; }
    /** A set containing the given element; returns the receiver when it is already present. */
    public add(value: T): SortedSet<T> {
        const index = this.#items.lowerBound(value, this.comparator);
        return index < this.size && this.comparator(this.#items.at(index)!, value) === 0 ? this : new SortedSet(this.#items.insertAt(index, value)!, this.comparator);
    }
    /** The elements of both sets. */
    public union(values: Iterable<T>): SortedSet<T> { let result: SortedSet<T> = this; for (const value of values) result = result.add(value); return result; }
    /** A set without that element; returns the receiver when it is absent. */
    public remove(value: T): SortedSet<T> {
        const index = this.indexOf(value);
        return index === undefined ? this : new SortedSet(this.#items.removeAt(index)!, this.comparator);
    }
    /** The largest element at most the probe, or `undefined` when every one exceeds it. */
    public floor(value: T): T | undefined { const index = this.#items.upperBound(value, this.comparator) - 1; return this.get(index); }
    /** The smallest element at least the probe, or `undefined` when every one is below it. */
    public ceiling(value: T): T | undefined { return this.get(this.#items.lowerBound(value, this.comparator)); }
    /** The largest element strictly below the probe, or `undefined` when none is. */
    public lower(value: T): T | undefined { return this.get(this.#items.lowerBound(value, this.comparator) - 1); }
    /** The smallest element strictly above the probe, or `undefined` when none is. */
    public higher(value: T): T | undefined { return this.get(this.#items.upperBound(value, this.comparator)); }
    /** The given run of elements as a new set, preserving their order. */
    public getRange(start: number, count: number): SortedSet<T> | undefined {
        if (!Number.isInteger(start) || !Number.isInteger(count) || start < 0 || count < 0 || start + count > this.size) return undefined;
        const first = this.#items.splitAt(start)!;
        return new SortedSet(first.right.splitAt(count)!.left, this.comparator);
    }
    /**
     * The elements in the inclusive value range as a new set. An inverted range yields an empty
     * set.
     */
    public getValueRange(low: T, high: T): SortedSet<T> {
        if (this.comparator(low, high) > 0) return SortedSet.empty(this.comparator);
        const start = this.#items.lowerBound(low, this.comparator);
        return this.getRange(start, this.#items.upperBound(high, this.comparator) - start)!;
    }
    /** The elements present in both sets. */
    public intersect(values: Iterable<T>): SortedSet<T> { const other = SortedSet.from(values, this.comparator); let result = SortedSet.empty<T>(this.comparator); for (const value of this) if (other.contains(value)) result = result.add(value); return result.setEquals(this) ? this : result; }
    /** This set's elements that are absent from the other. */
    public except(values: Iterable<T>): SortedSet<T> { let result: SortedSet<T> = this; for (const value of values) result = result.remove(value); return result; }
    /** The elements present in exactly one of the two sets. */
    public symmetricExcept(values: Iterable<T>): SortedSet<T> { const other = SortedSet.from(values, this.comparator); let result: SortedSet<T> = this; for (const value of other) result = result.contains(value) ? result.remove(value) : result.add(value); return result; }
    /** Whether every element of this set also occurs in the other. */
    public isSubsetOf(values: Iterable<T>): boolean { const other = SortedSet.from(values, this.comparator); return this.size <= other.size && Array.from(this).every((value) => other.contains(value)); }
    /** Whether this set is a subset of the other and the other holds an element it lacks. */
    public isProperSubsetOf(values: Iterable<T>): boolean { const other = SortedSet.from(values, this.comparator); return this.size < other.size && this.isSubsetOf(other); }
    /** Whether every element of the other occurs in this set. */
    public isSupersetOf(values: Iterable<T>): boolean { for (const value of values) if (!this.contains(value)) return false; return true; }
    /** Whether this set is a superset of the other and holds an element the other lacks. */
    public isProperSupersetOf(values: Iterable<T>): boolean { const other = SortedSet.from(values, this.comparator); return this.size > other.size && this.isSupersetOf(other); }
    /** Whether the two sets share at least one element. */
    public overlaps(values: Iterable<T>): boolean { for (const value of values) if (this.contains(value)) return true; return false; }
    /** Whether both sets hold the same elements. */
    public setEquals(values: Iterable<T>): boolean { const other = values instanceof SortedSet && values.comparator === this.comparator ? values : SortedSet.from(values, this.comparator); return this.size === other.size && this.isSubsetOf(other); }
    /** A cursor at the given gap of the set. */
    public cursorAt(position = 0): SortedSetCursor<T> { return new SortedSetCursor(this, position); }
    /** A cursor before the first entry not below the probe, where it would be inserted. */
    public cursorAtLowerBound(value: T): SortedSetCursor<T> { return this.cursorAt(this.#items.lowerBound(value, this.comparator)); }
    /** A cursor after any entry equivalent to the probe. */
    public cursorAtUpperBound(value: T): SortedSetCursor<T> { return this.cursorAt(this.#items.upperBound(value, this.comparator)); }
    /**
     * Seek to the probe and report whether it is present. On a miss the cursor sits where the probe
     * would be inserted and remains usable.
     */
    public findCursor(value: T): OrderedCursorSearch<SortedSetCursor<T>> {
        const cursor = this.cursorAtLowerBound(value);
        const candidate = cursor.peekNext();
        return { found: candidate !== undefined && this.comparator(candidate.value, value) === 0, cursor };
    }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return this.#items.toArray(); }
    /**
     * Whether both sets share a node by identity. A representation test used to confirm a no-op
     * avoided copying, not an equality test.
     */
    public sharesStorageWith(other: SortedSet<T>): boolean { return this.#items.sharesStructureWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

/** Immutable order-statistic sorted map preserving comparator-equivalent representatives. */
export class SortedMap<K, V> implements Iterable<SortedMapEntry<K, V>> {
    readonly #entries: MeasuredSequence<SortedMapEntry<K, V>, number>;
    /** The retained comparator defining the ordering. */
    public readonly comparator: Comparator<K>;
    private constructor(entries: MeasuredSequence<SortedMapEntry<K, V>, number>, comparator: Comparator<K>) { this.#entries = entries; this.comparator = comparator; }
    /** The empty map, retaining the supplied policy objects. */
    public static empty<K, V>(comparator: Comparator<K> = defaultComparator): SortedMap<K, V> { return new SortedMap(MeasuredSequence.empty(policy<SortedMapEntry<K, V>>()), comparator); }
    /** Build a map from the given entries. */
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
    /** Number of entries. */
    public get size(): number { return this.#entries.size; }
    /** Whether the map holds no entries. */
    public get isEmpty(): boolean { return this.#entries.isEmpty; }
    #entryComparator = (entry: SortedMapEntry<K, V>, probe: SortedMapEntry<K, V>): number => this.comparator(entry.key, probe.key);
    #probe(key: K): SortedMapEntry<K, V> { return { key, value: undefined as V }; }
    /** The ordinal rank of the key, or `-1` when absent. */
    public indexOfKey(key: K): number | undefined {
        const index = this.#entries.lowerBound(this.#probe(key), this.#entryComparator);
        return index < this.size && this.comparator(this.#entries.at(index)!.key, key) === 0 ? index : undefined;
    }
    /** Whether the key is present. */
    public containsKey(key: K): boolean { return this.indexOfKey(key) !== undefined; }
    /** The value stored for the key, or `undefined` when absent. */
    public get(key: K): V | undefined { const index = this.indexOfKey(key); return index === undefined ? undefined : this.#entries.at(index)!.value; }
    /** The entry at the given ordinal rank, or `undefined` when out of range. */
    public entryAt(rank: number): SortedMapEntry<K, V> | undefined { return this.#entries.at(rank); }
    /** The entry with the smallest key, or `undefined` when empty. */
    public minEntry(): SortedMapEntry<K, V> | undefined { return this.#entries.front(); }
    /** The entry with the largest key, or `undefined` when empty. */
    public maxEntry(): SortedMapEntry<K, V> | undefined { return this.#entries.back(); }
    /** A map with the key bound to the value, adding or replacing as needed. */
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
    /** A map with the entry inserted at the given index. */
    public insert(key: K, value: V): SortedMap<K, V> { const result = this.tryInsert(key, value); if (!result.added) throw new SortedDuplicateKeyError(); return result.value; }
    /**
     * Insert, reporting whether an entry was added. On a duplicate the returned cursor focuses the
     * retained entry.
     */
    public tryInsert(key: K, value: V): SortedAddResult<SortedMap<K, V>> { return this.containsKey(key) ? { value: this, added: false } : { value: this.setItem(key, value), added: true }; }
    /** A map without that entry; returns the receiver when it is absent. */
    public remove(key: K): SortedMap<K, V> { return this.tryRemove(key)?.map ?? this; }
    /** Remove the entry and report what was removed, or `undefined` when absent. */
    public tryRemove(key: K): SortedMapRemoveResult<K, V> | undefined { const index = this.indexOfKey(key); if (index === undefined) return undefined; const value = this.#entries.at(index)!.value; return { map: new SortedMap(this.#entries.removeAt(index)!, this.comparator), value }; }
    #lowerBound(key: K): number { return this.#entries.lowerBound(this.#probe(key), this.#entryComparator); }
    #upperBound(key: K): number { return this.#entries.upperBound(this.#probe(key), this.#entryComparator); }
    /**
     * The entry with the largest key at most the probe, or `undefined` when every key exceeds it.
     */
    public floorEntry(key: K): SortedMapEntry<K, V> | undefined { return this.entryAt(this.#upperBound(key) - 1); }
    /**
     * The entry with the smallest key at least the probe, or `undefined` when every key is below
     * it.
     */
    public ceilingEntry(key: K): SortedMapEntry<K, V> | undefined { return this.entryAt(this.#lowerBound(key)); }
    /** The entry with the largest key strictly below the probe, or `undefined` when none is. */
    public lowerEntry(key: K): SortedMapEntry<K, V> | undefined { return this.entryAt(this.#lowerBound(key) - 1); }
    /** The entry with the smallest key strictly above the probe, or `undefined` when none is. */
    public higherEntry(key: K): SortedMapEntry<K, V> | undefined { return this.entryAt(this.#upperBound(key)); }
    /** The given run of entries as a new map, preserving their order. */
    public getRange(start: number, count: number): SortedMap<K, V> | undefined { if (!Number.isInteger(start) || !Number.isInteger(count) || start < 0 || count < 0 || start + count > this.size) return undefined; const first = this.#entries.splitAt(start)!; return new SortedMap(first.right.splitAt(count)!.left, this.comparator); }
    /**
     * The entries whose keys lie in the inclusive range as a new map. An inverted range yields an
     * empty map.
     */
    public getKeyRange(low: K, high: K): SortedMap<K, V> { if (this.comparator(low, high) > 0) return SortedMap.empty(this.comparator); const start = this.#lowerBound(low); return this.getRange(start, this.#upperBound(high) - start)!; }
    /** A cursor at the given gap of the map. */
    public cursorAt(position = 0): SortedMapCursor<K, V> { return new SortedMapCursor(this, position); }
    /** A cursor before the first entry not below the probe, where it would be inserted. */
    public cursorAtLowerBound(key: K): SortedMapCursor<K, V> { return this.cursorAt(this.#lowerBound(key)); }
    /** A cursor after any entry equivalent to the probe. */
    public cursorAtUpperBound(key: K): SortedMapCursor<K, V> { return this.cursorAt(this.#upperBound(key)); }
    /**
     * Seek to the probe and report whether it is present. On a miss the cursor sits where the probe
     * would be inserted and remains usable.
     */
    public findCursor(key: K): OrderedCursorSearch<SortedMapCursor<K, V>> {
        const cursor = this.cursorAtLowerBound(key);
        const candidate = cursor.peekNext();
        return { found: candidate !== undefined && this.comparator(candidate.value.key, key) === 0, cursor };
    }
    /** Copy the entries into an array, in order. */
    public toArray(): SortedMapEntry<K, V>[] { return this.#entries.toArray(); }
    /** Iterate the keys. */
    public keys(): K[] { return this.toArray().map((entry) => entry.key); }
    /** Iterate the values. */
    public values(): V[] { return this.toArray().map((entry) => entry.value); }
    /**
     * Whether both maps share a node by identity. A representation test used to confirm a no-op
     * avoided copying, not an equality test.
     */
    public sharesStorageWith(other: SortedMap<K, V>): boolean { return this.#entries.sharesStructureWith(other.#entries); }
    public [Symbol.iterator](): IterableIterator<SortedMapEntry<K, V>> { return this.#entries[Symbol.iterator](); }
}

/** Immutable root-plus-rank cursor over a persistent sorted bag. */
export class SortedBagCursor<T> {
    public constructor(public readonly bag: SortedBag<T>, public readonly position = 0) {
        requireCursorRank(position, bag.size, "sorted bag");
    }
    /** Number of elements. */
    public get size(): number { return this.bag.size; }
    /** Whether the gap precedes the first element. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last element. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The element immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): { readonly value: T } | undefined { const value = this.bag.get(this.position - 1); return this.isAtStart ? undefined : { value: value! }; }
    /** The element immediately after the gap, or `undefined` at the end. */
    public peekNext(): { readonly value: T } | undefined { const value = this.bag.get(this.position); return this.isAtEnd ? undefined : { value: value! }; }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): SortedBagCursor<T> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new SortedBagCursor(this.bag, this.position - 1); }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): SortedBagCursor<T> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new SortedBagCursor(this.bag, this.position + 1); }
    /** A cursor at the given rank within the same bag version. */
    public seekRank(position: number): SortedBagCursor<T> { return position === this.position ? this : new SortedBagCursor(this.bag, position); }
    /** A bag containing the given element; returns the receiver when it is already present. */
    public add(value: T): SortedBagCursor<T> { const position = this.bag.countAtMost(value); return new SortedBagCursor(this.bag.add(value), position + 1); }
    /** Remove the element before the gap and return a cursor in its place. */
    public deletePrevious(): SortedBagCursor<T> { if (this.isAtStart) throw new RangeError("No occurrence precedes the cursor."); return this.deleteAt(this.position - 1); }
    /** Remove the element after the gap and return a cursor in its place. */
    public deleteNext(): SortedBagCursor<T> { if (this.isAtEnd) throw new RangeError("No occurrence follows the cursor."); return this.deleteAt(this.position); }
    private deleteAt(rank: number): SortedBagCursor<T> {
        return new SortedBagCursor(this.bag.cursorRemoveAt(rank)!, rank < this.position ? this.position - 1 : this.position);
    }
    /** The bag version this cursor is positioned in. */
    public snapshot(): SortedBag<T> { return this.bag; }
}

/** Immutable root-plus-rank cursor over a persistent sorted set. */
export class SortedSetCursor<T> {
    public constructor(public readonly set: SortedSet<T>, public readonly position = 0) { requireCursorRank(position, set.size, "sorted set"); }
    /** Number of elements. */
    public get size(): number { return this.set.size; }
    /** Whether the gap precedes the first element. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last element. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The element immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): { readonly value: T } | undefined { return this.isAtStart ? undefined : { value: this.set.get(this.position - 1)! }; }
    /** The element immediately after the gap, or `undefined` at the end. */
    public peekNext(): { readonly value: T } | undefined { return this.isAtEnd ? undefined : { value: this.set.get(this.position)! }; }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): SortedSetCursor<T> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new SortedSetCursor(this.set, this.position - 1); }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): SortedSetCursor<T> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new SortedSetCursor(this.set, this.position + 1); }
    /** A cursor at the given rank within the same set version. */
    public seekRank(position: number): SortedSetCursor<T> { return position === this.position ? this : new SortedSetCursor(this.set, position); }
    /** A set containing the given element; returns the receiver when it is already present. */
    public add(value: T): SortedSetCursor<T> { const location = this.set.cursorAtLowerBound(value); return new SortedSetCursor(this.set.add(value), location.position + 1); }
    /** Remove the element before the gap and return a cursor in its place. */
    public deletePrevious(): SortedSetCursor<T> { const item = this.peekPrevious(); if (item === undefined) throw new RangeError("No item precedes the cursor."); return new SortedSetCursor(this.set.remove(item.value), this.position - 1); }
    /** Remove the element after the gap and return a cursor in its place. */
    public deleteNext(): SortedSetCursor<T> { const item = this.peekNext(); if (item === undefined) throw new RangeError("No item follows the cursor."); return new SortedSetCursor(this.set.remove(item.value), this.position); }
    /** The set version this cursor is positioned in. */
    public snapshot(): SortedSet<T> { return this.set; }
}

/** Immutable key-order root-plus-rank cursor over a persistent sorted map. */
export class SortedMapCursor<K, V> {
    public constructor(public readonly map: SortedMap<K, V>, public readonly position = 0) { requireCursorRank(position, map.size, "sorted map"); }
    /** Number of entries. */
    public get size(): number { return this.map.size; }
    /** Whether the gap precedes the first entry. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last entry. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The entry immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): { readonly value: SortedMapEntry<K, V> } | undefined { return this.isAtStart ? undefined : { value: this.map.entryAt(this.position - 1)! }; }
    /** The entry immediately after the gap, or `undefined` at the end. */
    public peekNext(): { readonly value: SortedMapEntry<K, V> } | undefined { return this.isAtEnd ? undefined : { value: this.map.entryAt(this.position)! }; }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): SortedMapCursor<K, V> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new SortedMapCursor(this.map, this.position - 1); }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): SortedMapCursor<K, V> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new SortedMapCursor(this.map, this.position + 1); }
    /** A cursor at the given rank within the same map version. */
    public seekRank(position: number): SortedMapCursor<K, V> { return position === this.position ? this : new SortedMapCursor(this.map, position); }
    /** Insert at the gap and return a cursor positioned after the new entry. */
    public insert(key: K, value: V): SortedMapCursor<K, V> { const location = this.map.cursorAtLowerBound(key); return new SortedMapCursor(this.map.insert(key, value), location.position + 1); }
    /**
     * Insert, reporting whether an entry was added. On a duplicate the returned cursor focuses the
     * retained entry.
     */
    public tryInsert(key: K, value: V): { readonly added: boolean; readonly cursor: SortedMapCursor<K, V> } { const location = this.map.cursorAtLowerBound(key); const result = this.map.tryInsert(key, value); return { added: result.added, cursor: result.added ? new SortedMapCursor(result.value, location.position + 1) : location }; }
    /** A map with the key bound to the value, adding or replacing as needed. */
    public setItem(key: K, value: V): SortedMapCursor<K, V> { const location = this.map.findCursor(key); return new SortedMapCursor(this.map.setItem(key, value), location.found ? location.cursor.position : location.cursor.position + 1); }
    /** Replace the value of the entry after the gap, keeping its key and position. */
    public setNextValue(value: V): SortedMapCursor<K, V> { const entry = this.peekNext(); if (entry === undefined) throw new RangeError("No entry follows the cursor."); return new SortedMapCursor(this.map.setItem(entry.value.key, value), this.position); }
    /** Remove the entry before the gap and return a cursor in its place. */
    public deletePrevious(): SortedMapCursor<K, V> { const entry = this.peekPrevious(); if (entry === undefined) throw new RangeError("No entry precedes the cursor."); return new SortedMapCursor(this.map.remove(entry.value.key), this.position - 1); }
    /** Remove the entry after the gap and return a cursor in its place. */
    public deleteNext(): SortedMapCursor<K, V> { const entry = this.peekNext(); if (entry === undefined) throw new RangeError("No entry follows the cursor."); return new SortedMapCursor(this.map.remove(entry.value.key), this.position); }
    /** The map version this cursor is positioned in. */
    public snapshot(): SortedMap<K, V> { return this.map; }
}

function requireCursorRank(position: number, size: number, family: string): void {
    if (!Number.isInteger(position) || position < 0 || position > size) throw new RangeError(`Cursor position is outside the ${family}.`);
}

/** Mutable bulk builder that publishes isolated immutable sorted-set snapshots. */
export class SortedSetBuilder<T> {
    #value: SortedSet<T>;
    public constructor(comparator: Comparator<T> = defaultComparator) { this.#value = SortedSet.empty(comparator); }
    /** Number of elements. */
    public get size(): number { return this.#value.size; }
    /** A set containing the given element; returns the receiver when it is already present. */
    public add(value: T): this { this.#value = this.#value.add(value); return this; }
    /** A set without that element; returns the receiver when it is absent. */
    public remove(value: T): boolean { const next = this.#value.remove(value); const changed = next !== this.#value; this.#value = next; return changed; }
    /** An empty set retaining the same policies; returns the receiver when already empty. */
    public clear(): void { this.#value = SortedSet.empty(this.#value.comparator); }
    /** Freeze the accumulated elements into a persistent set. */
    public toImmutable(): SortedSet<T> { return this.#value; }
}

/** Mutable bulk builder that publishes isolated immutable sorted-map snapshots. */
export class SortedMapBuilder<K, V> {
    #value: SortedMap<K, V>;
    public constructor(comparator: Comparator<K> = defaultComparator) { this.#value = SortedMap.empty(comparator); }
    /** Number of entries. */
    public get size(): number { return this.#value.size; }
    /** A map with the key bound to the value, adding or replacing as needed. */
    public set(key: K, value: V): this { this.#value = this.#value.setItem(key, value); return this; }
    /** A map without that entry; returns the receiver when it is absent. */
    public remove(key: K): boolean { const next = this.#value.remove(key); const changed = next !== this.#value; this.#value = next; return changed; }
    /** An empty map retaining the same policies; returns the receiver when already empty. */
    public clear(): void { this.#value = SortedMap.empty(this.#value.comparator); }
    /** Freeze the accumulated entries into a persistent map. */
    public toImmutable(): SortedMap<K, V> { return this.#value; }
}
