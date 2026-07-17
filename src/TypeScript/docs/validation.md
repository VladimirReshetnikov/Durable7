# TypeScript validation

- Created (UTC): 2026-07-15T00:12:55Z
- Repository HEAD: 6bf20605073b1750d871d4bd53ef75fcfe25484c
- Scope: build, test, package, and interoperability validation

Run the complete workspace gate from `src/TypeScript`:

```powershell
npm ci
npm run validate
npm pack --dry-run
```

The gate requires TypeScript strict mode with unchecked-index, exact-optional, unused-code, and
isolated-declaration checks; all Vitest examples and fast-check histories; a clean ESM/declaration
build; and a package manifest containing only documented output.

The checked-in launcher limits npm registry concurrency and native helper builds, while Vitest is
pinned to one worker and disables file-level and in-file test concurrency. Validation therefore
never fans out into parallel test processes.

The 2026-07-17 complete serialized gate passes strict checking, 36/36 Vitest files and 242/242 tests,
the clean declaration/ESM build, and `npm pack --dry-run`. This aggregate includes 31/31 new focused
tests for the ordered multimap, map patch, directed graph, indexed map, and chunked bit set. No
benchmark is part of this gate; local performance runs remain postponed until they can execute in
isolation.

High-risk contracts receive direct executable coverage:

- retained immutable snapshots, no-op identity, collision representatives, one-descent map factory
  updates, detached reusable bulk-builder freezes, hash-bag multiplicity/algebra models, strict
  multimap contraction/counts, relation global representatives and inverse indexes, strict bimap
  two-domain conflicts, presence-safe strict patch application/inversion/composition, directed
  adjacency and reversal, selector-atomic secondary indexing, policy-driven replacement, cached inverse identity, retained models, all six
  transient-set relations, transient consumption, and generated CHAMP/Patricia histories;
- RRB 32-way boundary shapes, concatenation/slicing, retained versions, and structural validation;
- range-update algebra laws, noncommutative ordered measures, lazy composition direction, exhaustive
  split/slice/range boundaries, edits through pending tags, branching array models, failpoint sweeps,
  structural sharing, deterministic operation counters, and undefined-safe snapshot iteration;
- independent ordered-set, ordered-map, and grouped ordered-multimap representative/order invariants, exhaustive movement/range/relation
  boundaries, repeated sparse-label relabel histories, eager receiver-policy algebra failures,
  retained branches, and generated comparer-aware command models;
- deterministic zip-zip ranks/topology, heap drains, priority-search winner caches, interval-tree and
  payload interval-map pruning, sparse-bit-set rank/select/algebra,
  rope cursor branching, and 10,000-operation noncommutative DABA churn;
- cross-language golden `MST2` blocks and roots, strict codecs, tamper/conflict rejection, verified
  save/load/import, iterative synchronization, canonical membership/absence/range proofs, and
  present-null three-way merge;
- fixed-width arithmetic, byte order, parsing/formatting, signed overflow edges, and fast-check
  differential models for all six integer widths; and
- generated Tungsten list/association histories, key representative retention, sparse stamps, and
  relabel behavior.

`npm run check`, `npm test`, and `npm run build` remain available as focused gates. Generated `dist`,
`node_modules`, and TypeScript build-info files are ignored by Git.
