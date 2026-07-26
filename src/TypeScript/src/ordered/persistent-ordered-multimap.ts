/**
 * Persistent insertion-ordered multimap: ordered keys, each with an ordered value group.
 *
 * Order is retained at both levels - keys in the order they first acquired a value, values in the
 * order they were first added to their key. Value groups are sets, so re-adding a pair disturbs
 * neither ordering.
 */
import { defaultHashPolicy, type HashPolicy } from "../hamt/hash-policy.js";
import { PersistentOrderedMap } from "./persistent-ordered-map.js";
import { PersistentOrderedSet } from "./persistent-ordered-set.js";

/** One key/value representative in key-grouped insertion order. */
export interface OrderedMultimapEntry<K, V> {
    /** The key. */
    readonly key: K;
    /** The value. */
    readonly value: V;
}

/** Presence-discriminated lookup of one nonempty ordered value group. */
export type OrderedMultimapLookup<V> =
    | { readonly found: true; readonly values: PersistentOrderedSet<V> }
    | { readonly found: false; readonly values: PersistentOrderedSet<V> };

/** Result of attempting to add one ordered-multimap pair. */
export interface OrderedMultimapAddResult<K, V> {
    /** Whether a new pair was published, as opposed to it already being present. */
    readonly added: boolean;
    /** The resulting collection. */
    readonly map: PersistentOrderedMultimap<K, V>;
}

/**
 * Immutable set-valued multimap preserving key-group order and order within each value group.
 *
 * Pair iteration is key-grouped; it is not one globally interleaved pair-arrival history. Key and
 * value equivalence retain independent hash policies, and empty groups are never stored.
 */
export class PersistentOrderedMultimap<K, V> implements Iterable<OrderedMultimapEntry<K, V>> {
    readonly #groups: PersistentOrderedMap<K, PersistentOrderedSet<V>>;
    readonly #valuePolicy: HashPolicy<V>;
    readonly #pairCount: number;

    private constructor(
        groups: PersistentOrderedMap<K, PersistentOrderedSet<V>>,
        valuePolicy: HashPolicy<V>,
        pairCount: number,
    ) {
        this.#groups = groups;
        this.#valuePolicy = valuePolicy;
        this.#pairCount = pairCount;
    }

    /** The empty multimap, retaining the supplied policy objects. */
    public static empty<K, V>(
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valuePolicy: HashPolicy<V> = defaultHashPolicy<V>(),
    ): PersistentOrderedMultimap<K, V> {
        return new PersistentOrderedMultimap(
            PersistentOrderedMap.empty<K, PersistentOrderedSet<V>>(keyPolicy, Object.is),
            valuePolicy,
            0,
        );
    }

    /** Build a multimap from the given pairs. */
    public static from<K, V>(
        entries: Iterable<readonly [K, V]>,
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valuePolicy: HashPolicy<V> = defaultHashPolicy<V>(),
    ): PersistentOrderedMultimap<K, V> {
        if (entries === null || entries === undefined) throw new TypeError("entries must be iterable.");
        let result = PersistentOrderedMultimap.empty<K, V>(keyPolicy, valuePolicy);
        for (const [key, value] of entries) result = result.add(key, value);
        return result;
    }

    /** Number of distinct keys. Every key has at least one value. */
    public get keyCount(): number { return this.#groups.size; }
    /** Number of pairs, maintained incrementally rather than derived by summing groups. */
    public get pairCount(): number { return this.#pairCount; }
    /** Whether the multimap holds no pairs. */
    public get isEmpty(): boolean { return this.#pairCount === 0; }
    /** The retained hash policy defining key equivalence. */
    public get keyPolicy(): HashPolicy<K> { return this.#groups.keyPolicy; }
    /** The retained hash policy defining value equivalence. */
    public get valuePolicy(): HashPolicy<V> { return this.#valuePolicy; }

    /** Whether the key is present. */
    public containsKey(key: K): boolean { return this.#groups.containsKey(key); }

    /** Whether the pair is present. */
    public contains(key: K, value: V): boolean {
        const group = this.#groups.tryGetEntry(key);
        return group.found && group.entry.value.contains(value);
    }

    /** How many distinct values the key has, or zero when absent. */
    public countValues(key: K): number {
        const group = this.#groups.tryGetEntry(key);
        return group.found ? group.entry.value.size : 0;
    }

    /** Returns the represented group or an empty ordered set retaining the value policy. */
    public getValues(key: K): PersistentOrderedSet<V> {
        const group = this.#groups.tryGetEntry(key);
        return group.found ? group.entry.value : PersistentOrderedSet.empty(this.#valuePolicy);
    }

    /**
     * The value group for that key, reported presence-safely so absence stays distinct from an
     * empty group.
     */
    public tryGetValues(key: K): OrderedMultimapLookup<V> {
        const group = this.#groups.tryGetEntry(key);
        return group.found
            ? { found: true, values: group.entry.value }
            : { found: false, values: PersistentOrderedSet.empty(this.#valuePolicy) };
    }

    /**
     * The stored key representative, which is the first inserted for its class and survives value
     * replacement.
     */
    public getStoredKey(key: K): K | undefined { return this.#groups.getStoredKey(key); }

    /** The stored value representative, reported presence-safely. */
    public tryGetValue(key: K, value: V): { readonly found: boolean; readonly value: V } {
        const group = this.#groups.tryGetEntry(key);
        return group.found ? group.entry.value.tryGetValue(value) : { found: false, value };
    }

    /** Creates an immutable flattened key-grouped pair gap cursor. */
    public getCursor(position = 0): PersistentOrderedMultimapCursor<K, V> {
        return new PersistentOrderedMultimapCursor(this, position);
    }

    /** Locates a pair; a miss returns the pair-end cursor. */
    public getCursorAtPair(key: K, value: V): {
        readonly found: boolean;
        readonly cursor: PersistentOrderedMultimapCursor<K, V>;
    } {
        const position = this.cursorIndexOf(key, value);
        return {
            found: position >= 0,
            cursor: new PersistentOrderedMultimapCursor(
                this,
                position < 0 ? this.#pairCount : position,
            ),
        };
    }

    /** Locates the first pair in a key group; a miss returns the pair-end cursor. */
    public getCursorAtGroup(key: K): {
        readonly found: boolean;
        readonly cursor: PersistentOrderedMultimapCursor<K, V>;
    } {
        let position = 0;
        for (const pair of this) {
            if (this.keyPolicy.equivalent(pair.key, key)) {
                return { found: true, cursor: new PersistentOrderedMultimapCursor(this, position) };
            }
            position += 1;
        }
        return { found: false, cursor: new PersistentOrderedMultimapCursor(this, this.#pairCount) };
    }

    /** The pair at the given rank of the flattened grouped sequence the cursors range over. */
    public cursorEntryAt(rank: number): OrderedMultimapEntry<K, V> {
        if (!Number.isSafeInteger(rank) || rank < 0 || rank >= this.#pairCount) {
            throw new RangeError("rank must identify a key-grouped pair.");
        }
        let position = 0;
        for (const pair of this) {
            if (position === rank) return pair;
            position += 1;
        }
        throw new Error("The ordered multimap pair count disagrees with its groups.");
    }

    /** A multimap containing the given pair; returns the receiver when it is already present. */
    public add(key: K, value: V): PersistentOrderedMultimap<K, V> {
        const group = this.#groups.tryGetEntry(key);
        if (group.found) {
            const values = group.entry.value.add(value);
            if (values === group.entry.value) return this;
            return new PersistentOrderedMultimap(
                this.#groups.set(group.entry.key, values),
                this.#valuePolicy,
                this.incrementPairCount(),
            );
        }

        return new PersistentOrderedMultimap(
            this.#groups.add(key, PersistentOrderedSet.empty(this.#valuePolicy).add(value)),
            this.#valuePolicy,
            this.incrementPairCount(),
        );
    }

    /** Add the entry, reporting whether it was added rather than throwing on a duplicate. */
    public tryAdd(key: K, value: V): OrderedMultimapAddResult<K, V> {
        const map = this.add(key, value);
        return { added: map !== this, map };
    }

    /** A multimap without that pair; returns the receiver when it is absent. */
    public remove(key: K, value: V): PersistentOrderedMultimap<K, V> {
        const group = this.#groups.tryGetEntry(key);
        if (!group.found) return this;
        const removed = group.entry.value.tryRemove(value);
        if (!removed.removed) return this;
        const groups = removed.set.isEmpty
            ? this.#groups.remove(group.entry.key)
            : this.#groups.set(group.entry.key, removed.set);
        return new PersistentOrderedMultimap(groups, this.#valuePolicy, this.#pairCount - 1);
    }

    /** Remove the pair holding that key from both directions. */
    public removeKey(key: K): PersistentOrderedMultimap<K, V> {
        const group = this.#groups.tryGetEntry(key);
        return !group.found
            ? this
            : new PersistentOrderedMultimap(
                this.#groups.remove(group.entry.key),
                this.#valuePolicy,
                this.#pairCount - group.entry.value.size,
            );
    }

    /** An empty multimap retaining the same policies; returns the receiver when already empty. */
    public clear(): PersistentOrderedMultimap<K, V> {
        return this.isEmpty ? this : PersistentOrderedMultimap.empty(this.keyPolicy, this.#valuePolicy);
    }

    /** Iterate the keys. */
    public keys(): IterableIterator<K> { return this.#groups.keys(); }

    public *groups(): Generator<readonly [K, PersistentOrderedSet<V>], void> {
        for (const group of this.#groups) yield [group.key, group.value];
    }

    public *[Symbol.iterator](): IterableIterator<OrderedMultimapEntry<K, V>> {
        for (const group of this.#groups) {
            for (const value of group.value) yield { key: group.key, value };
        }
    }

    /**
     * Whether both multimaps reference the same group map. A representation test, not an equality
     * test.
     */
    public sharesGroupsRootsWith(other: PersistentOrderedMultimap<K, V>): boolean {
        return this.#groups.sharesOrderStorageWith(other.#groups)
            && this.#groups.sharesMembershipRootWith(other.#groups);
    }

    /**
     * Walk the whole multimap and check its invariants, throwing on the first violation. A
     * defensive audit, not part of normal use.
     */
    public validateStructure(): { readonly keyCount: number; readonly pairCount: number } {
        this.#groups.validateStructure();
        let pairCount = 0;
        for (const group of this.#groups) {
            if (group.value.isEmpty || group.value.policy !== this.#valuePolicy) {
                throw new Error("An ordered multimap stores an empty group or the wrong value policy.");
            }
            group.value.validateStructure();
            pairCount += group.value.size;
            if (!Number.isSafeInteger(pairCount)) throw new Error("The ordered multimap pair count is not exact.");
        }
        if (pairCount !== this.#pairCount) throw new Error("The ordered multimap pair count disagrees with its groups.");
        return { keyCount: this.keyCount, pairCount };
    }

    private incrementPairCount(): number {
        if (this.#pairCount === Number.MAX_SAFE_INTEGER) {
            throw new RangeError("The operation would exceed the exact ordered-multimap pair-count limit.");
        }
        return this.#pairCount + 1;
    }

    private cursorIndexOf(key: K, value: V): number {
        let position = 0;
        for (const pair of this) {
            if (this.keyPolicy.equivalent(pair.key, key)
                && this.#valuePolicy.equivalent(pair.value, value)) {
                return position;
            }
            position += 1;
        }
        return -1;
    }
}

/** Immutable root-plus-pair-rank gap cursor over grouped ordered-multimap enumeration. */
export class PersistentOrderedMultimapCursor<K, V> {
    public constructor(
        public readonly snapshot: PersistentOrderedMultimap<K, V>,
        public readonly position: number,
    ) {
        if (!Number.isSafeInteger(position) || position < 0 || position > snapshot.pairCount) {
            throw new RangeError("position must be an exact integer from zero through pairCount.");
        }
    }

    /** Number of pairs, maintained incrementally rather than derived by summing groups. */
    public get pairCount(): number { return this.snapshot.pairCount; }
    /** Whether the gap precedes the first pair. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last pair. */
    public get isAtEnd(): boolean { return this.position === this.pairCount; }

    /** The pair before the gap, reported presence-safely. */
    public tryPeekPrevious(): OrderedMultimapEntry<K, V> | undefined {
        return this.isAtStart ? undefined : this.snapshot.cursorEntryAt(this.position - 1);
    }

    /** The pair after the gap, reported presence-safely. */
    public tryPeekNext(): OrderedMultimapEntry<K, V> | undefined {
        return this.isAtEnd ? undefined : this.snapshot.cursorEntryAt(this.position);
    }

    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): PersistentOrderedMultimapCursor<K, V> {
        if (this.isAtStart) throw new RangeError("The ordered-multimap cursor is already at the start.");
        return new PersistentOrderedMultimapCursor(this.snapshot, this.position - 1);
    }

    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): PersistentOrderedMultimapCursor<K, V> {
        if (this.isAtEnd) throw new RangeError("The ordered-multimap cursor is already at the end.");
        return new PersistentOrderedMultimapCursor(this.snapshot, this.position + 1);
    }

    /** A cursor at the given position within the same multimap version. */
    public seek(position: number): PersistentOrderedMultimapCursor<K, V> {
        return position === this.position
            ? this
            : new PersistentOrderedMultimapCursor(this.snapshot, position);
    }

    /**
     * Adds under grouped semantics and returns the inserted pair's following gap.
     *
     * The gap is derived from the key group's end using only the key policy rather than by re-scanning
     * for the accepted pair by value equality. A value that is not reflexive under the value policy,
     * such as `NaN` under a bare `===` policy, is one the collection accepts but a content re-scan can
     * never find again, so re-scanning would throw on a pair the collection just stored.
     */
    public add(key: K, value: V): PersistentOrderedMultimapCursor<K, V> {
        const snapshot = this.snapshot.add(key, value);
        if (snapshot === this.snapshot) return this;
        return new PersistentOrderedMultimapCursor(snapshot, groupEndPosition(snapshot, key));
    }

    /** Add the entry, reporting whether it was added rather than throwing on a duplicate. */
    public tryAdd(key: K, value: V): {
        readonly added: boolean;
        readonly cursor: PersistentOrderedMultimapCursor<K, V>;
    } {
        const cursor = this.add(key, value);
        return { added: cursor !== this, cursor };
    }

    /**
     * Deletes the pair immediately before the gap. Removal locates the pair by content and is a no-op
     * when the stored value is not reflexive under the value policy (a `NaN`, for instance); the pair
     * count validates the removal, so a no-op returns this receiver rather than a false success that
     * removed nothing.
     */
    public deletePrevious(): PersistentOrderedMultimapCursor<K, V> {
        const pair = this.tryPeekPrevious();
        if (pair === undefined) throw new RangeError("The ordered-multimap cursor has no previous pair.");
        const snapshot = this.snapshot.remove(pair.key, pair.value);
        if (snapshot.pairCount === this.snapshot.pairCount) return this;
        return new PersistentOrderedMultimapCursor(snapshot, this.position - 1);
    }

    /**
     * Deletes the pair immediately after the gap. Removal locates the pair by content and is a no-op
     * when the stored value is not reflexive under the value policy (a `NaN`, for instance); the pair
     * count validates the removal, so a no-op returns this receiver rather than a false success that
     * removed nothing.
     */
    public deleteNext(): PersistentOrderedMultimapCursor<K, V> {
        const pair = this.tryPeekNext();
        if (pair === undefined) throw new RangeError("The ordered-multimap cursor has no next pair.");
        const snapshot = this.snapshot.remove(pair.key, pair.value);
        if (snapshot.pairCount === this.snapshot.pairCount) return this;
        return new PersistentOrderedMultimapCursor(snapshot, this.position);
    }
}

/**
 * Pair rank of the gap immediately after the last pair of an equivalent key group, or the pair-end
 * gap when the key is absent. Key groups are contiguous in the flattened enumeration, so this walks
 * the leading pairs once and consults only the key policy, never the value policy.
 */
function groupEndPosition<K, V>(snapshot: PersistentOrderedMultimap<K, V>, key: K): number {
    const keyPolicy = snapshot.keyPolicy;
    let position = 0;
    let groupEnd = 0;
    let seenGroup = false;
    for (const pair of snapshot) {
        if (keyPolicy.equivalent(pair.key, key)) {
            position += 1;
            seenGroup = true;
            groupEnd = position;
        } else if (seenGroup) {
            return groupEnd;
        } else {
            position += 1;
        }
    }
    return seenGroup ? groupEnd : position;
}
