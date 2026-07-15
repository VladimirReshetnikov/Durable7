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

High-risk contracts receive direct executable coverage:

- retained immutable snapshots, no-op identity, collision representatives, transient consumption,
  and generated CHAMP/Patricia histories;
- RRB 32-way boundary shapes, concatenation/slicing, retained versions, and structural validation;
- deterministic zip-zip ranks/topology, heap drains, priority-search winner caches, interval pruning,
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
