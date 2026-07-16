# Hamt Documentation

- Status: Informational
- Created (UTC): 2026-07-02T05:02:24Z
- Repository HEAD: 3c639e02d05377685676923a13b30a3d22fd4994
- Audience: Maintainers and implementers working on the C# CHAMP, hash-bag, hash-multimap, Ctrie, Patricia, and Merkle families
- Scope: Index of current specifications, usage, and validation for `src/CSharp/src/Tools.DataStructures.Hamt`

## Current Documents

- [API Specification](api-specification.md) defines the public C# API shape, semantic contracts,
  persistence/concurrency behavior, complexity targets, canonical wire, verification boundary, and
  merge contracts for the CHAMP map/set/bag/multimap, Ctrie, integer Patricia, and Merkle search-tree
  surfaces. The bag section locks its separate distinct/expanded counts, checked multiplicities,
  comparer normalization, representative precedence, enumeration, debugger, and no-op contracts.
  The bimap section locks its strict two-domain uniqueness, independent policies, representative,
  replacement, symmetric removal, inverse-view, failure-atomicity, and honest double-storage contracts.
  The multimap section locks its set-valued pair semantics, independent policies, first
  representatives, exact pair count, and no-empty-group invariant.
- [Usage guide](usage.md) shows persistent CHAMP updates and structural diff, hash-bag counting and
  algebra, comparer and stored-representative behavior, one-way map/set transient edit sessions,
  set-valued hash multimaps, strict bidirectional mappings, set algebra, Ctrie snapshots, Patricia structural algebra, and Merkle persistence, proofs,
  synchronization, and three-way merge.
- [Validation](validation.md) records the local .NET restore/build/test commands, workspace warning
  policy, generated XML-documentation gate, and xUnit/CsCheck/model/stress coverage for the CHAMP
  map/set/bag/multimap, Ctrie, Patricia, and Merkle families.
- [Tests README](../../tests/Tools.DataStructures.Hamt.Tests/README.md) maps the xUnit/CsCheck test project,
  source files, filter commands, wire vectors, proof/persistence adversarial cases, concurrency
  histories, and model/property coverage.
- [CHAMP transient T0 decision](transient-t0-decision.md) locks the private T1 deciding tuple,
  single-worker direct-separate command, historical owner-field evidence boundary, and T0
  opportunity-counter contract.
- [CHAMP transient T1 decision](transient-t1-decision.md) selects the direct separate-node kernel,
  records the affinity-pinned material-win and sparse guard, closes the retained-layout and
  failure/canonicality evidence, and authorizes T2 public-API implementation without shipping it.
- [CHAMP transient T2 decision](transient-t2-decision.md) records the original C#-only shipment of
  the optimized owner-token map/set lifecycle, its exact API and one-way consumption contract, the
  pinned T1 evidence, the full public-path confirmation, and the distinction between the earlier
  Dry-job smoke and performance evidence. Sibling workspaces now expose semantic path-copying
  sessions without inheriting that performance claim.
- [Frozen hash F0 packed-index signal decision](frozen-f0-signal-decision.md) records the faithful
  linear prototype, standalone semantic verifier, locked isolated evidence commands, and the
  postponed advance/defer gate that must close before F1 can be interpreted.
- [Frozen hash F1 fixed-layout decision](frozen-f1-layout-decision.md) locks the benchmark-local
  linear, Robin-Hood, and quadratic packed-index candidates, semantic setup oracle, retained-array
  diagnostics, single-worker evidence commands, and the postponed select/defer gate.
- [Implementation Review 2026-07-02](hamt-implementation-review-2026-07-02__afa84237ef48.md)
  records the adversarially-verified multi-agent review of the initial implementation, the
  improvements applied in response (annotations, `IReadOnlySet<T>`, single-pass adds,
  allocation-free copy-safe enumerators, key/item recovery APIs, doc-contract fixes, and a
  20-to-47-test suite expansion), and the declined findings with rationale.
