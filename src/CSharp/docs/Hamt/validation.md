# C# HAMT Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T20:33:30Z
- Repository HEAD: 7c02f68ae23244d48871317ea90d26c0defd2394
- Audience: Maintainers validating the C# HAMT workspace
- Scope: Local restore, build, test, warning-policy, and test-coverage guidance for `src/CSharp/src/Tools.DataStructures.Hamt`

Use this guide when changing the C# HAMT library, tests, examples, or documentation that makes build,
test, API, or complexity claims. For semantic contracts and usage examples, pair it with the
[API specification](api-specification.md) and [usage guide](usage.md).

## Build Model

`DataStructures.sln` contains:

- `src/Tools.DataStructures.Hamt/Tools.DataStructures.Hamt.csproj`, the public library.
- `tests/Tools.DataStructures.Hamt.Tests/Tools.DataStructures.Hamt.Tests.csproj`, the xUnit/CsCheck
  test project.

`Directory.Build.props` applies the workspace defaults:

- Target framework: `net10.0`.
- Language version: C# `preview`.
- Nullable annotations and implicit usings enabled.
- XML documentation generation enabled.
- Public XML documentation warnings `CS1591` and `CS1573` promoted to errors.

The test project references the library project and uses `xunit`, `xunit.runner.visualstudio`,
`Microsoft.NET.Test.Sdk`, and `CsCheck`.

## Commands

From `src/CSharp`:

```powershell
dotnet restore
dotnet build .\DataStructures.sln
.\test.ps1
```

For ordinary behavior changes, `.\test.ps1` is the main gate because it restores and builds as needed before running
the test projects while suppressing modal Windows failure UI throughout the child-process tree. Use the explicit
restore/build steps when validating toolchain or warning-policy changes, or when you want a clearer failure boundary.

## Test Coverage

`tests/Tools.DataStructures.Hamt.Tests/` covers the xUnit/CsCheck suite. See the
[tests README](../../tests/Tools.DataStructures.Hamt.Tests/README.md) for source-file grouping and filter examples.

The suite covers:

- map construction, lookup, replacement, removal, no-op behavior, and enumeration;
- set construction, membership, add/remove, set algebra, and `IReadOnlySet<T>` behavior;
- Axis 2 map/set contract oracles for comparer identity, stored representatives, nullable keys/items,
  collisions, stable enumeration, no-op identity, retained versions, and callback-exception atomicity;
- benchmark-only CHAMP diagnostics that pin root/path sharing, retained size, exact ordinary field
  layout, and the b590 source fingerprints for ordinary nodes and the monomorphic lookup loop;
- the selected 26-test private T1 direct-separate kernel, including first-edit deferral, reusable-path
  promotion, independent array ownership, production/diagnostic parity, O(1) adoption/publication,
  recursive canonicality, base/version isolation, consumed sessions, and deterministic callback,
  allocation, promotion, and publication failure rollback;
- comparer preservation, first equivalent key/item retention, and custom equality;
- equal-hash collision buckets, deep shared hash prefixes, and collision splitting;
- allocation-free copy-safe enumerators;
- CHAMP data-map/node-map shape, canonical independent-history topology, structural equality,
  slot-aligned semantic diff across every node-shape transition, eager validation, key-representative
  semantics, randomized invariant checking, and reference-pruning bounds through internal test access;
- bulk-builder semantics, including collision/deep-prefix construction and detachment of
  already-frozen snapshots from later builder mutations;
- generated map histories checked against model dictionaries with retained snapshots;
- generated set behavior checked against model set semantics.
- concurrent hash-trie root/main RDCSS and node GCAS helping, deterministic snapshot/write race
  schedules, tomb contraction, collision re-splitting, stored-key retention, stable snapshots,
  same-reference value no-ops without equality callbacks, contended publication/accumulation, and
  400 exhaustively serialized short-history linearizability checks across ordinary, shared-prefix,
  and equal-hash policies.
- 32/64-bit Patricia signed-boundary ordering, 35,000 randomized model operations with retained
  snapshots, combining overloads, set relations, and randomized prefix-aware structural algebra.
- Merkle `mst-sha256-b16-v2` codec, digest, empty-manifest, and complete block golden vectors;
  strict bidirectional codec round trips; and malformed, trailing, non-canonical, unversioned-id,
  ill-formed-Unicode, domain, and digest rejection;
- wide-block Merkle independent-history convergence under ordinary and adversarial layer schedules,
  randomized model histories with retained snapshots and ordered ranges, exact-root restoration,
  off-path block sharing, structure statistics, no-op identity, and shape-changing typed diff in both
  directions;
- Merkle save/load and complete/partial pack round trips; missing, tampered, malformed,
  non-canonical, foreign-domain, resource-budget, and destination-conflict failures; and commit
  atomicity for preflight/verification failures;
- complete and partial-closure block synchronization, canonical membership/non-membership/inclusive-
  range proofs, pre-decode proof-query byte limits, exact query-plus-block accounting,
  tampered-query/block and extra-step rejection, and typed three-way merge including disjoint edits,
  identical edits, unresolved/resolved conflicts, deletion, and present-null state.

For a new public operation, add both direct examples and model/property coverage when there is a natural
BCL or simple in-memory oracle.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/CSharp> .\test.ps1
```

If a docs-only change only updates links or wording and does not alter commands, API claims, or XML
documentation behavior, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
