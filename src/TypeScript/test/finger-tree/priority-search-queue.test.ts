/**
 * Tests for the priority search queue, exercising both indexes together: key-addressed lookup and
 * re-prioritization alongside minimum-by-priority access and range pruning.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import { PrioritySearchQueue } from "../../src/finger-tree/index.js";

describe("PrioritySearchQueue", () => {
  test("minimum cache uses key order to break priority ties", () => {
    const queue = PrioritySearchQueue.from([
      { key: 4, priority: 1, value: "d" }, { key: 2, priority: 1, value: "b" }, { key: 3, priority: 9, value: "c" },
    ]);
    expect(queue.minimum).toEqual({ key: 2, priority: 1, value: "b" });
    expect(queue.enumerateAtMost(2, 4, 2)).toEqual(expect.any(Object));
    expect([...queue.enumerateAtMost(2, 4, 2)]).toEqual([
      { key: 2, priority: 1, value: "b" }, { key: 4, priority: 1, value: "d" },
    ]);
    expect(queue.validateStructure().count).toBe(3);
  });

  test("random updates and minimum drains agree with a map model", () => {
    fc.assert(fc.property(fc.array(fc.record({ key: fc.integer({ min: -100, max: 100 }), priority: fc.integer({ min: -20, max: 20 }), remove: fc.boolean() }), { maxLength: 500 }), operations => {
      let queue = PrioritySearchQueue.empty<number, number, string>();
      const model = new Map<number, { priority: number; value: string }>();
      for (const operation of operations) {
        if (operation.remove) { queue = queue.remove(operation.key); model.delete(operation.key); }
        else { queue = queue.setItem(operation.key, operation.priority, String(operation.key)); model.set(operation.key, { priority: operation.priority, value: String(operation.key) }); }
      }
      queue.validateStructure();
      const expected = [...model].sort((a, b) => a[1].priority - b[1].priority || a[0] - b[0]);
      const actual: Array<readonly [number, number]> = [];
      while (!queue.isEmpty) { const view = queue.deleteMinimum(); actual.push([view.entry.key, view.entry.priority]); queue = view.remainder; }
      expect(actual).toEqual(expected.map(([key, item]) => [key, item.priority]));
    }), { numRuns: 100 });
  });
});
