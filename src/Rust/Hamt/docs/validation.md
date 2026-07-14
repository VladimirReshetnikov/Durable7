# Rust HAMT Validation

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers validating the Rust HAMT workspace
- Scope: Cargo commands, warning policy, and test coverage

Run from `src/Rust`:

```powershell
.\test.ps1 -Workspace Hamt
```

The wrapper locates Cargo on `PATH` or under the default rustup profile and applies inherited,
non-interactive Windows error handling before Cargo starts the test executable.

The crate uses `#![forbid(unsafe_code)]`. HAMT and Patricia unit tests are inline in
`Hamt/src/lib.rs` and `Hamt/src/patricia.rs`; Merkle core/wire and persistence integration tests
live in `Hamt/tests/merkle_core_wire.rs` and `Hamt/tests/merkle_persistence.rs`. Coverage includes:

- persistent snapshot preservation;
- no-op root sharing for equal-value replacement and absent removal;
- duplicate rejection through `try_add` and `add`;
- same-hash collision insertion, lookup, and removal;
- CHAMP inline-payload/child-run invariants, independent insertion histories, and typed diff;
- 32/64-bit Patricia signed boundaries, randomized `BTreeMap` histories, structural map/set algebra,
  key/left/right map combiners, cached subtree-cardinality invariants, no-op root sharing, and direct
  union/intersection identity checks for a partially shared subtree;
- streaming iterator exact-size accounting over collision buckets;
- last-wins bulk map construction while retaining the original stored key;
- one-way map/set edit sessions: clean/no-op root and policy identity, consuming publication,
  active lookup and iteration, stored representatives, collision-heavy point edits, clear,
  retained-source isolation, and a deterministic 4,096-command `BTreeMap` model;
- persistent set algebra and proper subset/superset relations, including zero-rehash shared-policy
  operations, semantic fallback across independent policies, and collision-heavy model histories;
- scratch bulk-builder snapshot detachment, first-key/last-value duplicate identity,
  final-hash-level splitting, and collision-heavy/branch-heavy differential agreement with
  incremental construction;
- strict big-endian integer, nullable UTF-8/byte, and RFC-4122 GUID codec vectors and malformed
  input rejection;
- exact C#-shared domain, root, and `MST2` single-block golden bytes;
- history-independent wide-tree construction, removal/reinsertion canonicality, inclusive ranges,
  typed diff, path-copy sharing, and invariant re-encoding;
- non-`Clone` values, retained randomized versions against `BTreeMap`, independently discovered
  SHA-256 layers, and concurrent readers;
- exact closure save/load/export/import including empty roots, complete and partial packs, missing
  blocks, digest tampering, malformed/noncanonical bytes, foreign domains, unsupported algorithms,
  authenticated subtree-count changes, and crossed child intervals;
- independent enforcement of all seven verification limits and proof-query rejection before any
  codec/block decode;
- destination conflict preflight with zero partial writes, concurrent idempotent store writes,
  closure-pruned sync packs, iterative frontier transfer, and partial-store repair;
- canonical `MSP2` membership, nonmembership, and range proofs plus changed query/block/expansion,
  omitted/extra step, duplicate, envelope, and root rejection;
- disjoint, identical, conflicting, custom-value, side-selection, deletion, present-`None`, and
  unresolved no-partial-tree merge behavior, including persistence and merge of non-`Clone` values.
