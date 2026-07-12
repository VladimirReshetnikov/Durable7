# Porting And Semantic Parity Guide

- Created (UTC): 2026-07-02T19:57:16Z
- Repository HEAD: 6662f6e3e827f0ba75e3ec0ab3f6c9f8c3d27b3f
- Audience: Maintainers and AI agents changing behavior across language workspaces
- Scope: Cross-language porting workflow, semantic parity checks, documentation updates, and validation evidence

Use this guide when a public data-structure behavior changes, a bug fix might need to cross language
boundaries, or a port exposes a design difference that should be documented rather than allowed to
drift. The [workspace map](../reference/workspace-map.md) explains where each workspace lives; the
[data structure catalog](../reference/data-structure-catalog.md) lists public entry points. This
guide is the operational playbook for keeping those surfaces aligned.

Semantic parity does not mean identical names or identical type-system shapes. It means the same
observable contracts are preserved where the languages expose equivalent capabilities, and that
intentional differences are explicit in the local API notes.

## Authoritative Inputs

| Input | Use it for |
| --- | --- |
| Managed API specs under `src/CSharp/*/docs` | Primary semantic contract for repository-owned collection behavior. |
| Native API specs and public headers under `src/C/*` and `src/Cpp/*` | Idiomatic C and C++ surface shape, ownership model, and local divergences. |
| Kotlin API notes under `src/Kotlin/*/docs` | Kotlin/JVM value semantics, null/result shapes, tool bootstrap, persistent representation, complexity, and intentional engine differences. |
| Rust API notes under `src/Rust/*/docs` | Rust value semantics, `Result`/`Option` shape, Cargo validation, and checkpoint divergences. |
| [Data structure catalog](../reference/data-structure-catalog.md) | Cross-language inventory of public data-structure entry points. |
| [Workspace map](../reference/workspace-map.md) | Port lineage, path conventions, and documentation placement. |
| [Build and validation guide](build-and-validation.md) plus workspace validation guides | Commands that prove the affected workspaces still build and pass tests, and the local warning policy, coverage map, stress controls, benchmark boundary, and evidence wording for each workspace. |
| [Test suite map](../reference/test-suite-map.md) plus local test READMEs | Runner shape, test-file grouping, sample-smoke hooks, stress knobs, and the local evidence entry point for changed behavior. |
| Workspace review reports and port plans | Historical rationale and previously identified hazards; keep them historical unless rewriting them into current-state guidance. |

When evidence conflicts, inspect the current source and tests. Treat old plans and review reports as
context, not as current contracts unless an active README, API spec, or public header still says the
same thing.

## Port Lineage

HAMT lineage:

1. [C# HAMT](../../src/CSharp/docs/Hamt/overview.md) is the managed semantic baseline for maps,
   sets, comparer preservation, collision handling, no-op identity, and set algebra.
2. [`src/Cpp/Hamt`](../../src/Cpp/Hamt/README.md) ports the HAMT contract to C++ value types,
   policy objects, `std::shared_ptr` structural sharing, and idiomatic result objects.
3. [`src/C/Hamt`](../../src/C/Hamt/README.md) ports the HAMT contract to type-erased value structs,
   explicit policy callbacks, and clone/destroy lifetime management.
4. [`src/Haskell/Hamt`](../../src/Haskell/Hamt/README.md) ports the HAMT contract to immutable
   Haskell values with a package-local `Hashable` class and optional runtime `HashPolicy`.
5. [`src/Kotlin/Hamt`](../../src/Kotlin/Hamt/README.md) ports the HAMT contract to Kotlin/JVM values,
   runtime `HashPolicy` objects, JVM-reference structural sharing, and null/result-shaped miss paths.
6. [`src/Rust/Hamt`](../../src/Rust/Hamt/README.md) ports the HAMT contract to Rust value types,
   `BuildHasher` hash policies, `Eq` key equality, `Arc` structural sharing, and `Result`/`Option`
   result shapes.

The policy-bound Merkle search tree has complete C#/Rust ports and C/Haskell core/wire ports. All
four pin the SHA-256 domain, key framing, empty digest, and canonical `MST2` block bytes. C# and Rust
additionally align on seven verification budgets, `MSP2` point/range proofs, closure-pruned
synchronization, and no-partial-result three-way merge; those trust-boundary surfaces remain
explicitly pending in C and Haskell. Language-local ownership and callback shapes may differ, but
golden blocks and every implemented accepted/rejected trust-boundary input must remain cross-
language compatible.

The Ctrie is an intentional parity exception. `ConcurrentHashTrie<TKey, TValue>` and its
Kotlin/JVM counterpart are managed-runtime mutable structures whose lock-free indirection-node
protocol relies on tracing garbage collection and offers O(1) immutable generation snapshots.
Keep their observable map, snapshot, helping, and stored-key contracts aligned with each other.
Do not treat the absence of C, C++, Rust, or Haskell ports as drift: native versions require an
explicit epoch/hazard-pointer reclamation design, while a pure Haskell port would be a different
structure. Promoting another language requires a separately reviewed reclamation and concurrency
contract, not a mechanical HAMT port.

DABA Lite is another deliberately mutable member, but its algorithm is portable independently of
its lifetime policy. C#, C, C++, Kotlin/JVM, and Rust preserve FIFO ordering, the six-cursor
schedule, three/two/one combine ceilings, callback-atomic or status-atomic mutators, callback-free
structural validation, and the absence of raw-value iteration. Managed tracing-GC ports can replace
the active chunk chain in O(1). C, C++, and safe Rust instead clear in O(n + c), iteratively
destroying `n` owned values in `c` chunks; deferring that work would violate prompt reclamation. C's
existing callbacks are infallible and non-reentrant by contract, C++ requires no-throw moves so its
planned publication phase cannot tear, and Rust's stable `Rc` cursor representation makes the
mutable core `!Send` and `!Sync`. Treat these ownership/concurrency differences as explicit language
semantics, not parity failures. A pure Haskell value would not preserve DABA's ephemeral incremental
schedule, so omission there is intentional.

The canonical zip-zip sorted set is a policy-canonical persistent member implemented in all six
languages. Every port derives a 32-byte HMAC key as SHA-256 of ASCII `ZZT2`
followed by the public seed in big-endian order, feed an eight-byte big-endian equivalence-class hash to
HMAC-SHA-256, and interpret the first three big-endian words as leading-zero geometric rank,
unsigned secondary rank, and digest content. Preserve random-key and caller-keyed modes, the
minimum 32-byte caller-key contract, comparer-smaller final priority tie, first-representative bulk
semantics, policy-object identity for canonical algebra, and receiver-comparer semantics for
cross-policy equality. A port must test exact rank vectors, unsigned secondary ordering,
equivalence/hash incoherence, insertion-order-independent topology, deep colliding chains, and
concurrent lazy-digest publication where the language exposes shared readers.

Rust deliberately admits natural factories only for explicitly pinned stable hash types; it does
not inherit `DefaultHasher`, `usize`, or `isize` as reproducibility contracts. Its bulk/read/clear/
equality surface accepts non-`Clone` values, while path-copying edits, algebra, and owned diff
require `Clone`. Treat these as honest type-system boundaries rather than parity gaps.

Haskell requires callers to supply the rank hash and creates every policy in `IO`, allocating an
opaque `Data.Unique` token even for deterministic seeded/keyed modes. This preserves pure set
operations without `unsafePerformIO` while making algebra identity testable; separately created
same-seed/key policies still reproduce topology and digest but remain algebra-incompatible. Its
digest is eagerly cached in immutable nodes, so shared-reader publication needs no synchronization.

C++ uses operating-system CNG on Windows and OpenSSL Crypto elsewhere, with the dependency carried
through the exported CMake target. Shared representative objects let moved ranges and rvalue
insertion support move-only `T`; set algebra and removal share those objects rather than imposing a
copy bound. Its node destructor uses a fixed ownership worklist so releasing a height-n tree neither
recurses nor allocates.

C uses the same platform crypto split through status-returning functions, atomic reference counts,
and type-erased callbacks. Distinct policy identities may compare semantically only when their
required caller-owned value-type identity tags match; this is the runtime equivalent of sharing one
generic element type. Preserve output atomicity, exact-operand aliasing, callback status propagation,
key zeroing, non-reentrancy, and the rule that concurrent distinct-handle reads require thread-safe
caller hooks.

FingerTree lineage:

1. [C# FingerTree](../../src/CSharp/docs/FingerTree/overview.md) is the broadest semantic source:
   tuned deque, general measured tree, derived collections, ropes, text helpers, samples,
   benchmarks, and design notes.
2. [`src/Cpp/FingerTree`](../../src/Cpp/FingerTree/README.md) ports the persistent family to a
   header-first C++ library with local naming, value semantics, the policy-canonical zip-zip set,
   move-only-capable Brodal-Okasaki and winner-cached priority-search cores, and CTest validation,
   and segregates the noncopyable mutable DABA Lite core with its native ownership constraints.
3. [`src/C/FingerTree`](../../src/C/FingerTree/README.md) follows the native design in C form with
   explicit handles, callback policies, and facade types, including the erased-type-safe canonical
   zip-zip set and a separately owned mutable DABA Lite handle with allocator-failure status
   semantics.
4. [`src/Haskell/FingerTree`](../../src/Haskell/FingerTree/README.md) ports the family to Haskell
   with a general measured tree, size-measured deque, reversible deque, derived collections, the
   explicitly identified policy-canonical zip-zip set, priority queues, intervals, ropes, and text
   helpers.
5. [`src/Kotlin/FingerTree`](../../src/Kotlin/FingerTree/README.md) ports the family to Kotlin/JVM over
   structurally shared measured AVL sequences with cached monoidal summaries and runtime
   measure/comparator policies, plus the policy-canonical zip-zip sorted set; its API notes state
   the strict-AVL versus lazy-digit-spine costs and segregate the mutable managed DABA Lite member.
6. [`src/Rust/FingerTree`](../../src/Rust/FingerTree/README.md) is the Rust semantic checkpoint for
   the same family names. It preserves immutable snapshot behavior now; the public facades use
   structurally shared Rust tree storage and include the policy-canonical zip-zip sorted set, while
   the workspace documents the remaining asymptotic boundary until the lazy measured spine is
   ported through the whole family. Its separate DABA Lite core is mutable, single-threaded, and
   documents deterministic-drop clear semantics.

Tungsten collections lineage:

1. [C# Tungsten collections](../../src/CSharp/docs/Tungsten/overview.md) are the semantic baseline
   for the `PersistentList<T>` facade and kernel-verified `PersistentAssociation<TKey, TValue>`
   ordering rules.
2. [`src/Cpp/Tungsten`](../../src/Cpp/Tungsten/README.md) ports the family to C++23 value types over
   the C++ HAMT and FingerTree substrates.
3. [`src/C/Tungsten`](../../src/C/Tungsten/README.md) ports the family to type-erased C value structs
   with explicit clone/dispose ownership, C HAMT lookup, and an internal stamp-ordered AVL sequence.
4. [`src/Haskell/Tungsten`](../../src/Haskell/Tungsten/README.md), [`src/Kotlin/Tungsten`](../../src/Kotlin/Tungsten/README.md),
   and [`src/Rust/Tungsten`](../../src/Rust/Tungsten/README.md) port the same behavior to their
   language-local immutable value, policy, and test-runner shapes.

A port can still reveal a baseline bug. When that happens, fix or document the baseline contract
first, then carry the corrected semantics through the sibling workspaces that expose the same
capability.

## Semantic Parity Checklist

Check these items before calling a cross-language change complete:

| Concern | Parity question |
| --- | --- |
| Persistence | Do mutation-shaped operations return a new version while preserving every retained old version? |
| Structural sharing | Are no-op updates, unchanged subtrees, and root-sharing behavior documented in the language's own terms? |
| Policy preservation | Are hash, equality, comparison, measure, ownership, and callback policies preserved across derived versions? |
| Ordering | Are enumeration, sorted order, tie-breaking, rank, interval, rope, and text-boundary semantics equivalent where exposed? |
| Failure behavior | Do duplicate-key, absent-key, empty-collection, invalid-rank, allocation, and callback failures match the documented contract? |
| Ownership and lifetime | Are C# references, C++ values/shared nodes, C handle clone/destroy rules, Haskell immutable values, Kotlin/JVM references, and Rust owned values/borrows/`Arc` sharing all respected by examples and tests? |
| Complexity and allocation | Do docs and tests protect the promised asymptotic shape and hot-path allocation behavior? |
| Concurrency | Are immutable publication and family-specific reference-counting rules documented without overstating guarantees? |
| Validation | Do tests cover the affected behavior in every touched workspace, including model or property tests when those are the relevant evidence? |
| Documentation | Are API specs, validation guides, test READMEs, README summaries, catalog entries, test-suite map rows, and port notes updated together? |

## API Shape Mapping

Do not copy names mechanically. Preserve contracts while using each language's natural surface:

| Concept | C# shape | C++ shape | C shape | Rust shape |
| --- | --- | --- | --- | --- |
| Immutable update | Method returns a new reference-typed collection value. | Method returns a new value object sharing immutable nodes. | Function writes a new value struct or handle through an out parameter. | Method returns a new owned value; unchanged internals may share `Arc` storage. |
| Empty factory | Static `Empty` or `Create(...)`. | `empty()` or `create(...)`. | `*_create(...)` returning an initialized value. | `new()`, `Default`, and `FromIterator`. |
| Bulk build | `CreateRange`, `SetItems`, `Union`, and sequence-based APIs. | `create_range`, range overloads, and result structs. | `*_create_range`, `*_set_many`, or `*_many` APIs over arrays/counts. | `FromIterator`, iterator-taking update methods, and Rust collection-style builders. |
| Try pattern | `bool Try...(out value)` or result tuple-like APIs. | `std::optional`, pointer-on-hit, or named result structs. | Status code plus out parameters and boolean flags. | `Option`, `Result`, and named result structs. |
| Comparer/hash policy | `IEqualityComparer<T>`, `IComparer<T>`, static measure operations. | Template policies plus stored runtime comparators where needed. | Callback tables and context pointers that must outlive the collection. | `Eq`/`Ord`, `BuildHasher`, and `MeasurePolicy<T>` traits. |
| Errors | .NET exceptions and nullable annotations. | Standard exceptions or explicit optional/result objects. | Status codes and caller-owned output storage. | `Option` for absent/out-of-range, `Result` for duplicate-key or recoverable errors, panic only for invariant construction failures. |
| Lifetime | Garbage-collected immutable objects. | RAII values over shared immutable nodes. | Explicit clone/dispose or create/destroy pairs. | Borrow-checked references, cloned owned values on removal, and `Arc` for shared immutable storage. |

Kotlin ports should preserve the same observable contracts with idiomatic JVM shapes: immutable return
values, `null` for miss paths, runtime policy/comparator objects when type-level policies would be
unnatural, and explicit result records or exceptions for duplicate-key contracts.

## Change Workflow

1. Locate the data-structure family in the [catalog](../reference/data-structure-catalog.md) and
   the owning workspaces in the [workspace map](../reference/workspace-map.md).
2. Read the managed API spec, the relevant native API notes/specs, and the public headers or public
   C# source for the touched surface.
3. Decide whether the change is a semantic contract change, a port-only bug fix, or an intentional
   language-specific divergence.
4. Update implementation and tests in every workspace whose exposed capability is affected. If a
   sibling port intentionally stays different, state the reason in that workspace's API notes.
5. Update docs at the narrowest accurate layer: workspace API specs for contracts, workspace README
   files for surface summaries, repository reference docs for cross-workspace inventory, and guides
   when the workflow or validation rule changes.
6. Run the validation commands from [build-and-validation.md](build-and-validation.md) for every
   affected workspace. Use the relevant workspace validation guide and
   [test suite map](../reference/test-suite-map.md) for coverage expectations, stress controls,
   sample-smoke hooks, benchmark boundaries, and exact evidence wording. Run repository-owned
   Markdown link and stale-path checks for docs changes.
7. Commit only after the evidence matches the scope of the claim. A C# unit test does not prove a C,
   Kotlin, or Rust port is aligned; a successful build does not prove a changed ordering or allocation contract.

## HAMT-Specific Checks

For map/set changes, verify these contracts across C#, C++, C, Haskell, Kotlin, and Rust where exposed:

- 32-way bitmap-indexed trie shape over 32 hash bits.
- Immutable equal-hash collision buckets with linear equality probing.
- Last-wins behavior for bulk set/update operations.
- Duplicate-key rejection for `Add`/`add`, including no-allocation duplicate try-add paths where
  documented.
- Preservation of originally stored key/value objects when an equivalent no-op replacement occurs.
- `TryGetKey` / `try_get_key` and `TryGetValue` / `try_get_value` recovery semantics.
- Set algebra comparer/policy behavior and any temporary materialization costs.
- Stable-but-unspecified enumeration order for unchanged versions.
- Structural sharing and no-op root/instance behavior expressed in each language's ownership model.
- For Merkle ports, byte-identical policy domains, `MST2` blocks, and `MSP2` queries; finite resource
  budgets enforced before untrusted allocation or decoding; complete closure validation; atomic
  publication; and merge semantics that distinguish deletion from a present nullable value.

Primary semantic docs:

- [C# HAMT API specification](../../src/CSharp/docs/Hamt/api-specification.md)
- [C++ HAMT API specification](../../src/Cpp/Hamt/docs/api-specification.md)
- [C HAMT API specification](../../src/C/Hamt/docs/api-specification.md)
- [Haskell HAMT workspace](../../src/Haskell/Hamt/README.md)
- [Kotlin HAMT API notes](../../src/Kotlin/Hamt/docs/api-notes.md)
- [Rust HAMT API notes](../../src/Rust/Hamt/docs/api-notes.md)

Validation guides:

- [C# HAMT validation](../../src/CSharp/docs/Hamt/validation.md)
- [C++ HAMT validation](../../src/Cpp/Hamt/docs/validation.md)
- [C HAMT validation](../../src/C/Hamt/docs/validation.md)
- [Haskell HAMT tests](../../src/Haskell/Hamt/test/README.md)
- [Kotlin HAMT validation](../../src/Kotlin/Hamt/docs/validation.md)
- [Rust HAMT validation](../../src/Rust/Hamt/docs/validation.md)

## FingerTree-Specific Checks

For finger-tree-family changes, verify these contracts across the relevant C#, C++, C, Haskell, Kotlin, and Rust surfaces:

- Tuned deque and general measured tree remain separate when the language exposes both.
- Measure policies obey monoid identity and associativity assumptions used by split, locate, and
  derived collections.
- Split/locate boundary semantics match, including miss paths that still return meaningful
  measure-before values where the API exposes them.
- Sorted bag/set/map rank, duplicate, comparer, and range semantics match the local API notes.
- Priority queues preserve the documented priority ordering and equal-priority tie behavior.
- Brodal-Okasaki heap ports preserve comparator-identity-gated meld, concrete representatives,
  O(1) worst-case insert/meld/minimum, logarithmic delete-min, and immutable snapshot sharing.
- Priority-search-queue ports preserve first equivalent key representatives, exact no-op reuse,
  priority-then-key winner ordering, nullable-safe result shapes where needed, and winner-pruned
  inclusive key-range/priority-threshold traversal over one persistent balanced tree.
- Interval trees use the documented endpoint comparison, overlap, containment, and removal rules.
- Ropes preserve chunked persistence, split/concat/indexing semantics, and text newline navigation
  rules where exposed.
- DABA Lite ports preserve FIFO order, six-cursor region equations, bounded callback counts,
  callback-failure atomicity, and the no-iteration surface; clear and concurrency claims must match
  tracing-GC versus deterministic-ownership semantics rather than being copied mechanically.
- Lazy middle and measure publication rules are not weakened when changing core internals.
- Concurrency docs distinguish immutable snapshot reads from mutation of handles or reference counts.

Primary semantic docs:

- [C# FingerTree API specification](../../src/CSharp/docs/FingerTree/api-specification.md)
- [C# persistence and concurrency notes](../../src/CSharp/docs/FingerTree/persistence-and-concurrency.md)
- [C++ FingerTree API notes](../../src/Cpp/FingerTree/docs/api-notes.md)
- [C++ FingerTree implementation notes](../../src/Cpp/FingerTree/docs/implementation-notes.md)
- [C FingerTree API notes](../../src/C/FingerTree/docs/api-notes.md)
- [Haskell FingerTree workspace](../../src/Haskell/FingerTree/README.md)
- [Kotlin FingerTree API notes](../../src/Kotlin/FingerTree/docs/api-notes.md)
- [Rust FingerTree API notes](../../src/Rust/FingerTree/docs/api-notes.md)

Validation guides:

- [C# FingerTree validation](../../src/CSharp/docs/FingerTree/validation.md)
- [C++ FingerTree validation](../../src/Cpp/FingerTree/docs/validation.md)
- [C FingerTree validation](../../src/C/FingerTree/docs/validation.md)
- [Haskell FingerTree tests](../../src/Haskell/FingerTree/test/README.md)
- [Kotlin FingerTree validation](../../src/Kotlin/FingerTree/docs/validation.md)
- [Rust FingerTree validation](../../src/Rust/FingerTree/docs/validation.md)

## Validation Evidence

Use the narrowest command set that covers the changed behavior, and broaden it when the behavior
crosses ports:

| Change shape | Required evidence |
| --- | --- |
| Documentation-only cross-reference or index change | Markdown link checker, stale-path scan, and `git diff --check`. |
| Public API contract change in one workspace | That workspace's validation-guide command plus updated API docs. |
| Behavior intended to match across ports | Validation-guide commands for every affected language workspace. |
| Complexity, allocation, or concurrency claim | Tests or benchmarks that actually exercise the claimed hot path or publication behavior. |
| Test runner, coverage-map, sample-smoke, or stress-control change | Updated local tests README, workspace validation guide, and [test-suite map](../reference/test-suite-map.md), plus the runner command that proves the changed path. |
| Build command, preset, or layout change | The command in [build-and-validation.md](build-and-validation.md) and the local validation guide for each affected workspace. |

Record validation in commit messages or final notes with the command and what it proves. If a command
is skipped because the change is docs-only or outside that workspace, say so rather than implying
broader evidence.

## Documenting Intentional Divergence

Intentional differences are healthy when they are language-driven. They become bugs when readers have
to rediscover them. For each divergence:

- describe the shared semantic contract first;
- describe the local shape and why it differs;
- state whether the difference affects behavior, error reporting, ownership, allocation, or only naming;
- add or update tests that lock the local behavior;
- link from the relevant workspace API notes or spec;
- update the catalog only when the public data-structure surface itself changes.
