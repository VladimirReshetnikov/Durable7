import { PersistentDeque } from "../finger-tree/core.js";
import { defaultComparator, type Comparator } from "../finger-tree/ordering.js";
import { defaultHashPolicy, type HashPolicy } from "../hamt/hash-policy.js";
import { PersistentHashMap } from "../hamt/persistent-hamt.js";

const stampGap = 1n << 32n;
interface Slot<V> { readonly stamp: bigint; readonly value: V }
interface AssociationEntry<K, V> { readonly stamp: bigint; readonly key: K; readonly value: V }
export interface AssociationRemoveResult<K, V> { readonly association: PersistentAssociation<K, V>; readonly value: V }

/** Persistent insertion-ordered map with Tungsten Association positioning rules. */
export class PersistentAssociation<K, V> implements Iterable<readonly [K, V]> {
    readonly #entries: PersistentDeque<AssociationEntry<K, V>>;
    readonly #index: PersistentHashMap<K, Slot<V>>;
    private constructor(entries: PersistentDeque<AssociationEntry<K, V>>, index: PersistentHashMap<K, Slot<V>>) { this.#entries = entries; this.#index = index; }
    public static empty<K, V>(policy: HashPolicy<K> = defaultHashPolicy<K>()): PersistentAssociation<K, V> { return new PersistentAssociation(PersistentDeque.empty(), PersistentHashMap.empty(policy)); }
    public static from<K, V>(pairs: Iterable<readonly [K, V]>, policy: HashPolicy<K> = defaultHashPolicy<K>()): PersistentAssociation<K, V> { return this.empty<K, V>(policy).setItems(pairs); }
    public get size(): number { return this.#entries.size; }
    public get isEmpty(): boolean { return this.#entries.isEmpty; }
    public get policy(): HashPolicy<K> { return this.#index.policy; }
    public containsKey(key: K): boolean { return this.#index.containsKey(key); }
    public get(key: K): V | undefined { return this.#index.get(key)?.value; }
    public getStoredKey(key: K): K | undefined { return this.#index.getEntry(key)?.key; }
    #pair(entry: AssociationEntry<K, V> | undefined): readonly [K, V] | undefined { return entry === undefined ? undefined : [entry.key, entry.value]; }
    public first(): readonly [K, V] | undefined { return this.#pair(this.#entries.front()); }
    public last(): readonly [K, V] | undefined { return this.#pair(this.#entries.back()); }
    public getAt(index: number): readonly [K, V] | undefined { return this.#pair(this.#entries.get(index)); }
    #indexOfStamp(stamp: bigint): number { let low = 0; let high = this.size; while (low < high) { const middle = (low + high) >>> 1; if (this.#entries.get(middle)!.stamp < stamp) low = middle + 1; else high = middle; } if (low >= this.size || this.#entries.get(low)!.stamp !== stamp) throw new Error("Association stamp is absent from order."); return low; }
    public indexOfKey(key: K): number { const slot = this.#index.getEntry(key)?.value; return slot === undefined ? -1 : this.#indexOfStamp(slot.stamp); }
    public setItem(key: K, value: V): PersistentAssociation<K, V> {
        const found = this.#index.getEntry(key); if (found === undefined) return this.#insertNew(this.#entries, this.#index, this.size, key, value);
        const slot = found.value; if (Object.is(slot.value, value)) return this;
        const position = this.#indexOfStamp(slot.stamp); const stored = this.#entries.get(position)!;
        return new PersistentAssociation(this.#entries.setItem(position, { stamp: slot.stamp, key: stored.key, value })!, this.#index.put(key, { stamp: slot.stamp, value }));
    }
    public setItems(pairs: Iterable<readonly [K, V]>): PersistentAssociation<K, V> { let result: PersistentAssociation<K, V> = this; for (const [key, value] of pairs) result = result.setItem(key, value); return result; }
    public join(other: PersistentAssociation<K, V>): PersistentAssociation<K, V> { if (this.isEmpty && this.policy === other.policy) return other; return this.setItems(other); }
    public append(key: K, value: V): PersistentAssociation<K, V> {
        const found = this.#index.getEntry(key); if (found === undefined) return this.#insertNew(this.#entries, this.#index, this.size, key, value);
        const position = this.#indexOfStamp(found.value.stamp); if (position === this.size - 1 && Object.is(found.value.value, value)) return this;
        const trimmed = this.#entries.removeAt(position)!; return this.#insertNew(trimmed, this.#index.remove(key), trimmed.size, key, value);
    }
    public prepend(key: K, value: V): PersistentAssociation<K, V> {
        const found = this.#index.getEntry(key);
        if (found === undefined) return this.#insertNew(this.#entries, this.#index, 0, key, value);
        const position = this.#indexOfStamp(found.value.stamp); if (position === 0 && Object.is(found.value.value, value)) return this;
        const trimmed = this.#entries.removeAt(position)!; return this.#insertNew(trimmed, this.#index.remove(key), 0, key, value);
    }
    public insert(position: number, key: K, value: V): PersistentAssociation<K, V> | undefined {
        if (!Number.isInteger(position) || position < 0 || position > this.size) return undefined;
        let entries = this.#entries; let index = this.#index; let target = position; const found = index.getEntry(key);
        if (found !== undefined) { const old = this.#indexOfStamp(found.value.stamp); entries = entries.removeAt(old)!; index = index.remove(key); if (old < target) target--; }
        return this.#insertNew(entries, index, target, key, value);
    }
    public remove(key: K): PersistentAssociation<K, V> { return this.tryRemove(key)?.association ?? this; }
    public tryRemove(key: K): AssociationRemoveResult<K, V> | undefined { const removed = this.#index.tryRemove(key); if (removed === undefined) return undefined; const position = this.#indexOfStamp(removed.value.stamp); return { association: new PersistentAssociation(this.#entries.removeAt(position)!, removed.map), value: removed.value.value }; }
    public removeRange(keys: Iterable<K>): PersistentAssociation<K, V> { let result: PersistentAssociation<K, V> = this; for (const key of keys) result = result.remove(key); return result; }
    public keyTake(keys: Iterable<K>): PersistentAssociation<K, V> { let result = PersistentAssociation.empty<K, V>(this.policy); for (const key of keys) { const found = this.#index.getEntry(key); if (found !== undefined && !result.containsKey(key)) result = result.append(found.key, found.value.value); } return result; }
    public removeAt(position: number): PersistentAssociation<K, V> | undefined { const entry = this.#entries.get(position); return entry === undefined ? undefined : new PersistentAssociation(this.#entries.removeAt(position)!, this.#index.remove(entry.key)); }
    public removeFirst(): PersistentAssociation<K, V> | undefined { return this.removeAt(0); }
    public removeLast(): PersistentAssociation<K, V> | undefined { return this.removeAt(this.size - 1); }
    public getRange(position: number, count: number): PersistentAssociation<K, V> | undefined {
        const split = this.#entries.splitRange(position, count); if (split === undefined) return undefined; if (count === this.size) return this;
        let index = PersistentHashMap.empty<K, Slot<V>>(this.policy); for (const entry of split.range) index = index.put(entry.key, { stamp: entry.stamp, value: entry.value });
        return new PersistentAssociation(split.range, index);
    }
    public take(count: number): PersistentAssociation<K, V> | undefined { return this.getRange(0, count); }
    public drop(count: number): PersistentAssociation<K, V> | undefined { return !Number.isInteger(count) || count < 0 || count > this.size ? undefined : this.getRange(count, this.size - count); }
    public reverse(): PersistentAssociation<K, V> { return this.size <= 1 ? this : this.#rebuild(this.#entries.toArray().reverse()); }
    public keySort(comparator: Comparator<K> = defaultComparator): PersistentAssociation<K, V> { return this.size <= 1 ? this : this.#rebuild(this.#entries.toArray().sort((left, right) => comparator(left.key, right.key) || Number(left.stamp - right.stamp))); }
    public sort(comparator: Comparator<V> = defaultComparator): PersistentAssociation<K, V> { return this.size <= 1 ? this : this.#rebuild(this.#entries.toArray().sort((left, right) => comparator(left.value, right.value) || Number(left.stamp - right.stamp))); }
    public keys(): K[] { return this.#entries.toArray().map((entry) => entry.key); }
    public values(): V[] { return this.#entries.toArray().map((entry) => entry.value); }
    public toArray(): Array<readonly [K, V]> { return this.#entries.toArray().map((entry) => [entry.key, entry.value]); }
    #pickStamp(entries: PersistentDeque<AssociationEntry<K, V>>, position: number): bigint | undefined { if (entries.isEmpty) return 0n; if (position === 0) return entries.front()!.stamp - stampGap; if (position === entries.size) return entries.back()!.stamp + stampGap; const left = entries.get(position - 1)!.stamp; const right = entries.get(position)!.stamp; return right - left <= 1n ? undefined : left + (right - left) / 2n; }
    #insertNew(entries: PersistentDeque<AssociationEntry<K, V>>, index: PersistentHashMap<K, Slot<V>>, position: number, key: K, value: V): PersistentAssociation<K, V> { const stamp = this.#pickStamp(entries, position); if (stamp === undefined) { const ordered = entries.toArray(); ordered.splice(position, 0, { stamp: 0n, key, value }); return this.#rebuild(ordered); } const entry = { stamp, key, value }; const nextEntries = position === 0 ? entries.prepend(entry) : position === entries.size ? entries.append(entry) : entries.insertAt(position, entry)!; return new PersistentAssociation(nextEntries, index.put(key, { stamp, value })); }
    #rebuild(ordered: readonly AssociationEntry<K, V>[]): PersistentAssociation<K, V> { let index = PersistentHashMap.empty<K, Slot<V>>(this.policy); const entries = ordered.map((entry, position) => { const stamp = BigInt(position) * stampGap; index = index.put(entry.key, { stamp, value: entry.value }); return { stamp, key: entry.key, value: entry.value }; }); return new PersistentAssociation(PersistentDeque.from(entries), index); }
    public *[Symbol.iterator](): IterableIterator<readonly [K, V]> { for (const entry of this.#entries) yield [entry.key, entry.value]; }
}
