/**
 * Persistent many-to-many relation with both directions materialized.
 *
 * Keeps two mutually inverse multimaps updated and published together, so asking which left values
 * relate to a right value is as cheap as the forward question and inversion is constant time. The
 * cost is roughly two entries per logical pair.
 */
import { defaultHashPolicy, type HashPolicy } from "./hash-policy.js";
import {
    type HashMultimapEntry,
    PersistentHashMultimap,
} from "./persistent-hash-multimap.js";
import { PersistentHashSet } from "./persistent-hamt.js";

/** One stored representative pair from a many-to-many relation. */
export interface RelationEntry<L, R> {
    /** The pair's left value. */
    readonly left: L;
    /** The pair's right value. */
    readonly right: R;
}

/**
 * Immutable many-to-many relation backed by mutually inverse set-valued multimaps.
 *
 * Every equivalence class has one global stored representative, even when it occurs in several
 * adjacency groups. The cached inverse facade swaps the existing roots without rebuilding pairs.
 */
export class PersistentRelation<L, R> implements Iterable<RelationEntry<L, R>> {
    readonly #forward: PersistentHashMultimap<L, R>;
    readonly #reverse: PersistentHashMultimap<R, L>;
    #inverseView: PersistentRelation<R, L> | undefined;

    private constructor(
        forward: PersistentHashMultimap<L, R>,
        reverse: PersistentHashMultimap<R, L>,
    ) {
        this.#forward = forward;
        this.#reverse = reverse;
    }

    /** The empty relation, retaining the supplied policy objects. */
    public static empty<L, R>(
        leftPolicy: HashPolicy<L> = defaultHashPolicy<L>(),
        rightPolicy: HashPolicy<R> = defaultHashPolicy<R>(),
    ): PersistentRelation<L, R> {
        return new PersistentRelation(
            PersistentHashMultimap.empty(leftPolicy, rightPolicy),
            PersistentHashMultimap.empty(rightPolicy, leftPolicy),
        );
    }

    /** Build a relation from the given pairs. */
    public static from<L, R>(
        entries: Iterable<readonly [L, R]>,
        leftPolicy: HashPolicy<L> = defaultHashPolicy<L>(),
        rightPolicy: HashPolicy<R> = defaultHashPolicy<R>(),
    ): PersistentRelation<L, R> {
        if (entries === null || entries === undefined) throw new TypeError("entries must be iterable.");
        let result = PersistentRelation.empty<L, R>(leftPolicy, rightPolicy);
        for (const [left, right] of entries) result = result.add(left, right);
        return result;
    }

    /** Number of pairs, maintained incrementally rather than derived by summing groups. */
    public get pairCount(): number { return this.#forward.pairCount; }
    /** Number of distinct left values participating in at least one pair. */
    public get leftCount(): number { return this.#forward.keyCount; }
    /** Number of distinct right values participating in at least one pair. */
    public get rightCount(): number { return this.#reverse.keyCount; }
    /** Whether the relation holds no pairs. */
    public get isEmpty(): boolean { return this.#forward.isEmpty; }
    /** The retained hash policy defining left-value equivalence. */
    public get leftPolicy(): HashPolicy<L> { return this.#forward.keyPolicy; }
    /** The retained hash policy defining right-value equivalence. */
    public get rightPolicy(): HashPolicy<R> { return this.#reverse.keyPolicy; }

    /** Returns a cached O(1) facade over the already-built reverse and forward roots. */
    public get inverse(): PersistentRelation<R, L> {
        if (this.#inverseView !== undefined) return this.#inverseView;
        const inverse = new PersistentRelation(this.#reverse, this.#forward);
        inverse.#inverseView = this;
        this.#inverseView = inverse;
        return inverse;
    }

    /** Whether the pair is present. */
    public contains(left: L, right: R): boolean { return this.#forward.contains(left, right); }
    /** Whether the left value participates in at least one pair. */
    public containsLeft(left: L): boolean { return this.#forward.containsKey(left); }
    /**
     * Whether the right value participates in at least one pair. As cheap as the forward question,
     * because the reverse index is materialized.
     */
    public containsRight(right: R): boolean { return this.#reverse.containsKey(right); }

    /** The right values related to the given left value. */
    public getRights(left: L): PersistentHashSet<R> { return this.#forward.getValues(left); }
    /** The left values related to the given right value. */
    public getLefts(right: R): PersistentHashSet<L> { return this.#reverse.getValues(right); }

    /** Adds a pair while reusing each domain's global first representative. */
    public add(left: L, right: R): PersistentRelation<L, R> {
        const storedLeft = this.#forward.getStoredKey(left) ?? left;
        const storedRight = this.#reverse.getStoredKey(right) ?? right;
        if (this.#forward.contains(storedLeft, storedRight)) return this;

        return new PersistentRelation(
            this.#forward.add(storedLeft, storedRight),
            this.#reverse.add(storedRight, storedLeft),
        );
    }

    /** A relation without that pair; returns the receiver when it is absent. */
    public remove(left: L, right: R): PersistentRelation<L, R> {
        const storedLeft = this.#forward.getStoredKey(left);
        const storedRight = this.#reverse.getStoredKey(right);
        if (storedLeft === undefined || storedRight === undefined
            || !this.#forward.contains(storedLeft, storedRight)) return this;

        return new PersistentRelation(
            this.#forward.remove(storedLeft, storedRight),
            this.#reverse.remove(storedRight, storedLeft),
        );
    }

    /** Removes one complete left adjacency group from both indexes. */
    public removeLeft(left: L): PersistentRelation<L, R> {
        const storedLeft = this.#forward.getStoredKey(left);
        if (storedLeft === undefined) return this;
        const rights = this.#forward.getValues(storedLeft);
        let reverse = this.#reverse;
        for (const right of rights) reverse = reverse.remove(right, storedLeft);
        return new PersistentRelation(this.#forward.removeKey(storedLeft), reverse);
    }

    /** Removes one complete right adjacency group from both indexes. */
    public removeRight(right: R): PersistentRelation<L, R> {
        const storedRight = this.#reverse.getStoredKey(right);
        if (storedRight === undefined) return this;
        const lefts = this.#reverse.getValues(storedRight);
        let forward = this.#forward;
        for (const left of lefts) forward = forward.remove(left, storedRight);
        return new PersistentRelation(forward, this.#reverse.removeKey(storedRight));
    }

    /** An empty relation retaining the same policies; returns the receiver when already empty. */
    public clear(): PersistentRelation<L, R> {
        return this.isEmpty ? this : PersistentRelation.empty(this.leftPolicy, this.rightPolicy);
    }

    public *entries(): Generator<RelationEntry<L, R>, void> {
        for (const entry of this.#forward) yield { left: entry.key, right: entry.value };
    }

    public [Symbol.iterator](): IterableIterator<RelationEntry<L, R>> { return this.entries(); }

    /** Checks both adjacency indexes, counts, and global representative identity. */
    public validateStructure(): boolean {
        if (!this.#forward.validateStructure() || !this.#reverse.validateStructure()
            || this.#forward.pairCount !== this.#reverse.pairCount) return false;

        for (const entry of this.#forward) {
            const reverseRight = this.#reverse.getStoredKey(entry.value);
            const reverseLeft = this.#reverse.getValues(entry.value).get(entry.key);
            if (!Object.is(reverseRight, entry.value) || !Object.is(reverseLeft, entry.key)) return false;
        }
        for (const entry of this.#reverse as Iterable<HashMultimapEntry<R, L>>) {
            if (!this.#forward.contains(entry.value, entry.key)) return false;
        }
        return true;
    }

    /** True when both directional indexes share their corresponding roots. */
    public sharesRootsWith(other: PersistentRelation<L, R>): boolean {
        return this.#forward.sharesRootWith(other.#forward)
            && this.#reverse.sharesRootWith(other.#reverse);
    }
}
