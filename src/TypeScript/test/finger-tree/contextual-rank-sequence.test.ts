/**
 * Tests for the contextual rank sequence: the lifted-automaton measure, its ordered (and
 * deliberately non-commutative) composition, one-descent event select, the retained-machine
 * identity gate, failure atomicity, and the bound probes that pin the reference-strength endpoint
 * and concatenation claims to the lazy finger-tree substrate.
 */
import fc from "fast-check";
import { describe, expect, test } from "vitest";
import {
    ContextualRankSequence,
    createContextualEventMachine,
    observeContextualRankWork,
    type ContextualEventLocation,
    type ContextualEventMachine,
    type ContextualEventTransition,
} from "../../src/finger-tree/contextual-rank-sequence.js";

/** Two states: outside quotes (0) and inside quotes (1). A comma outside quotes is an event. */
const quotedComma: ContextualEventMachine<string> = createContextualEventMachine<string>(2, (state, element) => {
    if (element === "\"") return { nextState: 1 - state, eventCount: 0 };
    if (element === "," && state === 0) return { nextState: 0, eventCount: 1 };
    return { nextState: state, eventCount: 0 };
});

/** Three states with transitions that emit zero, one, or two events. */
const ternary: ContextualEventMachine<number> = createContextualEventMachine<number>(3, (state, element) => {
    const next = (state + Math.abs(element)) % 3;
    return { nextState: next, eventCount: next };
});

type Signal = "arm" | "fire" | "idle";

/**
 * A machine chosen so that swapping two elements changes the outgoing state *and* the event count.
 * A commutative example (say, addition modulo three) would make the ordering test vacuous.
 */
const armedDetector: ContextualEventMachine<Signal> = createContextualEventMachine<Signal>(2, (state, element) => {
    if (element === "arm") return { nextState: 1, eventCount: 0 };
    if (element === "fire") return { nextState: 0, eventCount: state === 1 ? 2 : 0 };
    return { nextState: state, eventCount: 0 };
});

/** Counts every transition, so probes can prove cached queries never rescan the input. */
class CountingMachine implements ContextualEventMachine<number> {
    public readonly stateCount: number = 2;
    public calls: number = 0;
    public transition(state: number, element: number): ContextualEventTransition {
        this.calls++;
        return { nextState: (state + (element & 1)) & 1, eventCount: ((element ^ state) & 3) === 0 ? 1 : 0 };
    }
    public reset(): void { this.calls = 0; }
}

function range(count: number): number[] { return Array.from({ length: count }, (_, index) => index); }

function chars(text: string): string[] { return [...text]; }

/** A deterministic linear-congruential source, so a failing randomized case is reproducible. */
function makeRandom(seed: number): (bound: number) => number {
    let state = seed >>> 0;
    return (bound: number): number => {
        state = (Math.imul(state, 1_664_525) + 1_013_904_223) >>> 0;
        return state % bound;
    };
}

/**
 * Checks a sequence against an independent array plus a direct machine scan, for every initial
 * state, every prefix boundary, and every emitted event.
 */
function assertMatchesModel<T>(
    sequence: ContextualRankSequence<T>,
    model: readonly T[],
    machine: ContextualEventMachine<T>,
): void {
    expect(sequence.machine).toBe(machine);
    expect(sequence.toArray()).toEqual([...model]);
    expect(sequence.size).toBe(model.length);
    expect(sequence.count).toBe(model.length);
    expect(sequence.isEmpty).toBe(model.length === 0);
    expect(sequence.validateStructure().count).toBe(model.length);

    for (let initialState = 0; initialState < sequence.stateCount; initialState++) {
        let state = initialState;
        let events = 0;
        const expected: ContextualEventLocation[] = [];
        expect(sequence.evaluatePrefix(0, initialState)).toEqual({ finalState: initialState, eventCount: 0 });

        for (let index = 0; index < model.length; index++) {
            const element = model[index] as T;
            const step = machine.transition(state, element);
            for (let ordinal = 0; ordinal < step.eventCount; ordinal++) {
                expected.push({
                    elementIndex: index,
                    eventIndexInElement: ordinal,
                    stateBefore: state,
                    stateAfter: step.nextState,
                    elementEventCount: step.eventCount,
                });
            }
            state = step.nextState;
            events += step.eventCount;
            expect(sequence.evaluatePrefix(index + 1, initialState)).toEqual({ finalState: state, eventCount: events });
            expect(sequence.eventRank(index + 1, initialState)).toBe(events);
            expect(sequence.get(index)).toBe(element);
        }

        expect(sequence.evaluate(initialState)).toEqual({ finalState: state, eventCount: events });
        for (let eventIndex = 0; eventIndex < expected.length; eventIndex++) {
            expect(sequence.trySelectEvent(eventIndex, initialState)).toEqual(expected[eventIndex]);
        }
        expect(sequence.trySelectEvent(expected.length, initialState)).toBeUndefined();
    }
}

describe("ContextualRankSequence", () => {
    test("ranks and selects events whose meaning depends on left context", () => {
        const text = ContextualRankSequence.from(chars("\"a,b\",c,d"), quotedComma);

        // The commas inside the quoted field are data; the two outside it are separators.
        expect(text.evaluate(0)).toEqual({ finalState: 0, eventCount: 2 });
        expect(text.evaluate(1)).toEqual({ finalState: 1, eventCount: 1 });
        expect(text.eventRank(5, 0)).toBe(0);
        expect(text.eventRank(6, 0)).toBe(1);

        expect(text.trySelectEvent(0, 0)).toEqual({
            elementIndex: 5,
            eventIndexInElement: 0,
            stateBefore: 0,
            stateAfter: 0,
            elementEventCount: 1,
        });
        expect(text.trySelectEvent(1, 0)?.elementIndex).toBe(7);
        expect(text.trySelectEvent(2, 0)).toBeUndefined();

        // Starting inside a quoted region makes the comma at index 2 the first separator instead.
        expect(text.trySelectEvent(0, 1)?.elementIndex).toBe(2);

        // Editing publishes a new version and leaves the retained one exactly as it was.
        const edited = text.insert(0, "\"");
        expect(text.evaluate(0)).toEqual({ finalState: 0, eventCount: 2 });
        expect(edited.evaluate(0)).toEqual({ finalState: 1, eventCount: 1 });
        expect(text.toArray().join("")).toBe("\"a,b\",c,d");
        expect(edited.toArray().join("")).toBe("\"\"a,b\",c,d");
    });

    test("exhaustive small words match an independent scanner", () => {
        const alphabet = ["\"", ",", "x"] as const;
        for (let length = 0; length <= 6; length++) {
            const words = alphabet.length ** length;
            for (let code = 0; code < words; code++) {
                let value = code;
                const word: string[] = [];
                for (let position = 0; position < length; position++) {
                    word.push(alphabet[value % alphabet.length] as string);
                    value = Math.floor(value / alphabet.length);
                }
                assertMatchesModel(ContextualRankSequence.from(word, quotedComma), word, quotedComma);
            }
        }
    });

    test("ordered composition is not commutative in either component", () => {
        // "arm" then "fire" leaves the machine idle having emitted two events; the same two
        // elements in the other order leave it armed having emitted none. Both components move,
        // which is what makes this a real ordering test rather than a coincidence of one field.
        const armThenFire = ContextualRankSequence.from<Signal>(["arm", "fire"], armedDetector);
        const fireThenArm = ContextualRankSequence.from<Signal>(["fire", "arm"], armedDetector);

        expect(armThenFire.evaluate(0)).toEqual({ finalState: 0, eventCount: 2 });
        expect(fireThenArm.evaluate(0)).toEqual({ finalState: 1, eventCount: 0 });
        expect(armThenFire.evaluate(0)?.finalState).not.toBe(fireThenArm.evaluate(0)?.finalState);
        expect(armThenFire.evaluate(0)?.eventCount).not.toBe(fireThenArm.evaluate(0)?.eventCount);

        // Concatenation composes the two cached summaries; the operand order must survive into the
        // composed measure, not be normalized away.
        const arm = ContextualRankSequence.from<Signal>(["arm"], armedDetector);
        const fire = ContextualRankSequence.from<Signal>(["fire"], armedDetector);
        expect(arm.concat(fire).evaluate(0)).toEqual({ finalState: 0, eventCount: 2 });
        expect(fire.concat(arm).evaluate(0)).toEqual({ finalState: 1, eventCount: 0 });

        // Associativity, which the measure does need, still holds in both groupings.
        const idle = ContextualRankSequence.from<Signal>(["idle"], armedDetector);
        for (let state = 0; state < 2; state++) {
            expect(arm.concat(fire).concat(idle).evaluate(state)).toEqual(arm.concat(fire.concat(idle)).evaluate(state));
        }
        assertMatchesModel(armThenFire, ["arm", "fire"], armedDetector);
        assertMatchesModel(fireThenArm, ["fire", "arm"], armedDetector);
    });

    test("select resolves multi-event transitions to element and ordinal", () => {
        const sequence = ContextualRankSequence.from([2, -3, 4, 0], ternary);
        assertMatchesModel(sequence, [2, -3, 4, 0], ternary);

        const located = sequence.trySelectEvent(0, 0);
        expect(located).toBeDefined();
        expect(located!.eventIndexInElement).toBeLessThan(located!.elementEventCount);

        // The ternary machine must actually exercise a transition emitting more than one event,
        // otherwise the ordinal in the location above is trivially zero.
        const multiEvent = range(sequence.size).some(
            (index) => (sequence.eventRank(index + 1, 0) as number) - (sequence.eventRank(index, 0) as number) > 1,
        );
        expect(multiEvent).toBe(true);
    });

    test("randomized persistent branching keeps every retained version exact", () => {
        const random = makeRandom(0x61c7_2026);
        const versions: { sequence: ContextualRankSequence<number>; model: number[] }[] = [
            { sequence: ContextualRankSequence.empty(ternary), model: [] },
        ];

        for (let step = 0; step < 320; step++) {
            const parentIndex = random(versions.length);
            const parent = versions[parentIndex]!;
            const model = [...parent.model];
            let next: ContextualRankSequence<number>;
            const item = random(19) - 9;
            switch (random(7)) {
                case 0: model.unshift(item); next = parent.sequence.prepend(item); break;
                case 1: model.push(item); next = parent.sequence.append(item); break;
                case 2: {
                    const index = random(model.length + 1);
                    model.splice(index, 0, item);
                    next = parent.sequence.insert(index, item);
                    break;
                }
                case 3: {
                    if (model.length === 0) { next = parent.sequence; break; }
                    const index = random(model.length);
                    model[index] = item;
                    next = parent.sequence.setItem(index, item);
                    break;
                }
                case 4: {
                    if (model.length === 0) { next = parent.sequence; break; }
                    const index = random(model.length);
                    model.splice(index, 1);
                    next = parent.sequence.removeAt(index);
                    break;
                }
                case 5: {
                    const index = random(model.length + 1);
                    const count = random(model.length - index + 1);
                    next = parent.sequence.getRange(index, count);
                    model.splice(0, model.length, ...parent.model.slice(index, index + count));
                    break;
                }
                default: {
                    const suffix = versions[random(versions.length)]!;
                    if (model.length + suffix.model.length > 72) { next = parent.sequence; break; }
                    model.push(...suffix.model);
                    next = parent.sequence.concat(suffix.sequence);
                    break;
                }
            }

            assertMatchesModel(next, model, ternary);
            versions.push({ sequence: next, model });
            if (versions.length > 24) versions.splice(1 + random(versions.length - 2), 1);
        }

        for (const version of versions) assertMatchesModel(version.sequence, version.model, ternary);
    });

    test("split, range, and concat agree with array slicing", () => {
        fc.assert(
            fc.property(
                fc.array(fc.constantFrom("\"", ",", "x", "y"), { maxLength: 40 }),
                fc.nat(),
                fc.nat(),
                (word, rawIndex, rawCount) => {
                    const sequence = ContextualRankSequence.from(word, quotedComma);
                    const index = word.length === 0 ? 0 : rawIndex % (word.length + 1);
                    const count = rawCount % (word.length - index + 1);

                    const split = sequence.splitAt(index);
                    expect(split.left.toArray()).toEqual(word.slice(0, index));
                    expect(split.right.toArray()).toEqual(word.slice(index));
                    expect(split.left.concat(split.right).toArray()).toEqual(word);
                    expect(sequence.getRange(index, count).toArray()).toEqual(word.slice(index, index + count));

                    // The boundary summary is exactly the left half's whole-sequence summary.
                    for (let state = 0; state < 2; state++) {
                        expect(sequence.evaluatePrefix(index, state)).toEqual(split.left.evaluate(state));
                    }
                },
            ),
            { numRuns: 200 },
        );
    });

    test("out-of-range states and ranks are absent results, bad indices are RangeErrors", () => {
        const sequence = ContextualRankSequence.from([2, -3, 4, 0], ternary);
        const empty = ContextualRankSequence.empty(ternary);

        expect(sequence.evaluate(3)).toBeUndefined();
        expect(sequence.evaluate(-1)).toBeUndefined();
        expect(sequence.evaluatePrefix(0, 3)).toBeUndefined();
        expect(sequence.eventRank(2, 3)).toBeUndefined();
        expect(sequence.trySelectEvent(0, 3)).toBeUndefined();
        expect(sequence.trySelectEvent(-1, 0)).toBeUndefined();
        expect(sequence.trySelectEvent(1.5, 0)).toBeUndefined();
        expect(sequence.trySelectEvent(sequence.evaluate(0)!.eventCount, 0)).toBeUndefined();
        expect(empty.trySelectEvent(0, 0)).toBeUndefined();

        expect(() => sequence.get(-1)).toThrow(RangeError);
        expect(() => sequence.get(4)).toThrow(RangeError);
        expect(() => sequence.get(1.5)).toThrow(RangeError);
        expect(() => sequence.evaluatePrefix(5, 0)).toThrow(RangeError);
        expect(() => sequence.eventRank(-1, 0)).toThrow(RangeError);
        expect(() => sequence.insert(5, 1)).toThrow(RangeError);
        expect(() => sequence.setItem(4, 1)).toThrow(RangeError);
        expect(() => sequence.removeAt(4)).toThrow(RangeError);
        expect(() => sequence.splitAt(5)).toThrow(RangeError);
        expect(() => sequence.getRange(1, 4)).toThrow(RangeError);
        expect(() => sequence.getRange(-1, 0)).toThrow(RangeError);
        expect(() => sequence.getRange(0, -1)).toThrow(RangeError);

        expect(empty.isEmpty).toBe(true);
        expect(empty.evaluate(2)).toEqual({ finalState: 2, eventCount: 0 });
        expect(empty.evaluatePrefix(0, 1)).toEqual({ finalState: 1, eventCount: 0 });
        expect(empty.validateStructure()).toEqual({ count: 0, stateCount: 3, maximumTotalEventCount: 0 });
    });

    test("empty results stay usable because the machine is retained, not inferred", () => {
        const sequence = ContextualRankSequence.from([1, 2, 3], ternary);
        const emptied = sequence.getRange(0, 0);

        expect(emptied.isEmpty).toBe(true);
        expect(emptied.machine).toBe(ternary);
        expect(emptied.stateCount).toBe(3);
        expect(emptied).toBe(ContextualRankSequence.empty(ternary));
        expect(emptied.append(4).toArray()).toEqual([4]);
        expect(emptied.concat(sequence)).toBe(sequence);
        expect(sequence.concat(emptied)).toBe(sequence);
        expect(sequence.splitAt(0).left).toBe(emptied);
        expect(sequence.splitAt(3).right).toBe(emptied);
        // Draining by removal must reach the SAME canonical empty the other shrinking paths
        // return; asserting only `.isEmpty` here left `removeAt` free to publish a second,
        // structurally identical empty over the same machine.
        expect(sequence.removeAt(0).removeAt(0).removeAt(0)).toBe(emptied);
        expect(ContextualRankSequence.from([5], ternary).removeAt(0)).toBe(emptied);
    });

    test("concatenation is gated on retained machine identity", () => {
        const twin = createContextualEventMachine<number>(3, (state, element) => {
            const next = (state + Math.abs(element)) % 3;
            return { nextState: next, eventCount: next };
        });
        const mine = ContextualRankSequence.from([1, 2], ternary);
        const theirs = ContextualRankSequence.from([3, 4], twin);

        // Behaviourally identical machines, but distinct retained objects: rejected, not silently
        // reinterpreted under one of the two automata.
        expect(twin).not.toBe(ternary);
        expect(() => mine.concat(theirs as unknown as ContextualRankSequence<number>)).toThrow(TypeError);
        expect(() => mine.concat(undefined as unknown as ContextualRankSequence<number>)).toThrow(TypeError);
        expect(mine.concat(ContextualRankSequence.from([5], ternary)).toArray()).toEqual([1, 2, 5]);
        expect(ContextualRankSequence.from(mine, ternary)).toBe(mine);
        expect(ContextualRankSequence.from(mine, twin)).not.toBe(mine);
    });

    test("a machine that breaks its contract is rejected before anything is published", () => {
        expect(() => createContextualEventMachine<number>(0, () => ({ nextState: 0, eventCount: 0 }))).toThrow(RangeError);
        const stateless: ContextualEventMachine<number> = { stateCount: 0, transition: () => ({ nextState: 0, eventCount: 0 }) };
        expect(() => ContextualRankSequence.empty(stateless)).toThrow(RangeError);
        expect(() => ContextualRankSequence.empty(undefined as unknown as ContextualEventMachine<number>)).toThrow(TypeError);

        const escaping: ContextualEventMachine<number> = { stateCount: 1, transition: () => ({ nextState: 1, eventCount: 0 }) };
        const escapingEmpty = ContextualRankSequence.empty(escaping);
        expect(() => escapingEmpty.append(1)).toThrow(RangeError);
        expect(() => ContextualRankSequence.from([1], escaping)).toThrow(RangeError);
        expect(escapingEmpty.isEmpty).toBe(true);

        const negative: ContextualEventMachine<number> = { stateCount: 1, transition: () => ({ nextState: 0, eventCount: -1 }) };
        expect(() => ContextualRankSequence.empty(negative).append(1)).toThrow(RangeError);

        // A transition that throws mid-row leaves the receiver untouched, because the whole effect
        // row is built before any tree node is.
        let armed = false;
        const flaky: ContextualEventMachine<number> = {
            stateCount: 2,
            transition: (state, element) => {
                if (armed && element === 99) throw new Error("Injected transition failure.");
                return { nextState: state, eventCount: 0 };
            },
        };
        const stable = ContextualRankSequence.from([1, 2, 3], flaky);
        armed = true;
        expect(() => stable.append(99)).toThrow("Injected transition failure.");
        expect(() => stable.insert(1, 99)).toThrow("Injected transition failure.");
        expect(stable.toArray()).toEqual([1, 2, 3]);
        armed = false;
        expect(stable.append(99).toArray()).toEqual([1, 2, 3, 99]);
    });

    test("an unrepresentable composed event total fails atomically and stays retryable", () => {
        const huge: ContextualEventMachine<number> = {
            stateCount: 1,
            transition: () => ({ nextState: 0, eventCount: Number.MAX_SAFE_INTEGER }),
        };
        const source = ContextualRankSequence.from([1], huge);
        expect(source.evaluate(0)).toEqual({ finalState: 0, eventCount: Number.MAX_SAFE_INTEGER });

        // The substrate defers summary composition, so the overflowing successor is built
        // structurally and the failure surfaces at its first forced summary read. A failed force
        // publishes nothing, so the read stays retryable and fails identically every time.
        const appended = source.append(2);
        expect(() => appended.evaluate(0)).toThrow(RangeError);
        expect(() => appended.evaluate(0)).toThrow(RangeError);
        expect(appended.toArray()).toEqual([1, 2]);
        expect(source.evaluate(0)).toEqual({ finalState: 0, eventCount: Number.MAX_SAFE_INTEGER });
        expect(source.validateStructure().maximumTotalEventCount).toBe(Number.MAX_SAFE_INTEGER);
    });

    test("a stored undefined element is unambiguous", () => {
        const optional: ContextualEventMachine<string | undefined> = createContextualEventMachine<string | undefined>(
            2,
            (state, element) => (element === undefined
                ? { nextState: 1 - state, eventCount: 1 }
                : { nextState: state, eventCount: 0 }),
        );
        const sequence = ContextualRankSequence.from<string | undefined>(["a", undefined, "b"], optional);

        expect(sequence.get(1)).toBeUndefined();
        expect(sequence.toArray()).toEqual(["a", undefined, "b"]);
        expect(sequence.evaluate(0)).toEqual({ finalState: 1, eventCount: 1 });
        expect(sequence.trySelectEvent(0, 0)).toEqual({
            elementIndex: 1,
            eventIndexInElement: 0,
            stateBefore: 0,
            stateAfter: 1,
            elementEventCount: 1,
        });
        assertMatchesModel(sequence, ["a", undefined, "b"], optional);
    });

    test("derived versions share substrate structure, and unrelated ones do not", () => {
        const machine = new CountingMachine();
        const base = ContextualRankSequence.from(range(256), machine);
        const appended = base.append(-1);
        const edited = base.setItem(128, -2);

        // Two genuinely distinct values, so the assertion can actually fail.
        expect(appended).not.toBe(base);
        expect(edited).not.toBe(base);
        expect(base.sharesStructureWith(appended)).toBe(true);
        expect(base.sharesStructureWith(edited)).toBe(true);

        // Negative control: identical contents built independently share no node.
        const independent = ContextualRankSequence.from(range(256), machine);
        expect(independent.toArray()).toEqual(base.toArray());
        expect(base.sharesStructureWith(independent)).toBe(false);

        // Operations that return the receiver are identity facts, not sharing facts.
        expect(base.concat(ContextualRankSequence.empty(machine))).toBe(base);
        expect(base.getRange(0, base.size)).toBe(base);
        expect(base.splitAt(0).right).toBe(base);
        expect(base.splitAt(base.size).left).toBe(base);
    });

    test("cached rank and select never invoke the machine", () => {
        const machine = new CountingMachine();
        const sequence = ContextualRankSequence.from(range(4_096), machine);
        const total = sequence.evaluate(0)!.eventCount;
        expect(total).toBeGreaterThan(0);
        machine.reset();

        const observed = observeContextualRankWork(() => {
            let selected = 0;
            for (let boundary = 0; boundary <= sequence.size; boundary += 17) {
                expect(sequence.eventRank(boundary, boundary & 1)).toBeDefined();
            }
            for (let index = 0; index < sequence.size; index += 61) expect(sequence.get(index)).toBe(index);
            for (let eventIndex = 0; eventIndex < total; eventIndex += 31) {
                expect(sequence.trySelectEvent(eventIndex, 0)).toBeDefined();
                selected++;
            }
            return selected;
        });

        expect(observed.result).toBeGreaterThan(0);
        expect(machine.calls).toBe(0);
        expect(observed.statistics.machineTransitions).toBe(0);
    });

    test("storing one element costs exactly one effect row", () => {
        const machine = new CountingMachine();
        const base = ContextualRankSequence.from(range(1_000), machine);
        expect(machine.calls).toBe(1_000 * machine.stateCount);

        machine.reset();
        const observed = observeContextualRankWork(() => base.append(1).prepend(2).insert(500, 3).setItem(7, 4));
        expect(observed.statistics.machineTransitions).toBe(4 * machine.stateCount);
        expect(machine.calls).toBe(4 * machine.stateCount);

        machine.reset();
        expect(observeContextualRankWork(() => base.removeAt(3)).statistics.machineTransitions).toBe(0);
        expect(machine.calls).toBe(0);
    });

    test("endpoint update cost does not grow with the sequence length", () => {
        // The join-tree substrates every earlier port sat on paid Theta(s log n) table compositions
        // per endpoint push, so a 32x longer sequence cost measurably more. The lazy finger tree
        // touches only the outer digit and suspends every deeper cascade, so the immediate cost of
        // 512 pushes is *identical* at both lengths - and the machine runs exactly s times per new
        // element regardless.
        const machine = new CountingMachine();
        const compositions: number[] = [];
        const transitions: number[] = [];
        for (const count of [1_024, 32_768]) {
            const base = ContextualRankSequence.from(range(count), machine);
            const observed = observeContextualRankWork(() => {
                let current = base;
                for (let value = 0; value < 256; value++) current = current.append(value);
                for (let value = 0; value < 256; value++) current = current.prepend(value);
                return current;
            });
            expect(observed.result.size).toBe(count + 512);
            compositions.push(observed.statistics.summaryCompositions);
            transitions.push(observed.statistics.machineTransitions);
        }

        // 340 is one 2-3 node build (two compositions) per roughly three pushes - the same figure
        // the substrate's own bound probe records, and independent of length in both directions.
        expect(compositions[0]).toBe(compositions[1]);
        expect(compositions[0]).toBe(340);
        expect(transitions[0]).toBe(512 * machine.stateCount);
        expect(transitions[1]).toBe(512 * machine.stateCount);
    });

    test("query cost is logarithmic, and a warm whole-sequence read is free", () => {
        // Indexing composes no summary at all, a memoized root summary is an O(1) field read, and a
        // measure-directed descent composes O(s) tables per level: a 64x longer sequence must cost
        // barely more, never 64x more.
        const machine = new CountingMachine();
        const prefixCosts: number[] = [];
        for (const count of [4_096, 32_768, 262_144]) {
            const sequence = ContextualRankSequence.from(range(count), machine);
            expect(sequence.evaluate(0)).toBeDefined();
            expect(observeContextualRankWork(() => sequence.evaluate(0)).statistics.summaryCompositions).toBe(0);
            expect(observeContextualRankWork(() => sequence.get(count >> 1)).statistics.summaryCompositions).toBe(0);
            expect(observeContextualRankWork(() => sequence.splitAt(count >> 1)).statistics.summaryCompositions)
                .toBeLessThanOrEqual(16);
            prefixCosts.push(observeContextualRankWork(() => sequence.eventRank((count >> 1) + 1, 1)).statistics.summaryCompositions);
        }

        expect(prefixCosts[0]).toBeGreaterThan(0);
        expect(prefixCosts[2]).toBeLessThanOrEqual(64);
        expect(prefixCosts[2]).toBeLessThanOrEqual((prefixCosts[0] as number) * 3);
    });

    test("concatenation cost ignores operand asymmetry", () => {
        // O(s log(min(n, m))), not a height-difference bound: the substrate suspends its middle
        // recursion, so the immediate work is one boundary regrouping however unequal the operands.
        const machine = new CountingMachine();
        const big = ContextualRankSequence.from(range(4_096), machine);
        const costs: number[] = [];
        for (const smallCount of [1, 1_024]) {
            const small = ContextualRankSequence.from(range(smallCount), machine);
            const observed = observeContextualRankWork(() => big.concat(small));
            expect(observed.result.size).toBe(4_096 + smallCount);
            expect(observed.statistics.machineTransitions).toBe(0);
            costs.push(observed.statistics.summaryCompositions);
        }
        expect(costs[0]).toBeLessThanOrEqual(8);
        expect(costs[1]).toBeLessThanOrEqual(8);
    });

    test("deep organic push chains force without exhausting the JavaScript stack", () => {
        const machine = new CountingMachine();
        let sequence = ContextualRankSequence.empty(machine);
        for (let value = 0; value < 60_000; value++) sequence = sequence.append(value);

        expect(sequence.size).toBe(60_000);
        expect(sequence.get(0)).toBe(0);
        expect(sequence.get(59_999)).toBe(59_999);
        expect(sequence.evaluate(0)).toBeDefined();
        expect(sequence.eventRank(60_000, 0)).toBe(sequence.evaluate(0)!.eventCount);
    });

    test("validateStructure recomputes every cached summary", () => {
        const sequence = ContextualRankSequence.from(chars("\"a,b\",c,d"), quotedComma);
        expect(sequence.validateStructure()).toEqual({ count: 9, stateCount: 2, maximumTotalEventCount: 2 });
        expect(sequence.stateCount).toBe(2);

        // A machine that changes its declared width behind an existing lineage is caught.
        const mutable = { stateCount: 2, transition: (state: number): ContextualEventTransition => ({ nextState: state, eventCount: 0 }) };
        const built = ContextualRankSequence.from([1, 2, 3], mutable);
        expect(built.validateStructure().stateCount).toBe(2);
        mutable.stateCount = 3;
        expect(() => built.validateStructure()).toThrow(Error);
    });

    test("observation scopes nest and restore the enclosing counters", () => {
        const machine = new CountingMachine();
        const base = ContextualRankSequence.from(range(64), machine);
        const outer = observeContextualRankWork(() => {
            const inner = observeContextualRankWork(() => base.append(1));
            expect(inner.statistics.machineTransitions).toBe(machine.stateCount);
            return base.append(2);
        });
        expect(outer.statistics.machineTransitions).toBe(machine.stateCount);
        expect(() => observeContextualRankWork(() => { throw new Error("boom"); })).toThrow("boom");
        expect(observeContextualRankWork(() => base.append(3)).statistics.machineTransitions).toBe(machine.stateCount);
    });
});
