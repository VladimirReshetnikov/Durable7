/**
 * Bound probes for the measured finger-tree core.
 *
 * These are the inverted forms of the measurements that established the old join-tree core's
 * weaker bounds. Each counts the policy's `combine`/`measure` calls, because asymptotic claims are
 * invisible to correctness tests: an eager join tree would pass every behavioural case here while
 * failing every counter.
 */
import { describe, expect, test } from "vitest";
import { MeasuredSequence } from "../../src/finger-tree/measured-sequence.js";
import { PersistentDeque, ReversibleDeque } from "../../src/finger-tree/core.js";
import type { MeasurePolicy } from "../../src/finger-tree/measures.js";

/** The size measure with live call counters, so probes can see exactly what an operation paid. */
class CountingSizeMeasure implements MeasurePolicy<number, number> {
    public readonly identity = 0;
    public combineCalls = 0;
    public measureCalls = 0;
    public combine(left: number, right: number): number { this.combineCalls++; return left + right; }
    public measure(_element: number): number { this.measureCalls++; return 1; }
    public reset(): void { this.combineCalls = 0; this.measureCalls = 0; }
    public get totalCalls(): number { return this.combineCalls + this.measureCalls; }
}

/** A size measure whose combine can be armed to fail, for failure-atomicity probes. */
class FailingSizeMeasure implements MeasurePolicy<number, number> {
    public readonly identity = 0;
    public armed = false;
    public combine(left: number, right: number): number {
        if (this.armed) throw new Error("Injected combine failure.");
        return left + right;
    }
    public measure(_element: number): number { return 1; }
}

/** A non-commutative measure: concatenation in sequence order spells the sequence out. */
class SpellingMeasure implements MeasurePolicy<string, string> {
    public readonly identity = "";
    public combine(left: string, right: string): string { return left + right; }
    public measure(element: string): string { return element; }
}

function range(count: number): number[] { return Array.from({ length: count }, (_, index) => index); }

function buildByAppends(count: number, policy: MeasurePolicy<number, number>): MeasuredSequence<number, number> {
    let sequence = MeasuredSequence.empty(policy);
    for (let value = 0; value < count; value++) sequence = sequence.append(value);
    return sequence;
}

describe("measured finger-tree bound probes", () => {
    test("endpoint push cost is independent of length", () => {
        // The join tree paid Theta(log n) combines per push; the finger tree's immediate work
        // touches only the outer digit and suspends every deeper cascade: one 2-3 node build (two
        // combines) per roughly three pushes, exactly 340 combines for 512 pushes, and the 32x
        // larger sequence must not change the count at all.
        const costs: number[] = [];
        for (const count of [1_024, 32_768]) {
            const policy = new CountingSizeMeasure();
            let sequence = MeasuredSequence.from(range(count), policy);
            policy.reset();
            for (let value = 0; value < 256; value++) sequence = sequence.append(value);
            for (let value = 0; value < 256; value++) sequence = sequence.prepend(value);
            costs.push(policy.combineCalls);
            expect(sequence.size).toBe(count + 512);
        }
        expect(costs[0]).toBe(costs[1]);
        expect(costs[0]).toBe(340);
    });

    test("front and back are constant digit reads", () => {
        const policy = new CountingSizeMeasure();
        const sequence = MeasuredSequence.from(range(8_192), policy);
        const grown = sequence.append(8_192).prepend(-1);
        policy.reset();
        expect(sequence.front()).toBe(0);
        expect(sequence.back()).toBe(8_191);
        expect(grown.front()).toBe(-1);
        expect(grown.back()).toBe(8_192);
        expect(sequence.size).toBe(8_192);
        expect(grown.size).toBe(8_194);
        expect(policy.totalCalls).toBe(0);
    });

    test("concat immediate cost ignores operand asymmetry", () => {
        // Concatenation defers its middle recursion, so the immediate cost is one digit
        // regrouping regardless of how unequal the operands are. The join tree's concat walked
        // the taller operand's spine - its cost grew with the height difference.
        const policy = new CountingSizeMeasure();
        const big = MeasuredSequence.from(range(4_096), policy);
        const costs: number[] = [];
        for (const smallCount of [1, 1_024]) {
            const small = MeasuredSequence.from(range(smallCount), policy);
            policy.reset();
            const joined = big.concat(small);
            costs.push(policy.combineCalls);
            expect(joined.size).toBe(4_096 + smallCount);
        }
        expect(costs[0]).toBe(0);
        expect(costs[1]).toBe(4);
    });

    test("forced spines are memoized across persistent branches", () => {
        // Work deferred by one version and forced by another must never be repeated. This is the
        // property that makes the amortized bounds valid under persistence: the suspended spine
        // is shared, so the first full-measure force pays for everyone, and each sibling branch
        // re-pays only its own unshared root digits.
        const policy = new CountingSizeMeasure();
        const base = buildByAppends(10_000, policy);
        const branches = range(49).map((value) => base.append(value));
        policy.reset();

        expect(branches[0]!.measure).toBe(10_001);
        const firstForce = policy.combineCalls;
        policy.reset();
        for (const branch of branches.slice(1)) expect(branch.measure).toBe(10_001);
        const otherForces = policy.combineCalls;

        expect(firstForce).toBeGreaterThan(2_000);
        expect(otherForces).toBeLessThan(firstForce / 3);
        expect(otherForces / 48).toBeLessThan(200);
    });

    test("deep pending chains force iteratively", () => {
        // Organic append loops build Theta(n)-deep pending chains; forcing walks them with an
        // explicit stack, so a 200,000-link chain forces without a stack overflow.
        const policy = new CountingSizeMeasure();
        const appended = buildByAppends(200_000, policy);
        expect(appended.measure).toBe(200_000);
        expect(appended.front()).toBe(0);
        expect(appended.back()).toBe(199_999);
        expect(appended.at(123_456)).toBe(123_456);
        let iterated = 0;
        for (const _value of appended) iterated++;
        expect(iterated).toBe(200_000);

        let prepended = MeasuredSequence.empty(policy);
        for (let value = 0; value < 100_000; value++) prepended = prepended.prepend(value);
        expect(prepended.measure).toBe(100_000);
        expect(prepended.front()).toBe(99_999);
    });

    test("failed forces publish nothing and are retryable", () => {
        // A deferred computation that throws publishes nothing: the failed force is retryable
        // (both reads throw, proving the first failure memoized no poison value) and the source
        // stays intact and fully usable once the failure clears.
        const policy = new FailingSizeMeasure();
        const sequence = buildByAppends(100, policy);
        policy.armed = true;
        expect(() => sequence.measure).toThrow("Injected combine failure.");
        expect(() => sequence.measure).toThrow("Injected combine failure.");
        policy.armed = false;
        expect(sequence.measure).toBe(100);
        expect(sequence.toArray()).toEqual(range(100));
    });

    test("pending suspensions still reveal structural sharing", () => {
        // Endpoint edits defer their overflow work, so the shared structure hides behind pending
        // suspensions; the identity walk must force to see it. On an already-forced spine the
        // probe costs zero policy calls: probing cannot re-add work.
        const policy = new CountingSizeMeasure();
        const base = MeasuredSequence.from(range(256), policy);
        const appended = base.append(256).append(257);
        const prepended = base.prepend(-1).prepend(-2);
        expect(base.sharesStructureWith(appended)).toBe(true);
        expect(base.sharesStructureWith(prepended)).toBe(true);
        expect(appended.sharesStructureWith(prepended)).toBe(true);

        const warmed = buildByAppends(4_096, policy);
        const branch = warmed.append(-1);
        expect(warmed.measure).toBe(4_096);
        expect(branch.measure).toBe(4_097);
        policy.reset();
        expect(branch.sharesStructureWith(warmed)).toBe(true);
        expect(policy.totalCalls).toBe(0);
        const independent = buildByAppends(4_096, policy);
        expect(independent.sharesStructureWith(warmed)).toBe(false);
    });

    test("randomized lazy edits match an array model", () => {
        let state = 0x0feed5eed0451n;
        const nextInt = (bound: number): number => {
            state = (state * 6364136223846793005n + 1442695040888963407n) & 0xffffffffffffffffn;
            return bound === 0 ? 0 : Number((state >> 33n) % BigInt(bound));
        };
        const policy = new CountingSizeMeasure();
        let sequence = MeasuredSequence.empty(policy);
        let model: number[] = [];
        for (let round = 0; round < 600; round++) {
            switch (nextInt(8)) {
                case 0: {
                    const value = nextInt(1_000);
                    sequence = sequence.append(value);
                    model.push(value);
                    break;
                }
                case 1: {
                    const value = nextInt(1_000);
                    sequence = sequence.prepend(value);
                    model.unshift(value);
                    break;
                }
                case 2: {
                    if (model.length === 0) break;
                    const index = nextInt(model.length);
                    sequence = sequence.removeAt(index)!;
                    model.splice(index, 1);
                    break;
                }
                case 3: {
                    if (model.length === 0) break;
                    const index = nextInt(model.length);
                    const value = nextInt(1_000);
                    sequence = sequence.setAt(index, value)!;
                    model[index] = value;
                    break;
                }
                case 4: {
                    const index = nextInt(model.length + 1);
                    const split = sequence.splitAt(index)!;
                    expect(split.left.toArray()).toEqual(model.slice(0, index));
                    expect(split.right.toArray()).toEqual(model.slice(index));
                    sequence = split.left.concat(split.right);
                    break;
                }
                case 5: {
                    const batch = range(nextInt(40));
                    sequence = sequence.concat(MeasuredSequence.from(batch, policy));
                    model = [...model, ...batch];
                    break;
                }
                case 6: {
                    const index = nextInt(model.length + 1);
                    const value = nextInt(1_000);
                    sequence = sequence.insertAt(index, value)!;
                    model.splice(index, 0, value);
                    break;
                }
                default: {
                    const count = nextInt(model.length + 1);
                    expect(sequence.prefixMeasure(count)).toBe(count);
                    break;
                }
            }
            expect(sequence.size).toBe(model.length);
            expect(sequence.front()).toBe(model.length === 0 ? undefined : model[0]);
            expect(sequence.back()).toBe(model.length === 0 ? undefined : model[model.length - 1]);
            if (model.length !== 0) {
                const probe = nextInt(model.length);
                expect(sequence.at(probe)).toBe(model[probe]);
            }
            if (round % 64 === 0) expect(sequence.toArray()).toEqual(model);
        }
        expect(sequence.toArray()).toEqual(model);
        expect(sequence.measure).toBe(model.length);
    });
});

describe("reversed-view probes", () => {
    test("reversing is lazy and correct", () => {
        // Mirroring defers the middle's reversal: zero immediate policy calls, full order on
        // read. The plain deque's reverse() was previously a Theta(n) rebuild.
        const policy = new CountingSizeMeasure();
        const sequence = MeasuredSequence.from(range(16_384), policy);
        expect(sequence.measure).toBe(16_384);
        policy.reset();
        const mirrored = sequence.reversedView();
        expect(policy.totalCalls).toBe(0);
        expect(mirrored.front()).toBe(16_383);
        expect(mirrored.back()).toBe(0);
        expect(mirrored.at(0)).toBe(16_383);
        expect(mirrored.at(16_383)).toBe(0);
        expect(mirrored.toArray()).toEqual(range(16_384).reverse());
    });

    test("reverse of reverse is the original spine by cell identity", () => {
        // Reversing a still-pending reversal short-circuits to the original suspension cell, so
        // the double mirror shares (and memoizes through) the very cells the source owns.
        const policy = new CountingSizeMeasure();
        const sequence = MeasuredSequence.from(range(4_096), policy);
        expect(sequence.measure).toBe(4_096);
        const restored = sequence.reversedView().reversedView();
        expect(restored.toArray()).toEqual(range(4_096));
        expect(restored.sharesStructureWith(sequence)).toBe(true);
    });

    test("cross-orientation concat cost tracks the smaller operand, not the pair", () => {
        // The reversible deque's mismatched concat must join structurally, not materialize:
        // mirror-then-concat of a 16384-element sequence with a small one costs the same handful
        // of immediate policy calls as a same-orientation concat - the Theta(n + m)
        // materialization this replaced would pay tens of thousands.
        const policy = new CountingSizeMeasure();
        const big = MeasuredSequence.from(range(16_384), policy);
        expect(big.measure).toBe(16_384);
        const costs: number[] = [];
        for (const smallCount of [1, 1_024]) {
            const small = MeasuredSequence.from(range(smallCount), policy);
            expect(small.measure).toBe(smallCount);
            policy.reset();
            const joined = big.concat(small.reversedView());
            costs.push(policy.totalCalls);
            expect(joined.size).toBe(16_384 + smallCount);
            expect(joined.at(16_384 + smallCount - 1)).toBe(0);
        }
        expect(costs[0]!).toBeLessThanOrEqual(40);
        expect(costs[1]!).toBeLessThanOrEqual(40);
    });

    test("non-commutative measures stay correct under reversal", () => {
        // A reversed node's measure is not the mirror of its cached measure unless the monoid
        // commutes; the reversal recombines each mirrored node's summary in reversed order, so a
        // spelling measure must spell the reversed sequence exactly.
        const policy = new SpellingMeasure();
        const letters = Array.from("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
        let sequence = MeasuredSequence.from(letters.slice(0, 40), policy);
        for (const letter of letters.slice(40)) sequence = sequence.append(letter);
        const spelled = letters.join("");
        expect(sequence.measure).toBe(spelled);

        const mirrored = sequence.reversedView();
        const reversedSpelling = [...letters].reverse().join("");
        expect(mirrored.measure).toBe(reversedSpelling);
        expect(mirrored.toArray().join("")).toBe(reversedSpelling);
        expect(mirrored.prefixMeasure(10)).toBe(reversedSpelling.slice(0, 10));
        const split = mirrored.splitAt(17)!;
        expect(split.left.measure).toBe(reversedSpelling.slice(0, 17));
        expect(split.right.measure).toBe(reversedSpelling.slice(17));
        expect(mirrored.reversedView().measure).toBe(spelled);
        expect(sequence.concat(mirrored).measure).toBe(spelled + reversedSpelling);
    });

    test("deque facades reverse and cross-concat structurally", () => {
        const deque = PersistentDeque.from(range(4_096));
        const mirrored = deque.reverse();
        expect(mirrored.front()).toBe(4_095);
        expect(mirrored.back()).toBe(0);
        expect(mirrored.get(1_000)).toBe(3_095);
        expect(mirrored.reverse().toArray()).toEqual(deque.toArray());

        const forward = ReversibleDeque.from(range(2_048));
        const backward = ReversibleDeque.from(range(64)).reverse();
        const joined = forward.concat(backward);
        expect(joined.size).toBe(2_112);
        expect(joined.toArray()).toEqual([...range(2_048), ...range(64).reverse()]);
        expect(joined.sharesStorageWith(forward)).toBe(true);
        const mixed = backward.concat(forward);
        expect(mixed.toArray()).toEqual([...range(64).reverse(), ...range(2_048)]);
        expect(mixed.sharesStorageWith(forward)).toBe(true);
    });
});
