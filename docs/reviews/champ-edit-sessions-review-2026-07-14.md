# Cross-Language CHAMP Editing-Session Review

- Status: Complete - one P2 finding resolved; no open correctness findings
- Created (UTC): 2026-07-14T04:25:51Z
- Repository HEAD: 23cfe9c83f8ef642cb223eccc99faad9a79ceb40
- Audience: Maintainers reviewing the cross-language CHAMP lifecycle ports
- Scope: Haskell `1041a81`, Rust `6abaf39` / `ab33889`, Kotlin `9e17cfe`, C++ `424a42b`, and C `404ba91`

## Review Boundary

This review checks the sibling-language one-way map/set editing sessions that landed after the
earlier Axis 1 and Axis 2 review reports. It compares source, tests, and language-local documentation
for lifecycle consumption, retained-source isolation, policy and representative preservation,
logical no-op identity, iterator/view invalidation, set-relation semantics, and failure-before-commit
behavior.

The review deliberately does not require the sibling sessions to reproduce the C# owner-token
representation or its benchmark-backed in-place-edit performance. C, C++, Haskell, Kotlin, and Rust
use their persistent path-copy kernels for changed point edits and make no transient speed or
allocation claim. No benchmarks were run or interpreted for this review.

## Findings

| Severity | Finding | Disposition |
| --- | --- | --- |
| P2 | A throwing C++ policy move could partially move a transient's persistent map before iterator control and lifecycle state were transferred. Failed move construction left the source observably active; failed move assignment could additionally leave the overwritten destination and both iterator lineages observably active. The documentation disclosed this exceptional boundary only for `persist()`. | Resolved. The C++ transient now has an explicit `move_failed` terminal state. A failed move constructor invalidates the source and its iterator lineage; failed move assignment invalidates source, destination, and both iterator lineages before partially moved maps can be observed. Successful moves retain the existing control-transfer behavior. Throwing move-construction, move-assignment, and set-facade tests pass in Debug and Release. |

No P0 or P1 finding was discovered. After the P2 correction, no open correctness finding remains.

## Language Results

### Haskell

`MapTransient` and `SetTransient` build persistent candidates before a masked `IORef` commit.
Consumption, source isolation, exact clean/no-op root identity, policy/representative retention, and
callback failure behavior match the documented `IO` lifecycle. No correctness defect was found.

### Rust

`TransientHashMap` and `TransientHashSet` use ownership-consuming publication. Navigation and reads
retain `Arc`-backed persistent state, changed edits use the persistent path-copy kernel, and the type
system prevents use after publication. Receiver-policy relations and duplicate collapsing match the
persistent set contract. No correctness defect was found.

### Kotlin

Nested `Transient` sessions enforce one-way consumption dynamically, bind acquired views and
iterators to their acquisition version, preserve nullable/collision representatives, reject
reentrant edits/publication, and commit only completed persistent successors. Coverage includes
callback retryability and the complete receiver-policy relation surface. No correctness defect was
found.

### C++

The ordinary move, overwrite, destruction, publication, generation-bound iterator, callback
failure, source-isolation, and set-relation paths were sound. The exceptional policy-move P2 above
was the only concrete defect and is resolved with deterministic terminal invalidation. Publication
retains its separately documented no-retry/content-preservation caveat when a custom policy move
throws after partially moving policy/map subobjects.

### C

Opaque ref-counted transient handles share alias-wide consumption; point edits install only fully
prepared persistent successors; set relations preserve receiver policy and success-only outputs;
allocation and retain failures are retryable and output-atomic. Iterators intentionally borrow the
session state rather than retaining it, so at least one owning handle must outlive them. That
lifetime rule is explicit in the header, API specification, and usage guide and is therefore a
documented C ownership precondition rather than a defect.

## Validation Evidence

The original language commits record serialized complete-suite validation for Haskell, Rust,
Kotlin, C++, and C. The P2 correction was additionally validated with the C++ HAMT wrapper in both
Debug and Release under MSVC C++20 `/W4 /WX`, one compiler/test process at a time:

- 48 CHAMP/Patricia tests, including throwing transient move construction, throwing move
  assignment, source/destination iterator invalidation, and the set facade;
- 20 Merkle tests; and
- the copied aggregate-header consumer.

No benchmark executable was invoked.

## Residual Hardening Opportunities

These are optional coverage improvements, not open correctness findings:

- Haskell could add direct transient set-relation, `clear`, and injected asynchronous-exception
  cases beyond its existing callback-failure and model coverage.
- Rust could add `catch_unwind` regressions for panicking hash/equality/clone policies to make its
  documented panic-before-commit boundary more explicit in tests.
- C iterator refcount/version wraparound remains theoretical; the supported borrowed-lifetime rule
  and practical counters are already documented.

The managed Ctrie remains intentionally limited to C# and Kotlin/JVM. Frozen F0/F1 evidence, F2/F3
surfaces, C4 cursor adapters, and native Ctrie reclamation are outside this review and remain
postponed or inapplicable under the repository plans.
