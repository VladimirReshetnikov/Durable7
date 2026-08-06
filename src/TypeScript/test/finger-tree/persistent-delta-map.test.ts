/**
 * Tests for the checkpoint-differential sorted map: semantic no-ops, coalescing and cancellation,
 * the storage-clean snap back to the checkpoint root, key-representative retention, callback-free
 * checkpoint and rollback, and range-restricted change enumeration as a boundary seek rather than a
 * filter.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";

import { defaultComparator, type Comparator } from "../../src/finger-tree/ordering.js";
import {
    PersistentDeltaMap,
    type PersistentMapChange,
} from "../../src/finger-tree/persistent-delta-map.js";

/** A live call counter, so a test can prove a policy was and was not invoked. */
class Meter {
    public count = 0;
    public reset(): void { this.count = 0; }
}

const caseInsensitive: Comparator<string> = (left, right) => {
    const lowerLeft = left.toLowerCase();
    const lowerRight = right.toLowerCase();
    return lowerLeft < lowerRight ? -1 : lowerLeft > lowerRight ? 1 : 0;
};

const equalIgnoringCase = (left: string, right: string): boolean => left.toLowerCase() === right.toLowerCase();

function numberedEntries(count: number): (readonly [number, string])[] {
    return Array.from({ length: count }, (_, index): readonly [number, string] => [index, `v${index}`]);
}

describe("PersistentDeltaMap", () => {
    test("records added, removed, and updated classes with presence-safe endpoints", () => {
        const source = PersistentDeltaMap.from<string, number | undefined>([["a", 1], ["b", 2]]);
        const edited = source.setItem("a", 10).remove("b").setItem("c", undefined);

        expect(edited.changeCount).toBe(3);
        expect(edited.hasChanges).toBe(true);
        expect([...edited.getChanges()]).toStrictEqual([
            { key: "a", before: { present: true, value: 1 }, after: { present: true, value: 10 }, kind: "updated" },
            { key: "b", before: { present: true, value: 2 }, after: { present: false }, kind: "removed" },
            { key: "c", before: { present: false }, after: { present: true, value: undefined }, kind: "added" },
        ]);

        // A stored `undefined` is present, not absent: the wrapper is what keeps the two apart.
        expect(edited.containsKey("c")).toBe(true);
        expect(edited.get("c")).toBeUndefined();
        expect(edited.getEntry("c")).toStrictEqual({ key: "c", value: undefined });
        expect(edited.getEntry("b")).toBeUndefined();
        expect(edited.getChange("c")?.after).toStrictEqual({ present: true, value: undefined });
        expect(edited.remove("c").getChange("c")).toBeUndefined();

        const statistics = edited.validateStructure();
        expect(statistics).toStrictEqual({
            size: 2, checkpointSize: 2, changeCount: 3, addedCount: 1, removedCount: 1, updatedCount: 1, isClean: false,
        });
        expect(source.validateStructure().isClean).toBe(true);
    });

    test("treats a write the retained relation calls equal as a semantic no-op", () => {
        const map = PersistentDeltaMap.from<string, string>([["a", "Value"]], defaultComparator, equalIgnoringCase);

        // The receiver itself comes back, which is the only way to share all three roots.
        expect(map.setItem("a", "VALUE")).toBe(map);
        expect(map.setItem("a", "vAlUe")).toBe(map);
        expect(map.changeCount).toBe(0);

        // Positive control: the relation really is consulted, and a genuine difference is recorded.
        const effective = map.setItem("a", "other");
        expect(effective).not.toBe(map);
        expect(effective.changeCount).toBe(1);
        expect(effective.getChange("a")?.kind).toBe("updated");

        // A no-op over a dirty version leaves its record exactly as it was.
        expect(effective.setItem("a", "OTHER")).toBe(effective);
        expect(map.remove("absent")).toBe(map);
        expect(map.setItems([])).toBe(map);
    });

    test("coalesces repeated writes into one record and keeps the first before endpoint", () => {
        const source = PersistentDeltaMap.from([["a", 1], ["b", 2]]);
        const edited = source.setItem("a", 2).setItem("a", 3).setItem("a", 4);

        expect(edited.changeCount).toBe(1);
        expect(edited.get("a")).toBe(4);
        expect(edited.getChange("a")).toStrictEqual({
            key: "a", before: { present: true, value: 1 }, after: { present: true, value: 4 }, kind: "updated",
        });
        expect(edited.getChange("b")).toBeUndefined();
        expect(edited.validateStructure().updatedCount).toBe(1);
    });

    test("cancels a record when a class returns to its checkpoint state and snaps the root back", () => {
        const source = PersistentDeltaMap.from([["a", 1], ["b", 2]]);

        const restored = source.setItem("a", 5).setItem("a", 1);
        expect(restored.changeCount).toBe(0);
        expect(restored.hasChanges).toBe(false);

        // Cancellation must consult the RETAINED relation, not `Object.is`. Under the default
        // relation on primitives the two coincide, so every assertion above also passes for a map
        // that ignores its policy; a coarse relation separates them. Writing "VALUE" over a
        // checkpointed "value" is relation-equal, so the record must cancel even though the two
        // strings are distinct values.
        const coarse = PersistentDeltaMap.from<string, string>(
            [["k", "value"]], defaultComparator, equalIgnoringCase);
        const roundTripped = coarse.setItem("k", "other").setItem("k", "VALUE");
        expect(roundTripped.changeCount).toBe(0);
        expect(roundTripped.hasChanges).toBe(false);
        expect(roundTripped.currentSnapshot).toBe(coarse.checkpointSnapshot);
        expect(restored.isClean).toBe(true);
        // Two genuinely distinct versions: the successor holds the checkpoint root itself.
        expect(restored).not.toBe(source);
        expect(restored.currentSnapshot).toBe(source.checkpointSnapshot);
        expect(restored.toArray()).toEqual(source.toArray());

        const addThenRemove = source.setItem("z", 9).remove("z");
        expect(addThenRemove.changeCount).toBe(0);
        expect(addThenRemove.currentSnapshot).toBe(source.checkpointSnapshot);

        const removeThenRestore = source.remove("a").setItem("a", 1);
        expect(removeThenRestore.changeCount).toBe(0);
        expect(removeThenRestore.currentSnapshot).toBe(source.checkpointSnapshot);
        expect(removeThenRestore.get("a")).toBe(1);

        // Negative control: a version that still differs does not reuse the checkpoint root.
        const dirty = source.setItem("a", 5);
        expect(dirty.isClean).toBe(false);
        expect(dirty.currentSnapshot).not.toBe(dirty.checkpointSnapshot);
        expect(dirty.checkpointSnapshot).toBe(source.checkpointSnapshot);
        expect(() => dirty.validateStructure()).not.toThrow();
    });

    test("retains the key representative across updates and delete/re-add round trips", () => {
        const source = PersistentDeltaMap.from([["Alpha", 1]], caseInsensitive);

        const updated = source.setItem("ALPHA", 2);
        expect(updated.getEntry("alpha")?.key).toBe("Alpha");
        expect(updated.getChange("alpha")?.key).toBe("Alpha");

        const roundTrip = source.remove("alpha").setItem("aLpHa", 3);
        expect(roundTrip.getEntry("ALPHA")?.key).toBe("Alpha");
        expect(roundTrip.getChange("ALPHA")).toStrictEqual({
            key: "Alpha", before: { present: true, value: 1 }, after: { present: true, value: 3 }, kind: "updated",
        });
        expect(roundTrip.keys()).toEqual(["Alpha"]);
    });

    test("begins a new representative episode only after complete cancellation", () => {
        const empty = PersistentDeltaMap.empty<string, number>(caseInsensitive);

        const added = empty.setItem("Beta", 1);
        expect(added.getChange("beta")?.key).toBe("Beta");
        expect(added.getChange("beta")?.kind).toBe("added");

        const kept = added.setItem("BETA", 2);
        expect(kept.getEntry("beta")?.key).toBe("Beta");
        expect(kept.getChange("beta")?.key).toBe("Beta");

        const cancelled = kept.remove("bEtA");
        expect(cancelled.changeCount).toBe(0);
        expect(cancelled.isClean).toBe(true);

        const reborn = cancelled.setItem("bEtA", 7);
        expect(reborn.getEntry("beta")?.key).toBe("bEtA");
        expect(reborn.getChange("BETA")?.key).toBe("bEtA");
        expect(reborn.getChange("BETA")?.kind).toBe("added");
    });

    test("checkpoint and rollback are root swaps invoking no policy callback", () => {
        const keys = new Meter();
        const values = new Meter();
        const comparator: Comparator<number> = (left, right) => { keys.count++; return defaultComparator(left, right); };
        const valueEquals = (left: string, right: string): boolean => { values.count++; return left === right; };

        const source = PersistentDeltaMap.from(numberedEntries(64), comparator, valueEquals);
        const edited = source.setItem(3, "changed").remove(9);

        keys.reset();
        values.reset();
        const committed = edited.checkpoint();
        expect(keys.count).toBe(0);
        expect(values.count).toBe(0);
        expect(committed.currentSnapshot).toBe(edited.currentSnapshot);
        expect(committed.checkpointSnapshot).toBe(edited.currentSnapshot);
        expect(committed.changeCount).toBe(0);
        expect(committed.isClean).toBe(true);
        expect(committed.checkpoint()).toBe(committed);

        const rolled = edited.rollback();
        expect(keys.count).toBe(0);
        expect(values.count).toBe(0);
        expect(rolled.currentSnapshot).toBe(source.checkpointSnapshot);
        expect(rolled.checkpointSnapshot).toBe(source.checkpointSnapshot);
        expect(rolled.changeCount).toBe(0);
        expect(rolled.rollback()).toBe(rolled);
        expect(rolled.toArray()).toEqual(source.toArray());

        // Positive control: both meters are live, so the zeroes above mean something.
        const further = committed.setItem(3, "again");
        expect(keys.count).toBeGreaterThan(0);
        expect(values.count).toBeGreaterThan(0);
        expect(further.getChange(3)?.before).toStrictEqual({ present: true, value: "changed" });
    });

    test("enumerates changes in ascending key order without consulting either policy", () => {
        const keys = new Meter();
        const values = new Meter();
        const comparator: Comparator<number> = (left, right) => { keys.count++; return defaultComparator(left, right); };
        const valueEquals = (left: string, right: string): boolean => { values.count++; return left === right; };

        const source = PersistentDeltaMap.from(numberedEntries(512), comparator, valueEquals);
        const edited = source.setItem(300, "x").remove(500).setItem(10, "y").setItem(999, "z");

        keys.reset();
        values.reset();
        const changes = [...edited.getChanges()];
        // Zero callbacks over 512 entries: this cannot be a comparison of the two states on demand.
        expect(keys.count).toBe(0);
        expect(values.count).toBe(0);
        expect(changes.map((change) => change.key)).toEqual([10, 300, 500, 999]);
        expect(changes.map((change) => change.kind)).toEqual(["updated", "updated", "removed", "added"]);
        expect(edited.changeCount).toBe(4);

        // The walk is lazy: one step yields the least change without materializing the rest.
        const walk = edited.getChanges();
        expect(walk.next().value?.key).toBe(10);
        expect(walk.next().value?.key).toBe(300);
        expect(edited.checkpoint().changeCount).toBe(0);
        expect([...edited.checkpoint().getChanges()]).toEqual([]);
    });

    test("restricts change enumeration with a boundary seek and no value comparison", () => {
        const keys = new Meter();
        const values = new Meter();
        const comparator: Comparator<number> = (left, right) => { keys.count++; return defaultComparator(left, right); };
        const valueEquals = (left: string, right: string): boolean => { values.count++; return left === right; };

        const source = PersistentDeltaMap.from(numberedEntries(1024), comparator, valueEquals);
        const edited = source.setItems(
            Array.from({ length: 512 }, (_, index): readonly [number, string] => [index * 2, `changed${index * 2}`]),
        );
        expect(edited.changeCount).toBe(512);

        keys.reset();
        values.reset();
        const window = [...edited.changesInRange(400, 410)];
        expect(window.map((change) => change.key)).toEqual([400, 402, 404, 406, 408, 410]);
        // The retained relation is never asked anything by a range restriction.
        expect(values.count).toBe(0);
        // A filter over all k records would compare every one of the 512 keys against both bounds;
        // a boundary seek pays O(log k) descents plus the window.
        expect(keys.count).toBeLessThan(edited.changeCount / 4);

        // Positive control: the value meter is live, so the zero above is evidence.
        values.reset();
        edited.setItem(400, "again");
        expect(values.count).toBeGreaterThan(0);

        // The restriction agrees with filtering the full enumeration.
        const filtered = [...edited.getChanges()].filter((change) => change.key >= 400 && change.key <= 410);
        expect(window).toEqual(filtered);
        expect([...edited.changesInRange(401, 401)]).toEqual([]);
        expect([...edited.changesInRange(-50, 1)].map((change) => change.key)).toEqual([0]);
    });

    test("keeps the range seek cost flat as the change set grows", () => {
        // A self-calibrating probe: a filter over all k records costs Theta(k) comparisons, so
        // multiplying k by sixteen would multiply the count by about sixteen. A boundary seek pays
        // O(log(k + 1)) descents, so the count barely moves.
        const seekCost = (changeCount: number): number => {
            const keys = new Meter();
            const comparator: Comparator<number> = (left, right) => { keys.count++; return defaultComparator(left, right); };
            const edited = PersistentDeltaMap.from(numberedEntries(2048), comparator).setItems(
                Array.from({ length: changeCount }, (_, index): readonly [number, string] => [index * 2, `changed${index * 2}`]),
            );
            expect(edited.changeCount).toBe(changeCount);
            keys.reset();
            const window = [...edited.changesInRange(20, 24)];
            expect(window.map((change) => change.key)).toEqual([20, 22, 24]);
            return keys.count;
        };

        const small = seekCost(64);
        const large = seekCost(1024);
        expect(small).toBeGreaterThan(0);
        expect(large).toBeLessThanOrEqual(small * 2);
    });

    test("an inverted range yields no changes, decided by the retained comparator", () => {
        const ascending = PersistentDeltaMap.from(numberedEntries(32))
            .setItem(4, "x").setItem(8, "y").setItem(12, "z");
        expect([...ascending.changesInRange(4, 12)].map((change) => change.key)).toEqual([4, 8, 12]);
        expect([...ascending.changesInRange(12, 4)]).toEqual([]);

        const descending: Comparator<number> = (left, right) => right - left;
        const reversed = PersistentDeltaMap.from(numberedEntries(32), descending)
            .setItem(4, "x").setItem(8, "y").setItem(12, "z");
        // Under a descending order the low endpoint is the numerically greater key.
        expect([...reversed.changesInRange(12, 4)].map((change) => change.key)).toEqual([12, 8, 4]);
        expect([...reversed.changesInRange(4, 12)]).toEqual([]);
        expect(reversed.getKeyRange(4, 12).isEmpty).toBe(true);
        expect(reversed.getKeyRange(12, 4).keys()).toEqual([12, 11, 10, 9, 8, 7, 6, 5, 4]);
    });

    test("folds setItems exactly as repeated setItem and rejects a missing sequence", () => {
        const source = PersistentDeltaMap.from([["a", 1], ["b", 2]]);
        const folded = source.setItems([["a", 5], ["b", 6], ["a", 7]]);
        const stepwise = source.setItem("a", 5).setItem("b", 6).setItem("a", 7);

        expect(folded.toArray()).toEqual(stepwise.toArray());
        expect([...folded.getChanges()]).toEqual([...stepwise.getChanges()]);
        expect(source.setItems([{ key: "a", value: 1 }, ["b", 2]])).toBe(source);
        expect(source.setItems([{ key: "a", value: 4 }]).get("a")).toBe(4);
        expect(() => source.setItems(null as unknown as Iterable<readonly [string, number]>)).toThrow(TypeError);
        expect(() => PersistentDeltaMap.from(null as unknown as Iterable<readonly [string, number]>)).toThrow(TypeError);

        // A batch that ends where it started cancels exactly as the equivalent point writes do.
        const cancelled = source.setItems([["a", 99], ["c", 3]]).setItems([["a", 1]]).remove("c");
        expect(cancelled.isClean).toBe(true);
        expect(cancelled.currentSnapshot).toBe(source.checkpointSnapshot);
    });

    test("keeps every retained version readable and unchanged across branching", () => {
        const source = PersistentDeltaMap.from([["a", 1], ["b", 2]]);
        const left = source.setItem("a", 10);
        const right = source.setItem("a", 20).remove("b");

        expect(source.toArray()).toEqual([{ key: "a", value: 1 }, { key: "b", value: 2 }]);
        expect(left.get("a")).toBe(10);
        expect(right.get("a")).toBe(20);
        expect(right.containsKey("b")).toBe(false);
        expect(left.changeCount).toBe(1);
        expect(right.changeCount).toBe(2);
        expect(source.changeCount).toBe(0);
        expect(left.checkpointSnapshot).toBe(source.currentSnapshot);
    });

    test("shares storage between successors and reports independent roots apart", () => {
        const base = PersistentDeltaMap.from(numberedEntries(64));
        const dirty = base.setItems(numberedEntries(32).map(([key]): readonly [number, string] => [key, `changed${key}`]));
        const successor = dirty.setItem(63, "changed63");

        expect(successor).not.toBe(dirty);
        expect(successor.sharesStorageWith(dirty)).toBe(true);
        expect(successor.checkpointSnapshot).toBe(base.currentSnapshot);

        const unrelated = PersistentDeltaMap.from(numberedEntries(64))
            .setItems(numberedEntries(32).map(([key]): readonly [number, string] => [key, `changed${key}`]));
        expect(unrelated.toArray()).toEqual(dirty.toArray());
        expect(unrelated.sharesStorageWith(dirty)).toBe(false);

        // Distinguish the conjunction from a disjunction: `base` and `dirty` share the checkpoint
        // root (and, being derived, current-state nodes) but `base`'s change index is empty while
        // `dirty`'s is not, so an empty index shares no node with a non-empty one and all-three
        // must refuse. Without this pair, `&&` could be `||` and every other assertion still holds.
        expect(base.checkpointSnapshot).toBe(dirty.checkpointSnapshot);
        expect(base.changeCount).toBe(0);
        expect(dirty.changeCount).toBeGreaterThan(0);
        expect(base.sharesStorageWith(dirty)).toBe(false);
    });

    test("reads delegate to the current state, with extremes at the digits", () => {
        const map = PersistentDeltaMap.from([[1, "a"], [3, "c"], [5, "e"]]);

        expect(map.size).toBe(3);
        expect(map.isEmpty).toBe(false);
        expect(map.minEntry()).toEqual({ key: 1, value: "a" });
        expect(map.maxEntry()).toEqual({ key: 5, value: "e" });
        expect(map.floorEntry(4)).toEqual({ key: 3, value: "c" });
        expect(map.ceilingEntry(4)).toEqual({ key: 5, value: "e" });
        expect(map.lowerEntry(3)).toEqual({ key: 1, value: "a" });
        expect(map.higherEntry(3)).toEqual({ key: 5, value: "e" });
        expect(map.indexOfKey(3)).toBe(1);
        expect(map.indexOfKey(2)).toBeUndefined();
        expect(map.entryAt(2)).toEqual({ key: 5, value: "e" });
        expect(() => map.entryAt(3)).toThrow(RangeError);
        expect(() => map.entryAt(-1)).toThrow(RangeError);
        expect(map.getKeyRange(2, 5).toArray()).toEqual([{ key: 3, value: "c" }, { key: 5, value: "e" }]);
        expect(map.getKeyRange(5, 2).isEmpty).toBe(true);
        expect(map.keys()).toEqual([1, 3, 5]);
        expect(map.values()).toEqual(["a", "c", "e"]);
        expect([...map]).toEqual(map.toArray());

        const empty = PersistentDeltaMap.empty<number, string>();
        expect(empty.minEntry()).toBeUndefined();
        expect(empty.maxEntry()).toBeUndefined();
        expect(empty.isEmpty).toBe(true);
        expect(empty.isClean).toBe(true);
        expect(empty.rollback()).toBe(empty);
    });

    test("builds a clean checkpoint retaining the first representative while the last value wins", () => {
        const map = PersistentDeltaMap.from([["Alpha", 1], ["beta", 2], ["ALPHA", 3]], caseInsensitive);

        expect(map.getEntry("alpha")).toEqual({ key: "Alpha", value: 3 });
        expect(map.keys()).toEqual(["Alpha", "beta"]);
        expect(map.changeCount).toBe(0);
        expect(map.isClean).toBe(true);
        expect(map.comparator).toBe(caseInsensitive);
        expect(map.currentSnapshot).toBe(map.checkpointSnapshot);

        const policed = PersistentDeltaMap.empty<string, string>(caseInsensitive, equalIgnoringCase);
        expect(policed.valueEquals).toBe(equalIgnoringCase);
        expect(policed.setItem("k", "Value").setItem("K", "VALUE").changeCount).toBe(1);
    });

    test("matches a model over randomized edit, checkpoint, and rollback histories", () => {
        const operation = fc.oneof(
            fc.tuple(fc.constant("set" as const), fc.integer({ min: 0, max: 15 }), fc.integer({ min: 0, max: 3 })),
            fc.tuple(fc.constant("remove" as const), fc.integer({ min: 0, max: 15 }), fc.constant(0)),
            fc.tuple(fc.constant("checkpoint" as const), fc.constant(0), fc.constant(0)),
            fc.tuple(fc.constant("rollback" as const), fc.constant(0), fc.constant(0)),
        );

        fc.assert(fc.property(fc.array(operation, { maxLength: 80 }), (operations) => {
            let map = PersistentDeltaMap.empty<number, string>();
            let current = new Map<number, string>();
            let checkpoint = new Map<number, string>();
            for (const [kind, key, value] of operations) {
                if (kind === "set") { map = map.setItem(key, `v${value}`); current.set(key, `v${value}`); }
                else if (kind === "remove") { map = map.remove(key); current.delete(key); }
                else if (kind === "checkpoint") { map = map.checkpoint(); checkpoint = new Map(current); }
                else { map = map.rollback(); current = new Map(checkpoint); }
            }

            expect(map.toArray()).toEqual([...current.entries()]
                .sort((left, right) => left[0] - right[0])
                .map(([key, value]) => ({ key, value })));

            const expected: PersistentMapChange<number, string>[] = [];
            for (const key of [...new Set([...checkpoint.keys(), ...current.keys()])].sort((left, right) => left - right)) {
                const had = checkpoint.has(key);
                const has = current.has(key);
                if (had && has && checkpoint.get(key) === current.get(key)) continue;
                expected.push({
                    key,
                    before: had ? { present: true, value: checkpoint.get(key)! } : { present: false },
                    after: has ? { present: true, value: current.get(key)! } : { present: false },
                    kind: had ? (has ? "updated" : "removed") : "added",
                });
            }
            expect([...map.getChanges()]).toEqual(expected);
            expect([...map.changesInRange(4, 11)]).toEqual(expected.filter((change) => change.key >= 4 && change.key <= 11));

            const statistics = map.validateStructure();
            expect(statistics.changeCount).toBe(expected.length);
            expect(statistics.size).toBe(current.size);
            expect(statistics.checkpointSize).toBe(checkpoint.size);
            expect(statistics.isClean).toBe(expected.length === 0);
            expect(map.isClean).toBe(expected.length === 0);
        }), { numRuns: 200 });
    });
});
