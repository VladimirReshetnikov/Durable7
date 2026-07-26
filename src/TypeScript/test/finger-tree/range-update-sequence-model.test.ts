/**
 * Compares the range-update sequence against an eager reference model under generated edit
 * histories.
 */
import fc from "fast-check";
import { describe, expect, it } from "vitest";
import { RangeUpdateSequence } from "../../src/finger-tree/range-update-sequence.js";
import {
    applyAffine,
    SumAffineAlgebra,
    type AffineTag,
} from "./range-update-test-support.js";

interface ModelCommand {
    readonly kind: number;
    readonly source: number;
    readonly other: number;
    readonly first: number;
    readonly second: number;
    readonly value: number;
    readonly multiply: number;
    readonly add: number;
}

const commandArbitrary: fc.Arbitrary<ModelCommand> = fc.record({
    kind: fc.integer({ min: 0, max: 9 }),
    source: fc.integer({ min: 0, max: 10_000 }),
    other: fc.integer({ min: 0, max: 10_000 }),
    first: fc.integer({ min: -200, max: 200 }),
    second: fc.integer({ min: -200, max: 200 }),
    value: fc.integer({ min: -50, max: 50 }),
    multiply: fc.constantFrom(-1, 0, 1),
    add: fc.integer({ min: -10, max: 10 }),
});
function select(value: number, count: number): number {
    return count === 0 ? 0 : Math.abs(value) % count;
}

function rangeFrom(command: ModelCommand, length: number): { readonly index: number; readonly count: number } {
    const index = select(command.first, length + 1);
    const count = select(command.second, length - index + 1);
    return { index, count };
}

describe("RangeUpdateSequence generated branching model", () => {
    it("matches retained array versions across mixed operations", () => {
        fc.assert(fc.property(
            fc.array(commandArbitrary, { minLength: 1, maxLength: 80 }),
            commands => {
                const algebra = new SumAffineAlgebra();
                const initial = [3, 1, 4, 1, 5, 9, 2, 6];
                const versions: RangeUpdateSequence<number, number, AffineTag>[] = [
                    RangeUpdateSequence.from(initial, algebra),
                ];
                const models: number[][] = [initial];

                for (const command of commands) {
                    const sourceIndex = select(command.source, versions.length);
                    const source = versions[sourceIndex] as RangeUpdateSequence<number, number, AffineTag>;
                    const sourceModel = models[sourceIndex] as number[];
                    let result = source;
                    let model = sourceModel.slice();

                    switch (command.kind) {
                        case 0:
                            result = source.append(command.value);
                            model.push(command.value);
                            break;

                        case 1:
                            result = source.prepend(command.value);
                            model.unshift(command.value);
                            break;

                        case 2: {
                            const index = select(command.first, model.length + 1);
                            result = source.insert(index, command.value);
                            model.splice(index, 0, command.value);
                            break;
                        }

                        case 3:
                            if (model.length === 0) {
                                result = source.append(command.value);
                                model.push(command.value);
                            } else {
                                const index = select(command.first, model.length);
                                result = source.setItem(index, command.value);
                                model[index] = command.value;
                            }
                            break;

                        case 4:
                            if (model.length !== 0) {
                                const index = select(command.first, model.length);
                                result = source.removeAt(index);
                                model.splice(index, 1);
                            }
                            break;

                        case 5: {
                            const range = rangeFrom(command, model.length);
                            const tag: AffineTag = { multiply: command.multiply, add: command.add };
                            result = source.applyRange(range.index, range.count, tag);
                            for (let index = range.index; index < range.index + range.count; index++) {
                                model[index] = applyAffine(tag, model[index] as number);
                            }
                            break;
                        }

                        case 6: {
                            const index = select(command.first, model.length + 1);
                            const split = source.splitAt(index);
                            result = split.left.concat(split.right);
                            break;
                        }

                        case 7: {
                            const range = rangeFrom(command, model.length);
                            result = source.getRange(range.index, range.count);
                            model = model.slice(range.index, range.index + range.count);
                            break;
                        }

                        case 8: {
                            const otherIndex = select(command.other, versions.length);
                            const other = versions[otherIndex] as RangeUpdateSequence<number, number, AffineTag>;
                            const otherModel = models[otherIndex] as number[];
                            if (model.length + otherModel.length <= 128) {
                                result = source.concat(other);
                                model.push(...otherModel);
                            }
                            break;
                        }

                        case 9: {
                            const range = rangeFrom(command, model.length);
                            const expected = model.slice(range.index, range.index + range.count)
                                .reduce((sum, value) => sum + value, 0);
                            expect(source.measureRange(range.index, range.count)).toBe(expected);
                            break;
                        }

                        default:
                            throw new Error(`Unknown generated command ${command.kind}.`);
                    }

                    expect(result.toArray()).toEqual(model);
                    expect(result.measure).toBe(model.reduce((sum, value) => sum + value, 0));
                    result.validateStructure();
                    expect(source.toArray()).toEqual(sourceModel);

                    versions.push(result);
                    models.push(model);
                }

                for (let index = 0; index < versions.length; index++) {
                    expect((versions[index] as RangeUpdateSequence<number, number, AffineTag>).toArray())
                        .toEqual(models[index]);
                }
            },
        ), { numRuns: 100 });
    });
});
