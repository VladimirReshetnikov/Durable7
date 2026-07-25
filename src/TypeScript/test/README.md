# TypeScript test map

- Created (UTC): 2026-07-15T00:12:55Z
- Repository HEAD: 6bf20605073b1750d871d4bd53ef75fcfe25484c

| Area | Suites |
| --- | --- |
| HAMT | `persistent-hamt.test.ts`, `persistent-hash-map-factory-updates.test.ts`, `persistent-hamt-bulk-builder.test.ts`, `persistent-hash-bag.test.ts`, `persistent-hash-multimap.test.ts`, `persistent-relation.test.ts`, `persistent-bi-map.test.ts`, `persistent-map-patch.test.ts`, `persistent-directed-graph.test.ts`, `persistent-indexed-map.test.ts`, `transient-hash-set-relations.test.ts`, `concurrent-hash-trie.test.ts`, `persistent-patricia.test.ts`, `merkle.test.ts` |
| Finger-tree family | `core.test.ts`, `persistent-interval-map.test.ts`, `persistent-chunked-bit-set.test.ts`, `rope-daba.test.ts`, `rrb-vector.test.ts`, `canonical-sorted-set.test.ts`, `brodal-okasaki-heap.test.ts`, `priority-search-queue.test.ts`, `range-update-algebra.test.ts`, `range-update-sequence.test.ts`, `range-update-sequence-lazy.test.ts`, `range-update-sequence-model.test.ts`, `range-update-sequence-failure.test.ts`, `range-update-sequence-diagnostics.test.ts`, `range-update-sequence-iterator.test.ts` |
| Ordered collections | `persistent-ordered-map.test.ts`, `persistent-ordered-multimap.test.ts`, `persistent-ordered-set.test.ts`, `persistent-ordered-set-algebra.test.ts`, `persistent-ordered-set-property.test.ts` |
| Tungsten | `tungsten.test.ts` |

Run everything with `npm test`, one file with `npx vitest run test/<path>.test.ts`, or a test-name
subset with `npx vitest run -t "pattern"`. Property suites use fixed fast-check defaults and print a
replay seed/path on failure.
