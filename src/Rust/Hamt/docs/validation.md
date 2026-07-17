# Rust HAMT Validation

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers validating the Rust HAMT workspace
- Scope: Cargo commands, warning policy, and test coverage

Run from `src/Rust`:

```powershell
.\test.ps1 -Workspace Hamt
```

The current serialized HAMT gate passes 122/122 tests plus doc tests with one Cargo job and one
rusttest thread. The strict bimap suite contributes 8/8 tests. `cargo fmt --all --check`
passes, and focused Clippy for the library plus `persistent_bi_map` target passes with warnings
denied after allowing only the crate's pre-existing Rust 1.96 `double_must_use` baseline. An
all-target Clippy run additionally reaches a pre-existing hash-bag test literal-grouping warning;
neither baseline warning originates in the bimap tranche. Benchmarks are excluded and remain
postponed until an isolated run.

The wrapper locates Cargo on `PATH` or under the default rustup profile and applies inherited,
non-interactive Windows error handling before Cargo starts the test executable.

## Current derived-structure evidence

On 2026-07-16, the focused set-valued hash-multimap and relation suites each passed 6/6 tests. The
serialized full Rust workspace subsequently passed in both Debug and Release, including all HAMT
unit, integration, and documentation tests. Benchmarks were not run.

On 2026-07-17, the focused map-patch, directed-graph, and indexed-map suites pass 7/7, 6/6, and
6/6 tests. Together they cover presence-safe nested options, strict conflict atomicity, inversion
and composition, adjacency/reversal/incident removal, selector cardinality and panic atomicity,
retained representatives, root sharing, branching versions, and coupled-index validation.

The crate uses `#![forbid(unsafe_code)]`. HAMT, hash-bag invariant, and Patricia unit tests are inline
in `Hamt/src/lib.rs`, `Hamt/src/hash_bag.rs`, and `Hamt/src/patricia.rs`; one-descent map factories
and the hash bag have focused integration suites in `Hamt/tests/map_factory_updates.rs` and
`Hamt/tests/persistent_hash_bag.rs`; the strict bimap, set-valued multimap, and relation have focused
suites in `Hamt/tests/persistent_bi_map.rs`, `Hamt/tests/persistent_hash_multimap.rs`, and
`Hamt/tests/persistent_relation.rs`; patch, graph, and indexed-map suites live in their corresponding
`persistent_*.rs` files; Merkle core/wire and persistence integration tests live in
`Hamt/tests/merkle_core_wire.rs` and `Hamt/tests/merkle_persistence.rs`. Coverage includes:

- persistent snapshot preservation;
- no-op root sharing for equal-value replacement and absent removal;
- duplicate rejection through `try_add` and `add`;
- one-hash/one-descent `get_or_add` and `add_or_update`, exact selected-closure counts, caller-key
  versus stored-key identity, stored/equal-`Arc` value retention, collision buckets, panic
  atomicity, and a deterministic 4,096-command collision-heavy `BTreeMap` model;
- hash-bag distinct/total/count queries; first-representative retention; expanded, distinct, and
  entry iteration; zero-before-hash validation; checked multiplicity and wide `i64` totals;
  saturated removal; max/min/saturated/checked receiver-policy algebra; eager mismatched-policy
  normalization; representative precedence; failure atomicity; and a deterministic 4,096-command
  collision-heavy multiset model;
- strict bimap key-first two-domain conflicts, independent hash builders, `Eq` representatives,
  non-displacing replacement, symmetric removal, nested-`Option` presence, root-sharing inversion
  and clear, a deterministic 2,000-command two-map model, retained snapshots, panic atomicity, and
  concurrent readers;
- hash-multimap independent hash policies, first representatives, distinct key/pair counts,
  duplicate root sharing, empty-group contraction, whole-key removal, branching histories, and
  recursive invariants;
- relation many-to-many adjacency, global representatives, duplicate two-index root sharing,
  O(1) inverse root swapping, symmetric pair/whole-domain removal, retained branches, and mutually
  inverse invariants;
- same-hash collision insertion, lookup, and removal;
- CHAMP inline-payload/child-run invariants, independent insertion histories, and typed diff,
  including exact callback counters proving same-policy equality/diff rehash no keys and prune a
  partially shared descendant subtree before value comparison;
- 32/64-bit Patricia signed boundaries, randomized `BTreeMap` histories, structural map/set algebra,
  key/left/right map combiners, cached subtree-cardinality invariants, no-op root sharing, and direct
  union/intersection identity checks for a partially shared subtree;
- streaming iterator exact-size accounting over collision buckets;
- last-wins bulk map construction while retaining the original stored key;
- one-way map/set edit sessions: clean/no-op root and policy identity, consuming publication,
  active lookup and iteration, stored representatives, receiver-policy relations, collision-heavy
  point edits, clear, retained-source isolation, and a deterministic 4,096-command `BTreeMap` model;
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
