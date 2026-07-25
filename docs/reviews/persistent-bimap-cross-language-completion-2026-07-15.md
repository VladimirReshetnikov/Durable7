# PersistentBiMap: Cross-Language Completion Audit — 2026-07-15

- Status: Complete eight-language shipment; benchmarks deliberately deferred
- Created (UTC): 2026-07-15T22:36:39Z
- Repository HEAD (audited): 6aca92e98e7507eb19326233421f364edcf6e36a
- Audience: Maintainers and AI coding agents reviewing repository-owned persistent hash collections
- Scope: Shared contract, implementation locations, language mappings, package wiring, validation
  evidence, and remaining-work audit for the strict persistent bidirectional map

## Outcome

The repository now ships a strict immutable persistent bidirectional map in C#, C, C++, Haskell,
Kotlin, Rust, TypeScript, and Python. Every port stores the same logical bijection in two immutable
CHAMP maps: a forward `K -> V` map and an inverse `V -> K` map. The ports use language-local naming,
ownership, failure, nullability, and identity idioms while preserving the same two-domain contract.

The tranche is complete without benchmark evidence. Correctness, policy, representative,
persistence, failure-atomicity, inverse, model, concurrency, compiler, package, and documentation
gates provide the shipment evidence. Performance measurements remain postponed until the machine
can run them without competing CPU, memory, and I/O load.

## Completion Requirements And Evidence

| Requirement | Authoritative evidence | Disposition |
| --- | --- | --- |
| Public persistent bimap in every repository language | The eight implementation rows and source paths below | Complete |
| Independent key and value equivalence/hash policies | Constructors/factories and policy-specific tests in every port | Complete |
| Strict uniqueness in both domains | Add/try-add tests for key conflict, value conflict, and simultaneous conflict precedence | Complete |
| Non-displacing replacement | Configured-policy replacement tests and two-map models | Complete |
| First representative retention | Equivalent-key/value fixtures in every focused suite | Complete |
| Symmetric lookup and removal | Key-selected and value-selected lookup/removal tests, including opposite representatives | Complete |
| Null-like value presence | Nullable C#/Kotlin tests, stored `undefined` TypeScript tests, `None` Python tests, nested `Maybe`/`Option` Haskell/Rust tests, and stored `NULL` C tests | Complete |
| O(1)-in-pair-count inverse | Root/facade identity tests; implementations swap existing map wrappers and roots without enumeration | Complete |
| Two-map failure atomicity | Exception/panic/callback/allocation failpoint tests appropriate to each language | Complete |
| Retained immutable versions | Generated/deterministic models retain and recheck prior snapshots | Complete |
| Concurrent immutable reads | Managed/native reader tests where the runtime admits them; C documents its non-atomic handle boundary | Complete |
| Package/export/build integration | Project/package exports, aggregate headers/modules, installed-consumer/package/wheel gates | Complete |
| Detailed active documentation | Workspace API/usage/validation docs, global catalogs, semantic reference, and this audit | Complete |
| Benchmarks | Explicitly outside the shipment gate and postponed for an isolated session | Deferred by design, not missing correctness work |

## Normative Shared Contract

### Representation And Policies

Each logical association is present exactly once in each direction. Both maps have equal counts;
every forward `(key, value)` resolves to an equivalent inverse `(value, key)`, and every inverse
entry resolves back to the forward entry. Key and value hash/equality policies are separate retained
objects. A successor, clear result, or inverse facade must not silently substitute a default policy.

The bimap retains the first stored representative of each policy equivalence class. An equivalent
probe selects the existing representative; it does not replace it merely because the caller supplied
a different object from the same class.

### Strict Addition And Conflict Precedence

Strict addition succeeds only when both the key class and value class are unrepresented. An
equivalent complete pair is still a conflict rather than an idempotent add. Implementations check the
key domain first. Ports with a domain-bearing result report key conflict when both domains are
occupied; C# deliberately retains its conventional boolean `TryAdd` shape and returns the exact
receiver for either conflict.

Throwing/status/result mappings differ, but a conflict never publishes a partial successor:

- C# throws `ArgumentException`; `TryAdd` returns `false` plus the exact receiver.
- C returns `D7_HAMT_DUPLICATE_KEY`/`D7_HAMT_DUPLICATE_VALUE`, or a
  `d7_hamt_bi_map_conflict` from `try_add`.
- C++ throws `bimap_conflict_error`, or returns `bimap_conflict` from `try_add`.
- Haskell returns `Either BiMapConflict` and exposes `KeyConflict`/`ValueConflict`.
- Kotlin throws `BiMapConflictException`, or returns a result carrying `BiMapConflict.KEY`/`VALUE`.
- Rust returns `Result<_, BiMapConflict>` with `Key`/`Value` variants.
- TypeScript and Python throw their local `BiMapConflictError`, while their nonthrowing result
  carriers use `"key"`/`"value"`.

### Non-Displacing Set

Set has four cases:

1. a missing key and free value add a new pair;
2. a missing key and occupied value fail without displacement;
3. a present key and configured-value-policy-equivalent value return an unchanged/root-sharing
   result and retain both stored representatives; and
4. a present key and distinct free value replace only that key's old pair.

Replacement removes and reinserts both directions. This is intentional: the underlying map's
ordinary value-equality shortcut can use a different equality policy from the bimap's value domain.
No port offers a force-put operation that silently evicts another key.

### Lookup, Removal, Clear, And Enumeration

Lookup and removal work through either domain. Successful try-removal returns or reports the
opposite stored representative. Presence is separate from payload so stored `null`, `NULL`,
`undefined`, `None`, `Nothing`, or `Option::None` cannot be mistaken for absence. Absent removal is
an unchanged/root-sharing result. Clear retains both policies and is likewise unchanged when the
map is already empty.

Iteration follows the forward CHAMP. It is stable for one immutable version but otherwise
unspecified; callers must not infer insertion, sorted, or inverse-map order.

### Inverse And Identity

Inverse construction clones or reuses two small map facades and swaps their roots and policy roles.
It performs no pair enumeration and is O(1) in pair count.

- C#, Kotlin, TypeScript, and Python cache reciprocal facade objects. Double inversion returns the
  exact original object.
- C, C++, Haskell, and Rust are value/handle-semantic ports. Double inversion shares the same two
  immutable roots; object-reference identity is not part of their collection model.

The C facade carries an explicit policy-orientation bit. This ensures that an inverted handle uses
the original value policy as its active key policy and the original key policy as its active value
policy during no-op and invariant decisions, not merely during CHAMP lookup.

### Failure, Concurrency, Complexity, And Storage

Both successor maps are completed before a new facade is published. A callback, equality, hash,
clone, panic/exception, or allocation failure therefore exposes no half-bijection and leaves the
source usable. Immutable snapshots support concurrent readers subject to the supplied policy's own
thread-safety. TypeScript's guarantee is isolate-local. C allows concurrent reads of already-
retained snapshots, but its ordinary handle reference counts are non-atomic, so clone/update/destroy
operations on one shared lineage must be serialized.

One-direction lookup has the underlying CHAMP bound `O(w + c)`, with bounded trie depth `w` and an
equal-hash collision scan `c`. Point edits perform bounded probes and up to one persistent update per
direction; replacement performs remove-plus-add in both directions. Inverse and empty clear are
O(1) in pair count. Space is honestly approximately twice one map because every pair appears in
both tries.

The first public surface intentionally excludes algebra, bulk builders, transient/edit sessions,
mutable inverse views, and displacing force-put. Algebra would require a receiver-policy conflict
matrix spanning collisions in both domains; it is not implied by map or set algebra.

## Language Implementation Matrix

| Language | Public surface | Implementation | Focused tests | Identity/ownership mapping |
| --- | --- | --- | --- | --- |
| C# | `PersistentBiMap<TKey, TValue>` implementing `IReadOnlyDictionary<TKey, TValue>` | [`PersistentBiMap.cs`](../../src/CSharp/src/Durable7.Hamt/PersistentBiMap.cs) | [`PersistentBiMapTests.cs`](../../src/CSharp/tests/Durable7.Hamt.Tests/PersistentBiMapTests.cs) | Cached reciprocal reference identity; retained `IEqualityComparer` objects |
| C | `d7_hamt_bi_map`, iterator, conflict enum, status-returning operations | [`persistent_bi_map.h`](../../src/C/Hamt/include/durable7/hamt/persistent_bi_map.h), [`persistent_bi_map.c`](../../src/C/Hamt/src/persistent_bi_map.c) | [`persistent_bi_map_tests.c`](../../src/C/Hamt/tests/persistent_bi_map_tests.c) | Explicit clone/destroy handles, borrowed removal representatives, root-sharing double inverse |
| C++ | `persistent_bi_map<Key, T, KeyHash, KeyEqual, ValueHash, ValueEqual>` | [`persistent_bi_map.hpp`](../../src/Cpp/Hamt/include/durable7/hamt/persistent_bi_map.hpp) | Bimap groups in [`persistent_hamt_tests.cpp`](../../src/Cpp/Hamt/tests/persistent_hamt_tests.cpp) and the packaged-header consumer | Value semantics over `shared_ptr` roots; stateful policy objects retained by value |
| Haskell | `Durable7.Hamt.BiMap` | [`BiMap.hs`](../../src/Haskell/Hamt/src/Durable7/Hamt/BiMap.hs) | [`BiMapTests.hs`](../../src/Haskell/Hamt/test/BiMapTests.hs) | Pure strict two-map value; nested `Maybe` preserves nullable presence |
| Kotlin | `PersistentBiMap<K, V>` plus conflict/lookup/removal result types | [`PersistentBiMap.kt`](../../src/Kotlin/Hamt/src/durable7/hamt/PersistentBiMap.kt) | [`PersistentBiMapTests.kt`](../../src/Kotlin/Hamt/test/durable7/hamt/PersistentBiMapTests.kt) | Synchronized volatile reciprocal cache; retained runtime `HashPolicy` objects |
| Rust | `PersistentBiMap<K, V, SK, SV>` plus `BiMapConflict` and result carriers | [`bi_map.rs`](../../src/Rust/Hamt/src/bi_map.rs) | [`persistent_bi_map.rs`](../../src/Rust/Hamt/tests/persistent_bi_map.rs) | Safe value semantics over `Arc` roots; lawful `Eq`/`Hash` plus independent `BuildHasher` states |
| TypeScript | `PersistentBiMap<K, V>`, `BiMapLookup`, add/remove results, `BiMapConflictError` | [`persistent-bi-map.ts`](../../src/TypeScript/src/hamt/persistent-bi-map.ts) | [`persistent-bi-map.test.ts`](../../src/TypeScript/test/hamt/persistent-bi-map.test.ts) | Cached reciprocal object inside one isolate; explicit runtime `HashPolicy` objects |
| Python | `PersistentBiMap[K, V]`, dataclass lookup/add/remove results, `BiMapConflictError` | [`persistent_bi_map.py`](../../src/Python/src/durable7/hamt/persistent_bi_map.py) | [`test_persistent_bi_map.py`](../../src/Python/tests/hamt/test_persistent_bi_map.py) | Lock-coordinated reciprocal cache; explicit `HashPolicy`; unhashable defaults remain identity-based |

## Package And Documentation Wiring

- C# compiles the type directly into `Durable7.Hamt`; its XML documentation and API-
  shape tests guard the public surface.
- C exposes a separate public header and compiles the production source plus focused executable from
  the serialized HAMT build wrapper.
- C++ includes the bimap from the aggregate public HAMT header and constructs/replaces/inverts it in
  the copied installed-header consumer.
- Haskell lists `Durable7.Hamt.BiMap` in the Cabal package and aggregate facade, and the
  dependency-light HAMT executable invokes `BiMapTests`.
- Kotlin compiles the source and registered test group through the HAMT workspace launcher.
- Rust re-exports the module from the crate root and includes its integration test target in both
  Debug and Release Cargo gates.
- TypeScript exports the type and result/error vocabulary through the HAMT subpath and root package;
  the declaration/ESM build and package dry run verify distributable wiring.
- Python exports the type and result/error vocabulary from both HAMT and package-root namespaces;
  the clean-environment installed-wheel smoke exercises the public bimap.

All workspace READMEs, API notes/specifications, usage guides, validation guides, and test maps name
the strict contract and its benchmark boundary. The repository-wide
[data-structure catalog](../reference/data-structure-catalog.md#persistent-bidirectional-map) and
[semantic contracts reference](../reference/semantic-contracts.md#persistent-bidirectional-maps)
are the current cross-language navigation and contract authorities.

## Validation Evidence

Every build/test lane ran sequentially. MSBuild/NuGet, native compilers, Cargo/rusttest, Cabal, the
Kotlin compiler/JVM, Vitest, and pytest used their checked-in one-worker controls where applicable.
No two language toolchains overlapped.

### C#

Commit `eb80d985934397db0cc77dd40751cf7a892bfcea` records:

- 16/16 focused `PersistentBiMapTests`;
- 308/308 complete HAMT tests in Debug;
- 308/308 complete HAMT tests in Release; and
- zero build warnings and zero build errors in both configurations.

The suite covers independent comparer retention, strict conflicts, first representatives,
configured-value-comparer replacement, failure atomicity, cached inverse identity, nullable values,
enumeration, retained 1,000-command histories, concurrent readers, invariant validation, and API
shape.

### TypeScript And Python

Commit `0e1cec5b6adfd40a7fe2bd7a1c9c96c653effee0` records:

- TypeScript strict checking, 27/27 Vitest files and 187/187 tests, declaration/ESM build, and
  `npm pack --dry-run`; the focused bimap file contributes eight tests.
- Python Ruff, strict Mypy, 170/170 pytest tests, isolated source/wheel builds, Twine metadata checks,
  and clean installed-wheel smoke.

Both gates include policies, conflicts, representatives, null-like values, non-displacing
replacement, cached inverse identity, retained generated models, failure atomicity, and concurrent
or runtime-appropriate reader coverage.

### C++

Commit `a9ce58081b5ac2db2adef1194fc3de53e5501be5` records serialized:

- MSVC Debug and Release with `/W4 /WX`: 67/67 CHAMP/Patricia groups, 20/20 Merkle groups, and the
  installed-header consumer;
- strict GCC C++20 with `-Wall -Wextra -Wpedantic -Werror`: 67/67 groups; and
- strict LLVM/Clang C++20 with the same warnings-as-errors policy: 67/67 groups.

Six bimap groups cover stateful policies, representatives, conflicts, replacement, symmetric
removal, clear/enumeration, inversion, a 2,000-operation collision-heavy model, retained snapshots,
and injected policy failure.

### Rust

Commit `b5cf8a0f1ea22b1f2048077bdc164cb180391310` records `cargo fmt`, focused warnings-denied
Clippy, and serialized Debug and Release HAMT gates. Each configuration passes 91/91 tests plus doc
tests; eight integration tests belong to the bimap. The Clippy command allows only the documented
pre-existing Rust 1.96 `double_must_use` baseline, which does not originate in this tranche.

### Haskell

Commit `ccaa5365fb9aedbef21b778a27fa3d3ecfde5d5e` records the complete serialized GHC 9.12.4
HAMT executable with one Cabal job and `-Werror`. The bimap tier covers independent policies,
conflicts, representatives, nested-`Maybe` removal, inversion, clear, a 2,000-step model, retained
versions, injected hash failure, and `forkIO` readers.

### Kotlin

Commit `af77a15df5abfe41da144a34e4ec2bba0837cf85` records the fully serialized Kotlin 2.4.0/JVM
21 HAMT gate with one compiler backend thread, one active processor, serial GC, and all 69 registered
groups passing. The bimap group covers policies, conflicts, representatives, non-displacing
replacement, nullable values, reciprocal identity, a 2,000-operation model, policy failure,
retained snapshots, validation, and concurrent readers.

### C

Commit `6aca92e98e7507eb19326233421f364edcf6e36a` records:

- complete MSVC Debug: 43 core HAMT groups, 9 hash-bag groups, the bimap executable, Patricia, and
  22 Merkle groups;
- the same complete MSVC Release gate;
- a focused strict GCC C17 bimap build with `-Wall -Wextra -Wpedantic -Werror`; and
- the equivalent focused LLVM/Clang C17 build with the same warning flags.

The bimap executable covers separate callback contexts, strict conflicts, first representatives,
configured-policy replacement, inverse mutation/orientation, symmetric removal, stored `NULL`,
clear, a 2,000-operation collision-heavy model, canonical validation, and allocation-failure
atomicity.

## Final Integration Confirmation

The final-tree audit at commit `9b5e9b31e11307dac8397856893910cbc2e29b7e` repeated one focused,
serialized, benchmark-free lane for every language after all implementation and documentation
commits were present:

- C#: 16/16 focused Release bimap tests. The first no-restore attempt exposed a missing local NuGet
  cache entry; a serialized, parallel-disabled restore repaired the environment, after which the
  unchanged source passed.
- TypeScript: 8/8 focused Vitest tests.
- Python: 8/8 focused pytest tests with deterministic hashing.
- C++: 67/67 Release HAMT/Patricia groups, including all six bimap groups.
- Rust: 8/8 focused integration tests with one Cargo build job and one test thread.
- Haskell: the complete HAMT executable under GHC 9.12.4 with one Cabal job.
- Kotlin: all 69 registered HAMT groups with one compiler backend thread and one active processor.
- C: the Release bimap executable.

`origin/main` at `0e1cec5b6adfd40a7fe2bd7a1c9c96c653effee0` is an ancestor of the
shipment branch; the requested merge is therefore already contained and `git merge origin/main`
is a no-op. The final audit also confirms that the active documentation distinguishes the
pre-bimap 1,417-test full-solution Range checkpoint from the later 16-test focused and 308-test
complete C# HAMT bimap evidence.

## Benchmark Boundary

No benchmark was run, interpreted, or used as evidence. This shipment claims the asymptotic bounds
inherited from two CHAMPs and O(1)-in-pair-count root swapping for inverse construction. It makes no
throughput, allocation-rate, cache-locality, break-even, or comparison-to-standard-library claim.
Any future benchmark belongs in a separate isolated session and must not retroactively redefine the
semantic contract.

## Remaining-Work Audit

No implementation, package-wiring, semantic-contract, correctness-test, or documentation item
remains open for the first strict bimap surface. The following are intentionally outside it:

- algebra between bimaps, which needs a separately reviewed two-domain receiver-policy matrix;
- a construction-only builder or transient editing session;
- force-put/displacement semantics;
- native lock-free mutable bimap variants; and
- isolated performance measurement.

Those omissions are explicit design boundaries, not partially implemented features. A future
proposal may promote one only with its own contract, tests, validation, and cross-language parity
decision.
