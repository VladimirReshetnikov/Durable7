/**
 * Persistent multiset with explicit per-class multiplicities.
 *
 * Each equivalence class is stored once with a positive count, so memory follows the number of
 * distinct classes and adding many copies is constant work. Counting domains are checked rather
 * than allowed to wrap.
 */
import { attachBulkBuilderCombiner } from "./bulk-builder-internals.js";
import { defaultHashPolicy, type HashPolicy } from "./hash-policy.js";
import { PersistentHashMap, type HamtEntry } from "./persistent-hamt.js";

const maximumMultiplicity = 0x7fff_ffff;
const maximumArrayLength = 0xffff_ffffn;

/** Presence-discriminated stored-representative lookup result. */
export type HashBagLookup<T> =
    | { readonly found: true; readonly value: T }
    | { readonly found: false; readonly value: T };

function validateCopyCount(count: number): void {
    if (!Number.isInteger(count) || count < 0 || count > maximumMultiplicity) {
        throw new RangeError(`count must be an integer from 0 through ${String(maximumMultiplicity)}.`);
    }
}

function checkedMultiplicityAdd(left: number, right: number): number {
    const result = left + right;
    if (!Number.isSafeInteger(result) || result > maximumMultiplicity) {
        throw new RangeError("The per-class bag multiplicity exceeds 2^31 - 1.");
    }
    return result;
}

/** Immutable unordered multiset backed by the persistent CHAMP map. */
export class PersistentHashBag<T> implements Iterable<T> {
    static readonly #sharedDefaultEmpty = new PersistentHashBag<unknown>(
        PersistentHashMap.empty<unknown, number>(),
        0n,
    );

    readonly #counts: PersistentHashMap<T, number>;
    /**
     * Total number of occurrences, summed across classes, in a domain wide enough that summing in-
     * range classes cannot overflow.
     */
    public readonly totalCount: bigint;

    private constructor(counts: PersistentHashMap<T, number>, totalCount: bigint) {
        this.#counts = counts;
        this.totalCount = totalCount;
    }

    /** Returns an empty bag retaining the exact supplied policy object. */
    public static empty<T>(policy: HashPolicy<T> = defaultHashPolicy<T>()): PersistentHashBag<T> {
        if (policy === defaultHashPolicy<T>()) {
            return PersistentHashBag.#sharedDefaultEmpty as PersistentHashBag<T>;
        }
        return new PersistentHashBag(PersistentHashMap.empty<T, number>(policy), 0n);
    }

    /** Aggregates occurrences in input order and retains each first equivalent representative. */
    public static from<T>(
        items: Iterable<T>,
        policy: HashPolicy<T> = defaultHashPolicy<T>(),
    ): PersistentHashBag<T> {
        if (items === null || items === undefined) throw new TypeError("items must be iterable.");
        const builder = PersistentHashMap.createBulkBuilder<T, number>(policy);
        attachBulkBuilderCombiner(builder, checkedMultiplicityAdd);
        let totalCount = 0n;
        for (const item of items) {
            builder.setItem(item, 1);
            totalCount++;
        }
        return PersistentHashBag.wrap(builder.toImmutable(), totalCount);
    }

    /** Number of distinct equivalence classes, ignoring multiplicities. */
    public get distinctCount(): number { return this.#counts.size; }
    /** Whether the bag holds no occurrences. */
    public get isEmpty(): boolean { return this.#counts.isEmpty; }
    /** The retained policy defining equivalence. */
    public get policy(): HashPolicy<T> { return this.#counts.policy; }

    /** Whether the occurrence is present. */
    public contains(item: T): boolean { return this.#counts.containsKey(item); }
    /** The multiplicity of the occurrence, or zero when absent. */
    public countOf(item: T): number { return this.#counts.getEntry(item)?.value ?? 0; }

    /** The stored value representative, reported presence-safely. */
    public tryGetValue(equalValue: T): HashBagLookup<T> {
        const entry = this.#counts.getEntry(equalValue);
        return entry === undefined
            ? { found: false, value: equalValue }
            : { found: true, value: entry.key };
    }

    /** A bag containing the given occurrence; returns the receiver when it is already present. */
    public add(item: T): PersistentHashBag<T> { return this.addCopies(item, 1); }

    /**
     * A bag with the given number of additional occurrences; a count of zero returns the receiver.
     */
    public addCopies(item: T, count: number): PersistentHashBag<T> {
        validateCopyCount(count);
        if (count === 0) return this;
        const totalCount = this.totalCount + BigInt(count);
        const result = this.#counts.addOrUpdate(
            item,
            () => count,
            (_key, existingCount) => checkedMultiplicityAdd(existingCount, count),
        );
        return this.withCounts(result.map, totalCount);
    }

    /** A bag without that occurrence; returns the receiver when it is absent. */
    public remove(item: T): PersistentHashBag<T> { return this.removeCopies(item, 1); }

    /**
     * A bag with the given number of occurrences removed; removing at least the current
     * multiplicity drops the class entirely.
     */
    public removeCopies(item: T, count: number): PersistentHashBag<T> {
        validateCopyCount(count);
        if (count === 0) return this;
        const existing = this.#counts.getEntry(item);
        if (existing === undefined) return this;
        if (count >= existing.value) {
            const removed = this.#counts.tryRemoveEntry(item);
            if (removed === undefined) throw new Error("A located bag entry could not be removed.");
            return this.withCounts(
                removed.map,
                this.totalCount - BigInt(removed.entry.value),
            );
        }
        return this.withCounts(
            this.#counts.put(item, existing.value - count),
            this.totalCount - BigInt(count),
        );
    }

    /** A bag without that class, whatever its multiplicity. */
    public removeAll(item: T): PersistentHashBag<T> {
        const removed = this.#counts.tryRemoveEntry(item);
        return removed === undefined
            ? this
            : this.withCounts(removed.map, this.totalCount - BigInt(removed.entry.value));
    }

    /** An empty bag retaining the same policies; returns the receiver when already empty. */
    public clear(): PersistentHashBag<T> { return this.withCounts(this.#counts.clear(), 0n); }

    /** Multiset union: each receiver-policy class receives the larger multiplicity. */
    public union(other: PersistentHashBag<T>): PersistentHashBag<T> {
        return this.combine(other, "union");
    }

    /** Multiset intersection: each receiver-policy class receives the smaller multiplicity. */
    public intersect(other: PersistentHashBag<T>): PersistentHashBag<T> {
        return this.combine(other, "intersect");
    }

    /** Saturated multiset subtraction under the receiver's policy. */
    public except(other: PersistentHashBag<T>): PersistentHashBag<T> {
        return this.combine(other, "except");
    }

    /** Additive multiset sum with checked per-class multiplicities. */
    public sum(other: PersistentHashBag<T>): PersistentHashBag<T> {
        return this.combine(other, "sum");
    }

    /** Iterate the stored representatives once each, ignoring multiplicities. */
    public distinctItems(): IterableIterator<T> { return this.#counts.keys(); }
    /** Iterate the occurrences. */
    public entries(): IterableIterator<HamtEntry<T, number>> { return this.#counts.entries(); }

    /** Materializes expanded enumeration after checking JavaScript's array-length limit. */
    public toArray(): T[] {
        if (this.totalCount > maximumArrayLength) {
            throw new RangeError(
                `The expanded bag count ${String(this.totalCount)} exceeds JavaScript's maximum array length.`,
            );
        }
        const result = new Array<T>(Number(this.totalCount));
        let index = 0;
        for (const entry of this.#counts) {
            result.fill(entry.key, index, index + entry.value);
            index += entry.value;
        }
        return result;
    }

    public *[Symbol.iterator](): IterableIterator<T> {
        for (const entry of this.#counts) {
            for (let copy = 0; copy < entry.value; copy++) yield entry.key;
        }
    }

    private combine(
        other: PersistentHashBag<T>,
        operation: "union" | "intersect" | "except" | "sum",
    ): PersistentHashBag<T> {
        if (other === null || other === undefined) throw new TypeError("other must be a hash bag.");
        const normalized = this.policy === other.policy ? other : this.normalizeArgument(other);
        if (this === normalized) {
            if (operation === "union" || operation === "intersect") return this;
            if (operation === "except") return this.clear();
        }
        switch (operation) {
            case "union": return this.unionNormalized(normalized);
            case "intersect": return this.intersectNormalized(normalized);
            case "except": return this.exceptNormalized(normalized);
            case "sum": return this.sumNormalized(normalized);
        }
    }

    private normalizeArgument(other: PersistentHashBag<T>): PersistentHashBag<T> {
        const builder = PersistentHashMap.createBulkBuilder<T, number>(this.policy);
        attachBulkBuilderCombiner(builder, checkedMultiplicityAdd);
        let totalCount = 0n;
        for (const entry of other.#counts) {
            builder.setItem(entry.key, entry.value);
            totalCount += BigInt(entry.value);
        }
        return PersistentHashBag.wrap(builder.toImmutable(), totalCount);
    }

    private unionNormalized(other: PersistentHashBag<T>): PersistentHashBag<T> {
        if (other.isEmpty) return this;
        let counts = this.#counts;
        let totalCount = this.totalCount;
        for (const entry of other.#counts) {
            let addedCount = entry.value;
            const result = counts.addOrUpdate(
                entry.key,
                () => entry.value,
                (_key, receiverCount) => {
                    if (receiverCount >= entry.value) {
                        addedCount = 0;
                        return receiverCount;
                    }
                    addedCount = entry.value - receiverCount;
                    return entry.value;
                },
            );
            if (result.map === counts) continue;
            counts = result.map;
            totalCount += BigInt(addedCount);
        }
        return this.withCounts(counts, totalCount);
    }

    private intersectNormalized(other: PersistentHashBag<T>): PersistentHashBag<T> {
        if (this.isEmpty) return this;
        if (other.isEmpty) return this.clear();
        let counts = this.#counts;
        let totalCount = this.totalCount;
        for (const entry of this.#counts) {
            const argument = other.#counts.getEntry(entry.key);
            if (argument === undefined) {
                const removed = counts.tryRemoveEntry(entry.key);
                if (removed === undefined) throw new Error("A located bag entry could not be removed.");
                counts = removed.map;
                totalCount -= BigInt(removed.entry.value);
            } else if (argument.value < entry.value) {
                counts = counts.put(entry.key, argument.value);
                totalCount -= BigInt(entry.value - argument.value);
            }
        }
        return this.withCounts(counts, totalCount);
    }

    private exceptNormalized(other: PersistentHashBag<T>): PersistentHashBag<T> {
        if (this.isEmpty || other.isEmpty) return this;
        let counts = this.#counts;
        let totalCount = this.totalCount;
        for (const entry of this.#counts) {
            const argument = other.#counts.getEntry(entry.key);
            if (argument === undefined) continue;
            if (argument.value >= entry.value) {
                const removed = counts.tryRemoveEntry(entry.key);
                if (removed === undefined) throw new Error("A located bag entry could not be removed.");
                counts = removed.map;
                totalCount -= BigInt(removed.entry.value);
            } else {
                counts = counts.put(entry.key, entry.value - argument.value);
                totalCount -= BigInt(argument.value);
            }
        }
        return this.withCounts(counts, totalCount);
    }

    private sumNormalized(other: PersistentHashBag<T>): PersistentHashBag<T> {
        if (other.isEmpty) return this;
        let counts = this.#counts;
        let totalCount = this.totalCount;
        for (const entry of other.#counts) {
            const result = counts.addOrUpdate(
                entry.key,
                () => entry.value,
                (_key, receiverCount) => checkedMultiplicityAdd(receiverCount, entry.value),
            );
            counts = result.map;
            totalCount += BigInt(entry.value);
        }
        return this.withCounts(counts, totalCount);
    }

    private static wrap<T>(
        counts: PersistentHashMap<T, number>,
        totalCount: bigint,
    ): PersistentHashBag<T> {
        if (counts.isEmpty && counts.policy === defaultHashPolicy<T>()) {
            return PersistentHashBag.#sharedDefaultEmpty as PersistentHashBag<T>;
        }
        return new PersistentHashBag(counts, totalCount);
    }

    private withCounts(
        counts: PersistentHashMap<T, number>,
        totalCount: bigint,
    ): PersistentHashBag<T> {
        if (counts !== this.#counts) return PersistentHashBag.wrap(counts, totalCount);
        if (totalCount !== this.totalCount) {
            throw new Error("An unchanged hash-bag map cannot have a different total count.");
        }
        return this;
    }
}
