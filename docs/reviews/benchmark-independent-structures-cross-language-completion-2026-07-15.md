# Benchmark-Independent Structures: Cross-Language Completion Audit — 2026-07-15

- Created (UTC): 2026-07-15T14:22:45Z
- Repository HEAD (audited): d71c50e8163c5b12cab5881d9cc0191020ee9fe3
- Audience: Maintainers and AI coding agents working on repository-owned persistent collections
- Scope: Final implementation, dependency, review-report, and validation audit for the four
  structures selected by the benchmark-independent C#-first proposal

> **Later shipment note.** This report audits the four-surface proposal at its recorded HEAD.
> `PersistentBiMap`, which was still a reserve candidate at that checkpoint, subsequently shipped
> across all eight languages. Its separate
> [completion audit](persistent-bimap-cross-language-completion-2026-07-15.md) owns that later
> implementation and validation evidence.

## Outcome

The proposal is complete across C#, C, C++, Haskell, Kotlin, Rust, TypeScript, and Python. Every
language ships all four required surfaces:

1. one-hash, one-descent persistent-HAMT `GetOrAdd`/`AddOrUpdate` operations;
2. an immutable persistent hash bag with distinct and expanded cardinalities;
3. a neutral, independently owned persistent ordered set; and
4. a persistent lazy range-update sequence over an implicit AVL and a named action algebra.

The implementation order remained C# first. TypeScript and Python followed, then the five native
and statically typed siblings. No benchmark was run or interpreted. The reserve candidates and the
proposal's benchmark- or consumer-gated candidates remain outside this shipment.

## Completion Matrix

| Language | HAMT factories | Hash bag | Ordered set | Range-update sequence |
| --- | --- | --- | --- | --- |
| C# | `PersistentHashMap.GetOrAdd` / `AddOrUpdate`; focused callback/topology/model tests | `PersistentHashBag<T>`; algebra, overflow, enumeration, property, and API tests | `Tools.DataStructures.Ordered`; 62 focused model, failure, concurrency, API, and dependency tests | `IRangeUpdateAlgebra` + `RangeUpdateSequence`; 62 focused and 692 FingerTree tests |
| C | `tds_hamt_map_get_or_add` / `tds_hamt_map_add_or_update`; node-transition and failpoint tests | `tds_hamt_bag`; nine deterministic groups plus retained-history and failure sweeps | `src/C/Ordered`; independent C17 workspace and model/failure tests | `ft_range_update_sequence`; algebra, DAG, failpoint, model, and concurrency tests |
| C++ | `persistent_hash_map::get_or_add` / `add_or_update`; callback, topology, exception, and model tests | `persistent_hash_bag`; checked algebra and diagnostic tests | `src/Cpp/Ordered`; header-first independent workspace and 17-case suite | `range_update_sequence`; nine focused cases plus installed-consumer coverage |
| Haskell | `HashMap.getOrAdd` / `addOrUpdate` and `Either` variants; laziness, callback, topology, and model tests | `Data.Structures.Hamt.HashBag`; checked `Int32`/`Int64` contract | `Data.Structures.Ordered.PersistentOrderedSet`; independent package and model suite | `Data.Structures.FingerTree.RangeUpdateSequence`; laws, model, sharing, and overflow tests |
| Kotlin | `PersistentHashMap.getOrAdd` / `addOrUpdate`; nullable/callback/topology/model tests | `PersistentHashBag`; checked `Int`/`Long` contract | neutral `Ordered` workspace; 10 executable suites | `RangeUpdateSequence`; algebra, nullable, failure, model, DAG, and reader tests |
| Rust | `PersistentHashMap::get_or_add` / `add_or_update`; panic, topology, and model tests | `PersistentHashBag`; checked `i32`/`i64` contract | `tools-data-structures-ordered`; 11 tests plus Clippy | `tools-data-structures-range-update`; nine integration tests plus Clippy |
| TypeScript | `PersistentHashMap.getOrAdd` / `addOrUpdate`; strict callback/topology/model tests | `PersistentHashBag`; policy/algebra/model tests | neutral `ordered` module; example, algebra, property, invariant, and dependency tests | `RangeUpdateSequence`; 45 focused tests within a 169-test full package gate |
| Python | `PersistentHashMap.get_or_add` / `add_or_update`; callback/topology/model tests | `PersistentHashBag`; checked multiplicity and receiver-policy tests | neutral `ordered` package; example/property/invariant/dependency tests | `RangeUpdateSequence`; 18 focused tests within a 150-test full distribution gate |

## Authoritative Implementation Evidence

### C#

- HAMT factories: `src/CSharp/src/Tools.DataStructures.Hamt/PersistentHashMap.cs` and the HAMT
  factory-update tests.
- Hash bag: `src/CSharp/src/Tools.DataStructures.Hamt/PersistentHashBag.cs` and the
  `PersistentHashBag*Tests.cs` suites.
- Ordered set: `src/CSharp/src/Tools.DataStructures.Ordered/` and
  `src/CSharp/tests/Tools.DataStructures.Ordered.Tests/`.
- Range updates: `src/CSharp/src/Tools.DataStructures.FingerTree/RangeUpdateSequence*.cs`,
  `IRangeUpdateAlgebra.cs`, and the `RangeUpdate*Tests.cs` suites.

### C and C++

- C HAMT factory and bag APIs live under `src/C/Hamt/include` and `src/C/Hamt/src`; their tests are
  in `src/C/Hamt/tests`. The neutral ordered workspace is `src/C/Ordered`. The range API,
  implementation, and tests are respectively
  `src/C/FingerTree/include/tools/data_structures/finger_tree/range_update_sequence.h`,
  `src/C/FingerTree/src/range_update_sequence.c`, and
  `src/C/FingerTree/tests/range_update_sequence_tests.c`.
- C++ HAMT factory and bag APIs live under `src/Cpp/Hamt/include` with executable coverage under
  `src/Cpp/Hamt/tests`. The neutral ordered workspace is `src/Cpp/Ordered`. The range header and
  tests are `src/Cpp/FingerTree/include/tools/data_structures/finger_tree/range_update_sequence.hpp`
  and `src/Cpp/FingerTree/tests/range_update_sequence_tests.cpp`.

### Haskell, Kotlin, and Rust

- Haskell exposes the HAMT additions from `src/Haskell/Hamt`, the ordered set from the independent
  `src/Haskell/Ordered` package, and the range core from
  `Data.Structures.FingerTree.RangeUpdateSequence` in the FingerTree package.
- Kotlin exposes the HAMT additions from `src/Kotlin/Hamt`, the independent ordered workspace from
  `src/Kotlin/Ordered`, and the range core from
  `src/Kotlin/FingerTree/src/tools/datastructures/fingertree/RangeUpdateSequence.kt`.
- Rust exposes the HAMT additions from `src/Rust/Hamt`, the independent
  `tools-data-structures-ordered` crate from `src/Rust/Ordered`, and the independent
  `tools-data-structures-range-update` crate from `src/Rust/RangeUpdate`.

### TypeScript and Python

- TypeScript exports the HAMT additions from `src/TypeScript/src/hamt`, the neutral ordered set
  from `src/TypeScript/src/ordered`, and the range algebra/core from
  `src/TypeScript/src/finger-tree`. The matching suites live under `src/TypeScript/test`.
- Python exports the HAMT additions from
  `src/Python/src/vladimir_reshetnikov/data_structures/hamt`, the neutral ordered set from
  `.../ordered`, and the range core from `.../finger_tree/range_update_sequence.py`. Its matching
  suites live under `src/Python/tests` and the installed-wheel smoke gate exercises all four.

## Shared Semantic Gates

The language-local APIs deliberately differ in naming, result carriers, ownership, exceptions,
callbacks, iteration, and maximum-count representation. They preserve these common obligations:

- each HAMT factory operation hashes once, descends one trie route, invokes exactly the selected
  factory, retains the stored key representative on hits, preserves semantic no-op roots where the
  runtime can express identity, and publishes no successor on callback failure;
- each hash bag stores positive bounded per-class multiplicities, distinguishes distinct from total
  count, retains the receiver's representatives and policy in algebra, checks additive overflow,
  saturates subtraction at zero, and provides distinct, entry, and expanded traversal;
- each ordered set defines membership with its retained hash/equality policy while enumeration
  follows explicit persistent order, retains first representatives, owns movement/range/stable-sort
  semantics, normalizes algebra under receiver policy, and validates both sides of its dual index;
- each range sequence uses a separate persistent implicit AVL, ordered cached measures, directional
  `compose(newer, older)`, count-aware tag action, lazy logical-measure invariants, push-before-
  rotation structural edits, failure atomicity, retained-version isolation, and logarithmic proper
  range operations; whole-root nonidentity application is structural O(1).

## Dependency-Boundary Audit

The new general collections do not use Tungsten as an implementation substrate or semantic oracle.
The C#, C, C++, Haskell, Kotlin, Rust, TypeScript, and Python ordered implementations are owned by
neutral Ordered modules or packages. Their production code and executable tests import or link only
general HAMT/FingerTree substrates; Tungsten occurrences are explanatory documentation or explicit
negative dependency guards. The range-update implementations likewise contain no Tungsten
dependency. This satisfies the repository's application-leaf boundary.

## Review-Report Audit

No open correctness work was found in the repository review corpus relevant to this shipment.

- `axis1-new-cores-review-2026-07-12.md` states that every original finding is closed and points to
  the round-2 report as authoritative.
- `axis1-new-cores-review-round2-2026-07-12.md` states that no pending work remains.
- the C/C++, C#/Rust, Haskell/Kotlin, and 2026-07-10 cross-language reports each carry a current-state
  note or resolution addendum closing their historical deferred/open sections;
- the CHAMP editing-session review records no open correctness finding; and
- the older C++ FingerTree reports are historical and their active API, packaging, validation, and
  documentation gaps were remediated.

Text such as “findings for follow-up,” “open items,” or “deferred follow-ups” inside those reports
describes the reviewed historical HEAD, not the current tree. Optional Haskell asynchronous-
exception injection and Rust unwind-catching tests remain discretionary hardening, not shipment
blockers or unimplemented contract work.

## Validation Evidence

All validation was serialized; build servers and parallel workers were disabled where supported.
No language toolchains overlapped.

- C#: serialized Debug and Release solution builds completed with zero warnings/errors; both full
  gates passed 1,417/1,417 tests. Focused gates included 52 hash-bag tests, 62 Ordered tests, and 62
  Range tests; the complete FingerTree project passed 692 tests.
- C HAMT/bag: strict MSVC Debug/Release, GCC, and Clang lanes passed. C Ordered passed strict MSVC
  Debug/Release. C Range passed strict MSVC Debug/Release and strict GCC/Clang, with 9/9 CTest
  targets in the full native lanes.
- C++ HAMT/bag: serialized MSVC Debug/Release plus strict GCC/Clang lanes passed. C++ Ordered passed
  its Debug/Release 17-case executable. C++ Range passed its focused 9/9 lane and the complete
  Release 24/24 CTest groups, including installed-consumer packaging.
- Haskell: serialized HAMT, Ordered, and FingerTree gates passed; affected packages also passed
  `cabal check`.
- Kotlin: serialized HAMT passed 68 tests, Ordered passed 10 executable suites, and the complete
  FingerTree executable including Range passed with one active processor/backend thread.
- Rust: serialized HAMT passed 83 unit/integration tests plus doc tests; Ordered passed 11 tests;
  Range passed nine integration tests. Formatting and affected no-dependency Clippy lanes passed
  with warnings denied.
- TypeScript: the Range-focused lane passed 45 tests; the complete strict package gate passed 169
  tests, type checking, build, and package dry-run inspection.
- Python: the Range-focused contribution brought the complete gate to 150 tests; Ruff, formatting,
  strict Mypy, source/wheel builds, metadata checks, and installed-wheel smoke passed.

The commit messages for the implementation tranches retain exact lane-by-lane commands and counts.
This audit rechecked source, tests, package/build wiring, and dependency direction; it did not rerun
all already-green compilers solely for documentation changes.

## Benchmark Deferral And Remaining Scope

Benchmarks remain postponed until they can run in isolation without competing agent, CPU, memory,
or I/O load. This shipment makes asymptotic and deterministic structural claims only; it makes no
comparative throughput, allocation, cache-locality, or break-even claim.

At this report's audited HEAD, the proposal's `PersistentBiMap` and value-carrying interval map were
reserve candidates rather than omitted deliverables. `PersistentBiMap` later shipped in a separate
eight-language tranche; the value-carrying interval map remains parked. Frozen hash tiers,
automatic size tiers, GUID specialization, later cursor phases, native lock-free Ctries, and other
benchmark- or consumer-gated candidates also remain parked under their existing plans.
