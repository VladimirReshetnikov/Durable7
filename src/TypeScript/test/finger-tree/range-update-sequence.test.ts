import { describe, expect, it } from "vitest";
import type { RangeUpdateAlgebra } from "../../src/finger-tree/range-update-algebra.js";
import { RangeUpdateSequence } from "../../src/finger-tree/range-update-sequence.js";
import {
    addTag,
    FailingAffineAlgebra,
    identityAffineTag,
    SumAffineAlgebra,
} from "./range-update-test-support.js";

class CountingAlgebra extends SumAffineAlgebra {
    public measureCalls = 0;
    public identityCalls = 0;

    public override measure(element: number): number {
        this.measureCalls++;
        return super.measure(element);
    }

    public override isIdentity(tag: { readonly multiply: number; readonly add: number }): boolean {
        this.identityCalls++;
        return super.isIdentity(tag);
    }
}

type OptionalTag = number | undefined;

class UndefinedTagAlgebra implements RangeUpdateAlgebra<number, number, OptionalTag> {
    public readonly identity = 0;
    public readonly identityTag = 0;

    private delta(tag: OptionalTag): number { return tag === undefined ? 10 : tag; }
    private encode(delta: number): OptionalTag { return delta === 10 ? undefined : delta; }

    public combine(left: number, right: number): number { return left + right; }
    public measure(element: number): number { return element; }
    public isIdentity(tag: OptionalTag): boolean { return this.delta(tag) === 0; }
    public compose(newer: OptionalTag, older: OptionalTag): OptionalTag {
        return this.encode(this.delta(newer) + this.delta(older));
    }
    public applyElement(tag: OptionalTag, element: number): number { return element + this.delta(tag); }
    public applyMeasure(tag: OptionalTag, measure: number, count: number): number {
        return measure + this.delta(tag) * count;
    }
}

interface OptionalMeasure {
    readonly sum: number;
    readonly definedCount: number;
}

class OptionalElementAlgebra
implements RangeUpdateAlgebra<number | undefined, OptionalMeasure, number> {
    public readonly identity: OptionalMeasure = { sum: 0, definedCount: 0 };
    public readonly identityTag = 0;
    public combine(left: OptionalMeasure, right: OptionalMeasure): OptionalMeasure {
        return { sum: left.sum + right.sum, definedCount: left.definedCount + right.definedCount };
    }
    public measure(element: number | undefined): OptionalMeasure {
        return element === undefined
            ? { sum: 0, definedCount: 0 }
            : { sum: element, definedCount: 1 };
    }
    public isIdentity(tag: number): boolean { return tag === 0; }
    public compose(newer: number, older: number): number { return newer + older; }
    public applyElement(tag: number, element: number | undefined): number | undefined {
        return element === undefined ? undefined : element + tag;
    }
    public applyMeasure(tag: number, measure: OptionalMeasure, _count: number): OptionalMeasure {
        return { sum: measure.sum + tag * measure.definedCount, definedCount: measure.definedCount };
    }
}

describe("RangeUpdateSequence", () => {
    it("constructs minimal-height measured sequences and retains the exact algebra", () => {
        const algebra = new SumAffineAlgebra();
        for (let size = 0; size <= 64; size++) {
            const values = Array.from({ length: size }, (_, index) => index - 7);
            const sequence = RangeUpdateSequence.create(values, algebra);
            expect(sequence.algebra).toBe(algebra);
            expect(sequence.size).toBe(size);
            expect(sequence.count).toBe(size);
            expect(sequence.isEmpty).toBe(size === 0);
            expect(sequence.measure).toBe(values.reduce((sum, value) => sum + value, 0));
            expect(sequence.toArray()).toEqual(values);

            const statistics = sequence.validateStructure();
            expect(statistics.count).toBe(size);
            expect(statistics.nodeCount).toBe(size);
            expect(statistics.maximumAbsoluteBalanceFactor).toBeLessThanOrEqual(1);
            if (size > 0) {
                expect(statistics.height).toBe(Math.ceil(Math.log2(size + 1)));
            }
        }
    });

    it("uses one canonical empty facade per exact algebra object", () => {
        const firstAlgebra = new SumAffineAlgebra();
        const secondAlgebra = new SumAffineAlgebra();
        const first = RangeUpdateSequence.empty(firstAlgebra);

        expect(RangeUpdateSequence.empty(firstAlgebra)).toBe(first);
        expect(RangeUpdateSequence.create([], firstAlgebra)).toBe(first);
        expect(RangeUpdateSequence.from([], firstAlgebra)).toBe(first);
        expect(first.measure).toBe(0);
        const nonempty = RangeUpdateSequence.from([1], firstAlgebra);
        expect(first.concat(nonempty)).toBe(nonempty);
        expect(nonempty.concat(first)).toBe(nonempty);
        expect(RangeUpdateSequence.empty(secondAlgebra)).not.toBe(first);
        expect(() => first.concat(RangeUpdateSequence.empty(secondAlgebra))).toThrow(TypeError);
    });

    it("eagerly enumerates once before measuring and recognizes exact sequence sources", () => {
        const algebra = new CountingAlgebra();
        let enumerations = 0;
        const source: Iterable<number> = {
            *[Symbol.iterator](): Generator<number, void, unknown> {
                enumerations++;
                yield 3;
                yield 5;
                yield 8;
            },
        };

        const sequence = RangeUpdateSequence.createRange(source, algebra);
        expect(enumerations).toBe(1);
        expect(algebra.measureCalls).toBe(3);
        expect(RangeUpdateSequence.from(sequence, algebra)).toBe(sequence);
        expect(enumerations).toBe(1);

        const throwingAlgebra = new CountingAlgebra();
        function* throwingSource(): Generator<number, void, unknown> {
            yield 1;
            throw new Error("source failed");
        }
        expect(() => RangeUpdateSequence.from(throwingSource(), throwingAlgebra)).toThrow("source failed");
        expect(throwingAlgebra.measureCalls).toBe(0);
    });

    it("rejects sparse direct arrays before policy callbacks", () => {
        const algebra = new CountingAlgebra();
        const sparse = new Array<number>(3);
        sparse[2] = 4;
        expect(() => RangeUpdateSequence.create(sparse, algebra)).toThrow(TypeError);
        expect(algebra.measureCalls).toBe(0);
    });

    it("implements every split and slice boundary with identity retention", () => {
        const algebra = new SumAffineAlgebra();
        for (let size = 0; size <= 16; size++) {
            const values = Array.from({ length: size }, (_, index) => index * 3 - 5);
            const sequence = RangeUpdateSequence.from(values, algebra);
            for (let index = 0; index <= size; index++) {
                const split = sequence.splitAt(index);
                expect(split.left.toArray()).toEqual(values.slice(0, index));
                expect(split.right.toArray()).toEqual(values.slice(index));
                expect(split.left.concat(split.right).toArray()).toEqual(values);
                if (index === 0) expect(split.right).toBe(sequence);
                if (index === size) expect(split.left).toBe(sequence);
            }

            for (let index = 0; index <= size; index++) {
                for (let count = 0; count <= size - index; count++) {
                    const range = sequence.getRange(index, count);
                    expect(range.toArray()).toEqual(values.slice(index, index + count));
                    if (count === 0) expect(range).toBe(RangeUpdateSequence.empty(algebra));
                    if (count === size) expect(range).toBe(sequence);
                }
            }
        }
    });

    it("implements point edits without letting earlier tags transform new values", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from([1, 2, 3, 4], algebra).applyRange(0, 4, addTag(10));

        expect(source.prepend(90).toArray()).toEqual([90, 11, 12, 13, 14]);
        expect(source.append(91).toArray()).toEqual([11, 12, 13, 14, 91]);
        expect(source.insert(2, 92).toArray()).toEqual([11, 12, 92, 13, 14]);
        expect(source.setItem(1, 93).toArray()).toEqual([11, 93, 13, 14]);
        expect(source.removeAt(2).toArray()).toEqual([11, 12, 14]);
        expect(source.toArray()).toEqual([11, 12, 13, 14]);

        const equalReplacement = source.setItem(0, 11);
        expect(equalReplacement).not.toBe(source);
        expect(equalReplacement.toArray()).toEqual(source.toArray());
        equalReplacement.validateStructure();
    });

    it("validates index and range arguments in the specified order before callbacks", () => {
        const algebra = new CountingAlgebra();
        const sequence = RangeUpdateSequence.from([1, 2, 3], algebra);
        algebra.identityCalls = 0;

        for (const index of [-1, 3, 1.5, Number.NaN]) {
            expect(() => sequence.get(index)).toThrow(RangeError);
            expect(() => sequence.setItem(index, 0)).toThrow(RangeError);
            expect(() => sequence.removeAt(index)).toThrow(RangeError);
        }
        for (const index of [-1, 4, 1.5, Number.POSITIVE_INFINITY]) {
            expect(() => sequence.insert(index, 0)).toThrow(RangeError);
            expect(() => sequence.splitAt(index)).toThrow(RangeError);
        }

        expect(() => sequence.applyRange(-1, -1, identityAffineTag)).toThrow(/Index/);
        expect(() => sequence.applyRange(0, -1, identityAffineTag)).toThrow(/Count/);
        expect(() => sequence.applyRange(4, -1, identityAffineTag)).toThrow(/Count/);
        expect(() => sequence.applyRange(4, 0, identityAffineTag)).toThrow(/Index/);
        expect(() => sequence.applyRange(2, 2, identityAffineTag)).toThrow(/Count/);
        expect(algebra.identityCalls).toBe(0);
    });

    it("makes empty range operations callback-free and recognized identities source-retaining", () => {
        const algebra = new FailingAffineAlgebra();
        const sequence = RangeUpdateSequence.from([2, 4, 6], algebra);
        algebra.arm(1);

        expect(sequence.applyRange(1, 0, addTag(100))).toBe(sequence);
        expect(sequence.measureRange(2, 0)).toBe(0);
        expect(algebra.calls).toBe(0);

        algebra.disarm();
        expect(sequence.applyRange(0, sequence.size, { multiply: 1, add: 0 })).toBe(sequence);
        expect(sequence.toArray()).toEqual([2, 4, 6]);
    });

    it("does not use undefined as a value or pending-tag sentinel", () => {
        const elementAlgebra = new OptionalElementAlgebra();
        const elements = RangeUpdateSequence.from<number | undefined, OptionalMeasure, number>(
            [undefined, 2, undefined, 4],
            elementAlgebra,
        );
        expect(elements.get(0)).toBeUndefined();
        expect(elements.toArray()).toEqual([undefined, 2, undefined, 4]);
        expect(elements.applyRange(0, 4, 3).toArray()).toEqual([undefined, 5, undefined, 7]);

        const tagAlgebra = new UndefinedTagAlgebra();
        const source = RangeUpdateSequence.from([1, 2, 3], tagAlgebra);
        const tagged = source.applyRange(0, 3, undefined);
        expect(tagged.toArray()).toEqual([11, 12, 13]);
        expect(tagged.measure).toBe(36);
        const cancelled = tagged.applyRange(0, 3, -10);
        expect(cancelled.toArray()).toEqual([1, 2, 3]);
        expect(cancelled.validateStructure().pendingTagNodeCount).toBe(0);
    });

    it("exports the intended range API without named assign/add shortcuts", () => {
        const names = new Set(Object.getOwnPropertyNames(RangeUpdateSequence.prototype));
        for (const expected of [
            "get",
            "prepend",
            "append",
            "insert",
            "setItem",
            "removeAt",
            "concat",
            "splitAt",
            "getRange",
            "applyRange",
            "measureRange",
            "validateStructure",
            "sharedNodeCount",
        ]) {
            expect(names.has(expected)).toBe(true);
        }
        expect(names.has("addRange")).toBe(false);
        expect(names.has("assignRange")).toBe(false);
    });
});
