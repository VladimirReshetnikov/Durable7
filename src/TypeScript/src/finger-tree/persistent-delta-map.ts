/**
 * Checkpoint-differential persistent sorted map with an exact, coalesced change index.
 *
 * A version holds three immutable roots: the checkpoint state `B`, the current state `S`, and an
 * ordered change index `D` carrying exactly one `(before, after)` record for every key class on
 * which `B` and `S` differ. It answers one narrow question cheaply - which classes differ from the
 * last checkpoint, and what were their checkpoint and current values - which a plain persistent map
 * cannot: a root handle makes versions cheap but does not name the changed keys, and a write log
 * names operations while leaving repeated writes, cancellations, and ordering to be resolved later.
 * The first effective write to a class captures its checkpoint-relative `before`; later writes
 * replace only `after`; returning a class to its checkpoint state deletes its record outright. When
 * `D` empties, the current root snaps back to the *exact* checkpoint root, so a wholly cancelled
 * epoch is storage-clean as well as extensionally clean.
 *
 * Bounds. Let `N = max(2, |dom(B) union dom(S)|)` counting comparator-equivalence classes rather
 * than key objects, and let `k = |D|`. All three roots are `SortedMap`s over the size-measured lazy
 * Hinze-Paterson finger tree, so these are what the substrate actually delivers:
 *
 * - Point lookup, point write, point removal, rank, and neighbour queries are O(log N): each is one
 *   cached-size or cached-extreme descent, and a write is that descent plus a path-copied splice.
 * - `size` and `changeCount` are O(1), read from strict cached subtree sizes.
 * - `minEntry` and `maxEntry` are **O(1) worst case** - they are `front`/`back` digit reads on the
 *   finger tree, not rank selects - matching the C# baseline.
 * - `checkpoint` and `rollback` are O(1) root swaps invoking no policy callback at all.
 * - `getChange` is O(log(k + 1)), searching `D` rather than `S`.
 * - Fully consuming `getChanges` is Theta(k + 1), output-optimal, and free of policy callbacks. That
 *   is the whole point of the design: it avoids the Theta(k log(N / k + 1)) adversarial
 *   divergent-path walk a reference-pruned comparison of two path-copied balanced trees must
 *   perform. The walk is lazy, and because a finger tree keeps its leading elements in the root's
 *   prefix digit rather than down a spine, the first change costs Theta(1); abandoning the
 *   enumeration early skips the rest.
 * - `changesInRange` is a genuine boundary seek plus a bounded walk over the in-range sub-index,
 *   never a filter over all `k` records: O(log(k + 1)) key comparisons locate both boundaries,
 *   O(log(k + 1)) nodes are copied by the two splits that isolate them, and each yielded change then
 *   costs Theta(1). No value comparison happens at all.
 * - `sharesStorageWith` is O(1) when the compared roots are the same object, which is what every
 *   canonicalisation check asks; a *negative* answer between two different roots costs a walk of
 *   both node sets.
 *
 * A live version occupies O(N + k) nodes; retaining every successor adds O(log N) nodes per changed
 * point write, the same asymptotic persistence cost as an ordinary path-copied ordered map. The
 * mutation surface is deliberately point-only: an eager bulk clear would produce Theta(N) removal
 * records and could not also keep an ordinary map's O(1) clear, so callers open a fresh epoch with
 * `empty` when they do not need an enumerable delta. `setItems` is no exception - it *is* the left
 * fold of `setItem`, an allocation saving that claims no better bound than the O(m log N) sequence
 * of point writes it replaces.
 *
 * Policies. Key identity and output order come from a retained comparator; two keys name the same
 * class exactly when it compares them equal. Semantic no-ops and change cancellation come from a
 * retained value-equality relation. Both are retained on the map rather than passed per call, so an
 * empty or wholly cancelled result stays usable and every successor decides no-ops the same way.
 * "Exact" therefore means exact *extensionally* under those two policies, not by object identity of
 * the retained key or value representatives. The comparator must define a stable total preorder and
 * the relation a stable reflexive, symmetric, transitive equivalence; a non-reflexive relation, such
 * as raw IEEE equality over `NaN`, would record changes that never cancel. Policy side effects
 * cannot be undone, but a throwing policy never publishes a partial successor: the receiver and
 * every retained branch stay valid and unchanged. A baseline key representative survives point
 * updates and delete/re-add round trips; a newly added class keeps its first representative while
 * its net-added record is active, and a later addition after complete cancellation begins a new
 * representative episode.
 *
 * Divergences from the C# baseline, all deliberate:
 *
 * - Presence is an explicit wrapper. `undefined` is a legal stored value in JavaScript, so a bare
 *   `V | undefined` endpoint could not distinguish absence from a stored `undefined`. Endpoints are
 *   the discriminated union `DeltaMapValue<V>`, mirroring C#'s `DeltaMapValue<T>`, Rust's
 *   `Option<Arc<V>>`, and this workspace's own `MapPatchValue<V>`.
 * - `kind` is a field computed when a change record is emitted rather than a property that throws on
 *   the structurally impossible both-absent record.
 * - There is no `Empty` singleton; `empty` takes both policies with module-level defaults, matching
 *   `SortedMap.empty` and `PersistentIntervalMap.empty`.
 * - `from` retains the **first** representative of each key class while the last value wins, because
 *   the substrate's insertion does. C#'s `CreateRange` lets the last representative win, which
 *   contradicts its own `SetItem`; the rule used here is the one the point writes already follow.
 * - Lookups return `undefined` for an absent key instead of throwing, and `indexOfKey` returns
 *   `undefined` rather than `-1`, matching the substrate. `entryAt` throws `RangeError` outside
 *   `0..size - 1`, matching C#'s throwing `EntryAt` and this workspace's error convention, while
 *   `minEntry` and `maxEntry` keep the substrate's presence-safe `undefined` for an empty map.
 * - `getChanges` and `changesInRange` return lazy single-pass generators rather than repeatable
 *   enumerables. Every version is immutable, so calling either again yields the same sequence.
 * - C#'s `GetChanges(low, high)` overload is named `changesInRange`, as in the Rust and Python ports.
 * - `validateStructure` returns `DeltaMapStatistics` instead of C#'s `void ValidateInvariants`,
 *   matching the audit convention of every other collection here.
 * - C# documents `OverflowException` at `int.MaxValue` entries; the substrate imposes no such
 *   ceiling, so that contract is vacuous here and is not reproduced.
 */
import { defaultComparator, type Comparator } from "./ordering.js";
import { SortedMap, type SortedMapEntry } from "./sorted.js";

/**
 * A presence-safe change endpoint: absent when the key class is missing at that end, present
 * otherwise, so a stored `undefined` stays distinct from absence.
 */
export type DeltaMapValue<V> =
    | { readonly present: false }
    | { readonly present: true; readonly value: V };

/** Creates the absent endpoint, used when a key class is missing at that end. O(1). */
export function absentDeltaMapValue<V>(): DeltaMapValue<V> { return { present: false }; }

/** Creates a present endpoint, including a present `undefined`. O(1). */
export function presentDeltaMapValue<V>(value: V): DeltaMapValue<V> { return { present: true, value }; }

/** Classifies one net key change relative to a {@link PersistentDeltaMap} checkpoint. */
export type PersistentMapChangeKind = "added" | "removed" | "updated";

/** One exact net change between a version's checkpoint and its current state. */
export interface PersistentMapChange<K, V> {
    /**
     * The checkpoint representative when the class was present at the checkpoint, and otherwise the
     * current addition episode's first representative.
     */
    readonly key: K;
    /** The checkpoint endpoint. */
    readonly before: DeltaMapValue<V>;
    /** The current endpoint. */
    readonly after: DeltaMapValue<V>;
    /** The classification implied by the two endpoints. */
    readonly kind: PersistentMapChangeKind;
}

/** Representation measurements returned by a successful structural audit. */
export interface DeltaMapStatistics {
    /** Number of current entries. */
    readonly size: number;
    /** Number of checkpoint entries. */
    readonly checkpointSize: number;
    /** Number of exact net-changed key classes. */
    readonly changeCount: number;
    /** Records absent at the checkpoint and present now. */
    readonly addedCount: number;
    /** Records present at the checkpoint and absent now. */
    readonly removedCount: number;
    /** Records present at both endpoints with non-equivalent values. */
    readonly updatedCount: number;
    /** Whether the current state is the exact checkpoint root. */
    readonly isClean: boolean;
}

/** One `(before, after)` endpoint pair stored in the change index. */
interface DeltaRecord<V> { readonly before: DeltaMapValue<V>; readonly after: DeltaMapValue<V> }

function sameValueZero<V>(left: V, right: V): boolean { return left === right || (left !== left && right !== right); }

/**
 * The stored entry for the key's class, presence-safely, in O(log N). One ceiling descent plus one
 * comparison, exactly as the C# and Rust ports resolve a class representative.
 */
function lookupEntry<K, V>(map: SortedMap<K, V>, key: K): SortedMapEntry<K, V> | undefined {
    const entry = map.ceilingEntry(key);
    return entry !== undefined && map.comparator(entry.key, key) === 0 ? entry : undefined;
}

function changeKind<V>(before: DeltaMapValue<V>, after: DeltaMapValue<V>): PersistentMapChangeKind {
    if (before.present) return after.present ? "updated" : "removed";
    if (after.present) return "added";
    throw new Error("PersistentDeltaMap invariant violated: a recorded change is absent at both endpoints.");
}

function toChange<K, V>(entry: SortedMapEntry<K, DeltaRecord<V>>): PersistentMapChange<K, V> {
    const { before, after } = entry.value;
    return { key: entry.key, before, after, kind: changeKind(before, after) };
}

function normalizeItem<K, V>(item: readonly [K, V] | SortedMapEntry<K, V>): readonly [K, V] {
    if (Array.isArray(item)) return item as readonly [K, V];
    const entry = item as SortedMapEntry<K, V>;
    return [entry.key, entry.value];
}

/**
 * Immutable sorted map carrying a checkpoint and the exact net changes from it.
 *
 * See the module documentation for the representation, the policy contract, and the complexity
 * statements. Every operation returns a new version and leaves the receiver, and every other
 * retained version, valid and unchanged; a semantic no-op returns the receiver itself, sharing all
 * three roots.
 */
export class PersistentDeltaMap<K, V> implements Iterable<SortedMapEntry<K, V>> {
    readonly #current: SortedMap<K, V>;
    readonly #checkpoint: SortedMap<K, V>;
    readonly #changes: SortedMap<K, DeltaRecord<V>>;
    /** The retained comparator defining key classes and ascending order. */
    public readonly comparator: Comparator<K>;
    /** The retained relation deciding semantic no-ops and change cancellation. */
    public readonly valueEquals: (left: V, right: V) => boolean;

    private constructor(
        current: SortedMap<K, V>,
        checkpoint: SortedMap<K, V>,
        changes: SortedMap<K, DeltaRecord<V>>,
        comparator: Comparator<K>,
        valueEquals: (left: V, right: V) => boolean,
    ) {
        this.#current = current;
        this.#checkpoint = checkpoint;
        this.#changes = changes;
        this.comparator = comparator;
        this.valueEquals = valueEquals;
    }

    /**
     * The empty map, retaining the supplied policy objects; its current state is its checkpoint.
     * O(1). This is also the O(1) way to open an unrelated clean epoch, which is not the same as
     * clearing while keeping the old checkpoint - an operation the point-only surface omits.
     */
    public static empty<K, V>(
        comparator: Comparator<K> = defaultComparator,
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
    ): PersistentDeltaMap<K, V> {
        const state = SortedMap.empty<K, V>(comparator);
        return new PersistentDeltaMap(state, state, SortedMap.empty<K, DeltaRecord<V>>(comparator), comparator, valueEquals);
    }

    /**
     * Builds a clean checkpoint from the given entries. O(m log m) for m entries. Within one key
     * class the first representative is retained and the last value wins.
     */
    public static from<K, V>(
        items: Iterable<readonly [K, V] | SortedMapEntry<K, V>>,
        comparator: Comparator<K> = defaultComparator,
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
    ): PersistentDeltaMap<K, V> {
        if (items === null || items === undefined) throw new TypeError("items must be iterable.");
        const state = SortedMap.from<K, V>(items, comparator);
        return new PersistentDeltaMap(state, state, SortedMap.empty<K, DeltaRecord<V>>(comparator), comparator, valueEquals);
    }

    /** Number of current entries. O(1). */
    public get size(): number { return this.#current.size; }
    /** Whether the current map holds no entries. O(1). */
    public get isEmpty(): boolean { return this.#current.isEmpty; }
    /** Number of exact net-changed key classes. O(1). */
    public get changeCount(): number { return this.#changes.size; }
    /** Whether the current map differs semantically from its checkpoint. O(1). */
    public get hasChanges(): boolean { return !this.#changes.isEmpty; }
    /**
     * Whether this version records no change *and* reuses the exact checkpoint root. O(1). The two
     * halves cannot come apart: a version whose change index empties snaps its current root back to
     * the checkpoint root, and {@link validateStructure} rejects any version where they have.
     */
    public get isClean(): boolean { return this.#changes.isEmpty && this.#current === this.#checkpoint; }
    /** The current persistent ordered-map root. O(1). */
    public get currentSnapshot(): SortedMap<K, V> { return this.#current; }
    /** The checkpoint persistent ordered-map root. O(1). */
    public get checkpointSnapshot(): SortedMap<K, V> { return this.#checkpoint; }

    /** Whether a current entry exists for the key's class. O(log N). */
    public containsKey(key: K): boolean { return this.#current.containsKey(key); }
    /**
     * The current value for the key's class, or `undefined` when absent. O(log N). Use
     * {@link getEntry} when a stored `undefined` must stay distinct from absence.
     */
    public get(key: K): V | undefined { return this.#current.get(key); }
    /** The retained current representative and value for the key's class. O(log N). */
    public getEntry(key: K): SortedMapEntry<K, V> | undefined { return lookupEntry(this.#current, key); }
    /** The zero-based rank of the key's class, or `undefined` when absent. O(log N). */
    public indexOfKey(key: K): number | undefined { return this.#current.indexOfKey(key); }
    /** The current entry at the given zero-based key rank. O(log N). */
    public entryAt(rank: number): SortedMapEntry<K, V> {
        const entry = this.#current.entryAt(rank);
        if (entry === undefined) throw new RangeError("Entry rank is outside the delta map.");
        return entry;
    }
    /** The least current entry by key, or `undefined` when empty. O(1) worst case: a digit read. */
    public minEntry(): SortedMapEntry<K, V> | undefined { return this.#current.minEntry(); }
    /** The greatest current entry by key, or `undefined` when empty. O(1) worst case: a digit read. */
    public maxEntry(): SortedMapEntry<K, V> | undefined { return this.#current.maxEntry(); }
    /** The greatest current entry whose key is at most the probe. O(log N). */
    public floorEntry(key: K): SortedMapEntry<K, V> | undefined { return this.#current.floorEntry(key); }
    /** The least current entry whose key is at least the probe. O(log N). */
    public ceilingEntry(key: K): SortedMapEntry<K, V> | undefined { return this.#current.ceilingEntry(key); }
    /** The greatest current entry whose key is strictly below the probe. O(log N). */
    public lowerEntry(key: K): SortedMapEntry<K, V> | undefined { return this.#current.lowerEntry(key); }
    /** The least current entry whose key is strictly above the probe. O(log N). */
    public higherEntry(key: K): SortedMapEntry<K, V> | undefined { return this.#current.higherEntry(key); }
    /**
     * The current entries whose keys lie in the inclusive range as a plain ordered map. O(log N). An
     * inverted range yields an empty map. This is a read: the result does not open a delta epoch.
     */
    public getKeyRange(low: K, high: K): SortedMap<K, V> { return this.#current.getKeyRange(low, high); }
    /** Copy the current keys into an array, in ascending order. Theta(n). */
    public keys(): K[] { return this.#current.keys(); }
    /** Copy the current values into an array, in ascending key order. Theta(n). */
    public values(): V[] { return this.#current.values(); }
    /** Copy the current entries into an array, in ascending key order. Theta(n). */
    public toArray(): SortedMapEntry<K, V>[] { return this.#current.toArray(); }

    /** One exact checkpoint-relative net change, or `undefined` when the class is unchanged. O(log(k + 1)). */
    public getChange(key: K): PersistentMapChange<K, V> | undefined {
        const entry = lookupEntry(this.#changes, key);
        return entry === undefined ? undefined : toChange(entry);
    }

    /**
     * Iterates the exact net changes once each in ascending key order. Theta(k + 1) when fully
     * consumed, which is output-optimal, and free of key and value policy callbacks. The walk is
     * lazy, so abandoning it early skips the rest, and the first change costs Theta(1): the
     * substrate's leading elements sit in the root's prefix digit, with no spine to descend.
     */
    public *getChanges(): Generator<PersistentMapChange<K, V>, void> {
        for (const entry of this.#changes) yield toChange(entry);
    }

    /**
     * Iterates the net changes whose keys lie in the inclusive range `[low, high]`, with the
     * ordering, endpoints, kinds, and representatives {@link getChanges} uses.
     *
     * The change index is itself an ordered map under the same retained comparator, so this is a
     * boundary seek plus a bounded walk over the in-range sub-index, never a filter over all k
     * records: O(log(k + 1)) key comparisons locate both boundaries, O(log(k + 1)) nodes are copied
     * by the two splits that isolate them, and each yielded change then costs Theta(1). No value
     * comparison happens at all, and the seek is deferred to the first step of the iteration.
     *
     * An inverted range yields no changes rather than failing, matching {@link getKeyRange}.
     * "Inverted" is decided by the retained comparator, so under a descending order the low endpoint
     * is the numerically greater key.
     */
    public *changesInRange(low: K, high: K): Generator<PersistentMapChange<K, V>, void> {
        for (const entry of this.#changes.getKeyRange(low, high)) yield toChange(entry);
    }

    /**
     * Adds or replaces one current entry and coalesces its checkpoint-relative change. O(log N).
     *
     * A write whose value the retained relation considers equal to the current value is a semantic
     * no-op and returns the receiver itself, sharing all three roots. A write that returns a class to
     * its checkpoint state deletes that class's record instead of recording an update.
     */
    public setItem(key: K, value: V): PersistentDeltaMap<K, V> {
        const currentEntry = lookupEntry(this.#current, key);
        if (currentEntry !== undefined && this.valueEquals(currentEntry.value, value)) return this;

        const changeEntry = lookupEntry(this.#changes, key);
        // A class present at the checkpoint keeps its baseline representative; a still-active
        // addition episode keeps its first representative; only a genuinely new class takes the
        // supplied key.
        const representative = currentEntry !== undefined
            ? currentEntry.key
            : changeEntry !== undefined ? changeEntry.key : key;
        // The first effective write captures `before`; later writes preserve it.
        const before = changeEntry !== undefined
            ? changeEntry.value.before
            : currentEntry !== undefined ? presentDeltaMapValue(currentEntry.value) : absentDeltaMapValue<V>();
        const after = presentDeltaMapValue(value);
        // `SortedMap.setItem` vetoes a write whose value is `Object.is`-equal to the stored one.
        // Two `Object.is`-equal values are the same value, so a reflexive relation cannot call them
        // different: reaching here means the veto cannot fire and the current root really moves.
        // (Python needed an explicit reissue here because its substrate vetoes on `==`, which *is*
        // coarser than a legal relation.)
        const nextCurrent = this.#current.setItem(representative, value);
        const nextChanges = this.#endpointsEqual(before, after)
            ? this.#changes.remove(representative)
            : this.#changes.setItem(representative, { before, after });
        return this.#successor(nextCurrent, nextChanges);
    }

    /**
     * Applies the entries in iteration order, exactly as repeated {@link setItem} would.
     * O(m log N) for m entries.
     *
     * This is a convenience and an allocation saving over repeated single assignment, not a different
     * algorithm: it *is* the left fold of {@link setItem}, so every coalescing, cancellation,
     * representative-retention, and checkpoint-snap rule holds verbatim and no stronger bound is
     * claimed. A later entry for the same class wins. An empty sequence, or one whose every entry is
     * a semantic no-op, returns the receiver. Because each intermediate value is itself an immutable
     * successor that published nothing, a policy throwing partway leaves the receiver and every
     * retained branch unchanged.
     */
    public setItems(items: Iterable<readonly [K, V] | SortedMapEntry<K, V>>): PersistentDeltaMap<K, V> {
        if (items === null || items === undefined) throw new TypeError("items must be iterable.");
        let result: PersistentDeltaMap<K, V> = this;
        for (const item of items) {
            const [key, value] = normalizeItem(item);
            result = result.setItem(key, value);
        }
        return result;
    }

    /**
     * Removes one current entry and coalesces its checkpoint-relative change. O(log N).
     *
     * Removing an absent key is a no-op that returns the receiver itself. Removing a class added
     * since the checkpoint cancels its record instead of recording a removal.
     */
    public remove(key: K): PersistentDeltaMap<K, V> {
        const currentEntry = lookupEntry(this.#current, key);
        if (currentEntry === undefined) return this;

        const changeEntry = lookupEntry(this.#changes, key);
        const representative = changeEntry !== undefined ? changeEntry.key : currentEntry.key;
        const before = changeEntry !== undefined ? changeEntry.value.before : presentDeltaMapValue(currentEntry.value);
        const after = absentDeltaMapValue<V>();
        const nextCurrent = this.#current.remove(currentEntry.key);
        const nextChanges = this.#endpointsEqual(before, after)
            ? this.#changes.remove(representative)
            : this.#changes.setItem(representative, { before, after });
        return this.#successor(nextCurrent, nextChanges);
    }

    /**
     * Makes the current snapshot the new checkpoint and resets the change index. O(1). An
     * already-clean version returns the receiver itself; no policy callback is invoked either way.
     */
    public checkpoint(): PersistentDeltaMap<K, V> {
        if (this.isClean) return this;
        return new PersistentDeltaMap(
            this.#current, this.#current, SortedMap.empty<K, DeltaRecord<V>>(this.comparator), this.comparator, this.valueEquals);
    }

    /**
     * Returns to this version's checkpoint and resets the change index. O(1). An already-clean
     * version returns the receiver itself; no policy callback is invoked either way.
     */
    public rollback(): PersistentDeltaMap<K, V> {
        if (this.isClean) return this;
        return new PersistentDeltaMap(
            this.#checkpoint, this.#checkpoint, SortedMap.empty<K, DeltaRecord<V>>(this.comparator), this.comparator, this.valueEquals);
    }

    /**
     * Whether all three of both versions' roots share at least one node by identity. A representation
     * test used to show that a write or a cancellation avoided copying, not an equality test, and
     * deliberately not a same-root test: the substrate answers node overlap, so two versions that
     * merely descend from a common ancestor report `true` while holding different entries. Compare
     * the snapshots with `===` when root identity is what you mean. Two empty roots always share, so
     * a negative control needs non-empty states.
     */
    public sharesStorageWith(other: PersistentDeltaMap<K, V>): boolean {
        return this.#current.sharesStorageWith(other.#current)
            && this.#checkpoint.sharesStorageWith(other.#checkpoint)
            && this.#changes.sharesStorageWith(other.#changes);
    }

    /**
     * Walks the whole representation and returns its measurements, throwing on the first violation.
     *
     * Checks that all three roots ascend strictly under the retained comparator, that a version
     * without changes reuses the exact checkpoint root, that every record has non-equivalent
     * endpoints matching the checkpoint and current states, and that the recorded change cardinality
     * is exactly the number of differing classes. A defensive audit, not part of any operation's
     * cost: it is O((N + k) log N) and does invoke both policies.
     */
    public validateStructure(): DeltaMapStatistics {
        this.#requireAscending(this.#current.keys(), "the current state is not strictly ascending by key");
        this.#requireAscending(this.#checkpoint.keys(), "the checkpoint state is not strictly ascending by key");
        this.#requireAscending(this.#changes.keys(), "the change index is not strictly ascending by key");

        const isClean = this.#current === this.#checkpoint;
        if (this.#changes.isEmpty && !isClean) {
            throw new Error("PersistentDeltaMap invariant violated: a version without changes does not reuse its checkpoint root.");
        }

        let addedCount = 0;
        let removedCount = 0;
        let updatedCount = 0;
        for (const change of this.#changes) {
            const { before, after } = change.value;
            if (this.#endpointsEqual(before, after)) {
                throw new Error("PersistentDeltaMap invariant violated: a recorded change has equivalent endpoints.");
            }
            if (!this.#endpointMatches(before, lookupEntry(this.#checkpoint, change.key))) {
                throw new Error("PersistentDeltaMap invariant violated: a change's before endpoint differs from the checkpoint state.");
            }
            if (!this.#endpointMatches(after, lookupEntry(this.#current, change.key))) {
                throw new Error("PersistentDeltaMap invariant violated: a change's after endpoint differs from the current state.");
            }
            if (!before.present) addedCount++;
            else if (!after.present) removedCount++;
            else updatedCount++;
        }

        let expected = 0;
        for (const baseline of this.#checkpoint) {
            const current = lookupEntry(this.#current, baseline.key);
            if (current === undefined || !this.valueEquals(baseline.value, current.value)) {
                expected++;
                if (lookupEntry(this.#changes, baseline.key) === undefined) {
                    throw new Error("PersistentDeltaMap invariant violated: a checkpoint difference has no recorded change.");
                }
            }
        }
        for (const entry of this.#current) {
            if (lookupEntry(this.#checkpoint, entry.key) === undefined) {
                expected++;
                if (lookupEntry(this.#changes, entry.key) === undefined) {
                    throw new Error("PersistentDeltaMap invariant violated: a current addition has no recorded change.");
                }
            }
        }
        if (expected !== this.#changes.size) {
            throw new Error("PersistentDeltaMap invariant violated: the recorded change cardinality is not exact.");
        }

        return {
            size: this.#current.size,
            checkpointSize: this.#checkpoint.size,
            changeCount: this.#changes.size,
            addedCount,
            removedCount,
            updatedCount,
            isClean,
        };
    }

    public [Symbol.iterator](): IterableIterator<SortedMapEntry<K, V>> { return this.#current[Symbol.iterator](); }

    #successor(current: SortedMap<K, V>, changes: SortedMap<K, DeltaRecord<V>>): PersistentDeltaMap<K, V> {
        // A wholly cancelled epoch snaps back to the exact checkpoint root, so clean versions are
        // canonical and later checkpoint, rollback, and cleanliness queries stay O(1).
        return new PersistentDeltaMap(
            changes.isEmpty ? this.#checkpoint : current, this.#checkpoint, changes, this.comparator, this.valueEquals);
    }

    #endpointsEqual(left: DeltaMapValue<V>, right: DeltaMapValue<V>): boolean {
        if (!left.present) return !right.present;
        return right.present && this.valueEquals(left.value, right.value);
    }

    #endpointMatches(endpoint: DeltaMapValue<V>, stored: SortedMapEntry<K, V> | undefined): boolean {
        if (!endpoint.present) return stored === undefined;
        return stored !== undefined && this.valueEquals(endpoint.value, stored.value);
    }

    #requireAscending(keys: K[], message: string): void {
        for (let index = 1; index < keys.length; index++) {
            if (this.comparator(keys[index - 1]!, keys[index]!) >= 0) {
                throw new Error(`PersistentDeltaMap invariant violated: ${message}.`);
            }
        }
    }
}
