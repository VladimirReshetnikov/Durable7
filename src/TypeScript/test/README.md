# TypeScript test map

- Created (UTC): 2026-07-15T00:12:55Z
- Repository HEAD: 6bf20605073b1750d871d4bd53ef75fcfe25484c

| Area | Suites |
| --- | --- |
| HAMT | `persistent-hamt.test.ts`, `persistent-patricia.test.ts`, `merkle.test.ts` |
| Finger-tree family | `core.test.ts`, `rope-daba.test.ts`, `rrb-vector.test.ts`, `canonical-sorted-set.test.ts`, `brodal-okasaki-heap.test.ts`, `priority-search-queue.test.ts` |
| Tungsten | `tungsten.test.ts` |
| Numerics | `wide-integer.test.ts` |

Run everything with `npm test`, one file with `npx vitest run test/<path>.test.ts`, or a test-name
subset with `npx vitest run -t "pattern"`. Property suites use fixed fast-check defaults and print a
replay seed/path on failure.
