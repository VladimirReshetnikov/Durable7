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

## Tungsten Is An Application-Specific Leaf

Tungsten collections are governed by the Tungsten project and observed Wolfram-kernel behavior.
They may change when that behavior is newly discovered or reinterpreted, and the workspaces may
eventually move out of this repository. C# Tungsten documentation is authoritative only for sibling
Tungsten ports.

Tungsten may portably consume general HAMT/FingerTree families, but no general-purpose collection or
non-Tungsten workspace may depend on a Tungsten package/type or use its contract as a baseline. A
generally useful mechanism must be forked into an independently owned implementation with its own
API, contracts, tests, and parity decision. Kernel-driven changes then flow across Tungsten ports
only; neither the fork nor any general family inherits them automatically.
See the detailed
[Tungsten application-leaf dependency boundary](../reference/tungsten-application-leaf-boundary.md)
before extracting or generalizing a Tungsten mechanism.

## Authoritative Inputs

| Input | Use it for |
| --- | --- |
| Managed API specs under `src/CSharp/*/docs` | Primary semantic contract for the owning general collection family. Tungsten specs are authoritative only inside the sibling Tungsten port lineage. |
| Native API specs and public headers under `src/C/*` and `src/Cpp/*` | Idiomatic C and C++ surface shape, ownership model, and local divergences. |
| Kotlin API notes under `src/Kotlin/*/docs` | Kotlin/JVM value semantics, null/result shapes, tool bootstrap, persistent representation, complexity, and intentional engine differences. |
| Rust API notes under `src/Rust/*/docs` | Rust value semantics, `Result`/`Option` shape, Cargo validation, and checkpoint divergences. |
| [TypeScript API notes](../../src/TypeScript/docs/api-notes.md) | Strict ESM value semantics, JavaScript runtime mappings, isolate-local concurrency, and intentional engine differences. |
| [Python API notes](../../src/Python/docs/api-notes.md) | Python 3.11+ naming and result shapes, measured-AVL checkpoints, lock-coordinated concurrency, and Unicode-code-point text positions. |
| [OCaml API notes](../../src/OCaml/docs/api-notes.md) | OCaml module/functor shape, mutex-backed snapshots, Unicode-scalar text positions, and documented algorithmic checkpoint boundaries. |
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
7. [`src/TypeScript`](../../src/TypeScript/README.md) ports the contract to strict ESM with
   JavaScript-native policies, path-copy editing sessions, and an isolate-local snapshot facade.
8. [`src/Python`](../../src/Python/README.md) ports the contract to typed Python values with runtime
   `HashPolicy`, path-copy editing sessions, a lock-coordinated thread-safe snapshot facade, and
   Python-native exception/optional result shapes.
9. [`src/OCaml`](../../src/OCaml/README.md) ports the contract to immutable OCaml values with explicit
   runtime hash policies, path-copy editing sessions, mutex-backed snapshots, and `option`/`result`
   miss and failure shapes.

C#, TypeScript, Python, and OCaml additionally expose one-descent persistent map factories and
`PersistentHashBag`; TypeScript and Python expose reusable construction-only CHAMP bulk builders,
and TypeScript, Python, and OCaml expose all six transient-set relation predicates. C++ and Rust already expose their corresponding
public construction-only builders. These staging builders are not editing sessions: they own
unpublished mutable nodes, may be reused after freeze, and each frozen persistent snapshot must be
detached from later builder mutation.

OCaml's reusable `Bulk_builder` preserves detached freeze and representative semantics, but each
edit currently delegates to the persistent path-copy kernel. It is a staging convenience and makes
no unpublished-mutable-node or construction-throughput claim.

The one-way CHAMP map/set editing lifecycle now exists in all nine workspaces. Preserve these shared
semantics when changing it: O(1)-in-trie adoption and terminal publication, one logical owner,
one-way consumption, exact policy and stored-representative preservation, retained-source
isolation, unchanged-root identity after logical no-ops, receiver-policy set relations, and
failure-atomic point edits.

Do not mechanically copy the C# representation claim into a sibling port. C# alone currently has
the optimized owner-token kernel: it mutates token-owned nodes and path-copies shared/sealed nodes.
C, C++, Haskell, Kotlin, Rust, TypeScript, Python, and OCaml expose semantic lifecycle facades whose changed
point edits invoke the persistent path-copying kernel; their adoption/publication are O(1) in trie
size, but they make no edit-throughput or allocation-win claim. Keep each language's lifecycle
shape explicit:

- C clone handles alias one ref-counted session state, share consumption, and surface consumed or
  modified-iterator conditions through status codes.
- C++ sessions are move-only and rvalue-published. A throwing custom policy move terminally
  invalidates a moved source and, for assignment, its destination; publication has the documented
  no-retry/content-preservation caveat. Nothrow policy moves avoid both exceptional boundaries.
- Haskell sessions live in `IO` and use candidate-before-masked-commit publication into their
  `IORef` state.
- Kotlin rejects post-publication access dynamically and binds acquired views to the session version.
- Rust consumes the session through `into_persistent(self)`, expressing use-after-publication
  prevention in ownership rather than a runtime consumed state.
- TypeScript and Python reject post-publication access dynamically; their iterators capture a
  session version, survive logical no-ops, and reject subsequent content changes.
- OCaml sessions expose the same terminal publication rule through an explicit consumed state;
  changed edits retain the persistent path-copy kernel and make no owner-token performance claim.

The policy-bound Merkle search tree is complete across all nine languages. Every port pins the
SHA-256 domain, key framing, empty digest, canonical `MST2` block bytes, seven verification budgets,
`MSP2` point/range proofs, closure-pruned synchronization, and no-partial-result three-way merge.
Language-local ownership and callback shapes differ—pure successor stores in Haskell, synchronized
managed/native stores elsewhere, and type-erased fallible callbacks in C—but golden blocks and
every accepted/rejected trust-boundary input must remain cross-language compatible.

The Ctrie is an intentional parity exception. `ConcurrentHashTrie<TKey, TValue>` and its Kotlin/JVM
counterpart are managed-runtime mutable structures whose lock-free indirection-node protocol relies
on tracing garbage collection and offers O(1) immutable generation snapshots. TypeScript exposes a
synchronous isolate-local facade without a cross-worker progress claim. Python and OCaml expose thread-safe,
lock-coordinated facades over persistent CHAMP roots: writes are serialized and snapshots remain O(1),
but neither is the GCAS/RDCSS Ctrie or makes a lock-free claim. Keep observable map, generation,
snapshot, and stored-key contracts aligned while preserving those progress distinctions. Do not
treat the absence of C, C++, Rust, or Haskell GCAS/RDCSS ports as drift: native versions require an explicit
epoch/hazard-pointer reclamation design, while a pure Haskell port would be a different structure.
Promoting another language requires a separately reviewed reclamation and concurrency contract, not
a mechanical HAMT port.

DABA Lite is another deliberately mutable member, but its algorithm is portable independently of
its lifetime policy. C#, C, C++, Kotlin/JVM, Rust, TypeScript, and Python preserve FIFO ordering, the six-cursor
schedule, three/two/one combine ceilings, callback-atomic or status-atomic mutators, callback-free
structural validation, and the absence of raw-value iteration. C#, Kotlin/JVM, TypeScript, and Python
can replace the active chunk chain in O(1); Python leaves eventual cycle reclamation to the runtime.
C, C++, and safe Rust instead clear in O(n + c), iteratively
destroying `n` owned values in `c` chunks; deferring that work would violate prompt reclamation. C's
existing callbacks are infallible and non-reentrant by contract, C++ requires no-throw moves so its
planned publication phase cannot tear, and Rust's stable `Rc` cursor representation makes the
mutable core `!Send` and `!Sync`. Treat these ownership/concurrency differences as explicit language
semantics, not parity failures. A pure Haskell value would not preserve DABA's ephemeral incremental
schedule, so omission there is intentional.

OCaml exposes the same FIFO aggregation operations and callback-failure atomicity as a semantic
checkpoint, but its current immutable queue facade does not claim the specialized six-cursor
schedule or worst-case callback bounds. Treat those missing performance guarantees as an explicit
checkpoint boundary.

The canonical sorted-set rank contract is implemented in all nine languages. Every port derives a
32-byte HMAC key as SHA-256 of ASCII `ZZT2`
followed by the public seed in big-endian order, feeds an eight-byte big-endian equivalence-class hash to
HMAC-SHA-256, and interprets the first three big-endian words as leading-zero geometric rank,
unsigned secondary rank, and digest content. The established zip-tree ports also preserve random-key and caller-keyed modes, the
minimum 32-byte caller-key contract, comparer-smaller final priority tie, first-representative bulk
semantics, policy-object identity for canonical algebra, and receiver-comparer semantics for
cross-policy equality. A port must test exact rank vectors, unsigned secondary ordering,
equivalence/hash incoherence, insertion-order-independent topology, deep colliding chains, and
concurrent lazy-digest publication where the language exposes shared readers.

OCaml reproduces the exact HMAC rank vectors and insertion-history-independent sorted contents, but
its current public policy factory is seeded and its storage delegates to the persistent sorted-set
facade. It therefore makes no random/caller-key factory, canonical zip-tree topology, lazy-digest
publication, or zip-zip complexity claim.

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

Python uses `hashlib`/`hmac`, retains an owned rank key, and lock-protects lazy digest publication.
Because Python's built-in string/bytes hashes are randomized, its natural factory supplies a pinned
stable rank hash only for supported immutable values and requires an explicit equivalence-coherent
rank hash for application objects.

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
   semantics. Its `ft_rope_cursor`, `ft_measured_rope_cursor`, and nominal `ft_text_rope_cursor` are
   explicit-lifetime snapshot-plus-gap checkpoints with success-only publication and copied peeks.
4. [`src/Haskell/FingerTree`](../../src/Haskell/FingerTree/README.md) ports the family to Haskell
   with a general measured tree, size-measured deque, reversible deque, derived collections, the
   explicitly identified policy-canonical zip-zip set, priority queues, intervals, ropes, and text
   helpers, including positional, measured, and `Char`-element text snapshot-plus-gap cursor
   checkpoints. These preserve ordered measures and pure failure semantics without importing the C#
   focused cursor representation, cache, allocation, or amortized-locality claims.
5. [`src/Kotlin/FingerTree`](../../src/Kotlin/FingerTree/README.md) ports the family to Kotlin/JVM over
   structurally shared measured AVL sequences with cached monoidal summaries and runtime
   measure/comparator policies, plus the policy-canonical zip-zip sorted set. Its positional,
   measured, and UTF-16 text cursors are snapshot-plus-gap semantic checkpoints with no C# focused
   cursor representation or amortized-locality claim; its API notes state the strict-AVL versus
   lazy-digit-spine costs and segregate the mutable managed DABA Lite member.
6. [`src/Rust/FingerTree`](../../src/Rust/FingerTree/README.md) is the Rust semantic checkpoint for
   the same family names. It preserves immutable snapshot behavior now; the public facades use
   structurally shared Rust tree storage and include the policy-canonical zip-zip sorted set, while
   the workspace documents the remaining asymptotic boundary until the lazy measured spine is
   ported through the whole family. Its separate DABA Lite core is mutable, single-threaded, and
   documents deterministic-drop clear semantics.
7. [`src/TypeScript`](../../src/TypeScript/README.md) ports the measured family and shipped derived
   cores to strict ESM with persistent JavaScript gap cursors and a mutable isolate-local DABA Lite.
8. [`src/Python`](../../src/Python/README.md) ports the family over immutable measured AVL and RRB
   substrates. It exposes deque/measured, sorted, priority, interval, canonical zip-zip, Brodal,
   priority-search, rope, and snapshot-plus-gap cursor surfaces; text positions count Python Unicode
   code points. Its mutable DABA Lite is single-threaded and separately documented.
9. [`src/OCaml`](../../src/OCaml/README.md) ports the family through immutable measured sequences and
   language-local derived facades, including Unicode-scalar text cursors. Its API notes identify
   checkpoint implementations that preserve observable behavior without inheriting specialized
   topology or worst-case bounds.

Ordered-set lineage:

This is a neutral general composition lineage, independent of the application-specific Tungsten
family. The ports compose public HAMT membership/stamp indexes with public persistent ordered
sequences and own their contracts, sparse-label mechanics, tests, and evolution separately.

1. [C# Ordered](../../src/CSharp/docs/Ordered/overview.md) is the semantic reference for
   `PersistentOrderedSet<T>`: first representatives, addition without implicit movement, explicit
   movement, positional ranges, stable one-shot sorting, receiver-policy algebra, no-op identity,
   sparse relabeling, and retained versions.
2. [`src/TypeScript`](../../src/TypeScript/README.md) ports that contract to a neutral strict-ESM
   `ordered` export with discriminated lookup and explicit removal results, keeping absence distinct
   from a stored `undefined`.
3. [`src/Python`](../../src/Python/README.md) ports it to the neutral typed `ordered` module with
   named result objects and Python-native indexing and exception shapes.
4. [`src/C/Ordered`](../../src/C/Ordered/README.md), [`src/Cpp/Ordered`](../../src/Cpp/Ordered/README.md),
   [`src/Haskell/Ordered`](../../src/Haskell/Ordered/README.md),
   [`src/Kotlin/Ordered`](../../src/Kotlin/Ordered/README.md), and
   [`src/Rust/Ordered`](../../src/Rust/Ordered/README.md) provide the native and functional sibling ports.
5. [`src/OCaml`](../../src/OCaml/README.md) ports the neutral ordered set, map, and grouped multimap
   with persistent values, explicit movement, positional operations, stable sorting, and retained
   first representatives.

No Ordered port references a Tungsten package, type, source file, test oracle, or privileged API.
Port Ordered changes among these neutral workspaces only when the general contract changes; do not
propagate a kernel-driven Tungsten change into this lineage.

Tungsten collections lineage:

This is a family-local application lineage, not a general collection lineage. Its behavior may
change with new kernel evidence. Port changes within it across sibling Tungsten workspaces; fork any
generally useful mechanism into a separately owned family instead of importing, wrapping, or
refactoring Tungsten into a dependency.

1. [C# Tungsten collections](../../src/CSharp/docs/Tungsten/overview.md) are the baseline only for
   sibling Tungsten ports of the `PersistentList<T>` facade and kernel-verified
   `PersistentAssociation<TKey, TValue>` ordering rules.
2. [`src/Cpp/Tungsten`](../../src/Cpp/Tungsten/README.md) ports the family to C++23 value types over
   the C++ HAMT and FingerTree substrates.
3. [`src/C/Tungsten`](../../src/C/Tungsten/README.md) ports the family to type-erased C value structs
   with explicit clone/dispose ownership, C HAMT lookup, and an internal stamp-ordered AVL sequence.
4. [`src/Haskell/Tungsten`](../../src/Haskell/Tungsten/README.md), [`src/Kotlin/Tungsten`](../../src/Kotlin/Tungsten/README.md),
   and [`src/Rust/Tungsten`](../../src/Rust/Tungsten/README.md) port the same behavior to their
   language-local immutable value, policy, and test-runner shapes.
5. [`src/TypeScript`](../../src/TypeScript/README.md) and [`src/Python`](../../src/Python/README.md)
   port the same application-leaf `PersistentList`/`PersistentAssociation` vocabulary into their
   language-local packages.
6. [`src/OCaml`](../../src/OCaml/README.md) ports that vocabulary into a separate application-leaf
   library that depends on the general OCaml HAMT and FingerTree libraries; no dependency points
   back from a general library into Tungsten.

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
| Ownership and lifetime | Are C# references, C++ values/shared nodes, C handle clone/destroy rules, Haskell and OCaml immutable values, Kotlin/JVM references, Rust owned values/borrows/`Arc` sharing, JavaScript objects, and Python references all respected by examples and tests? |
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

TypeScript and Python ports likewise preserve contracts with runtime-native shapes: ESM/camelCase and
`undefined`/exceptions for TypeScript; importable typed modules, snake_case, `None`, named dataclass
results, and exceptions for Python. Neither runtime shape authorizes a stronger concurrency or
owner-token performance claim than the local implementation proves.

OCaml ports use module-qualified immutable values, `option` for ordinary miss paths, `result` for
recoverable contract failures, first-class policy records, and explicit mutable handles only for
transient or synchronized facades. Those shapes do not imply stronger topology, complexity, or
progress guarantees than the OCaml API notes state.

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

For map/set changes, verify these contracts across C#, C++, C, Haskell, Kotlin, Rust, TypeScript, Python, and OCaml where exposed:

- 32-way bitmap-indexed trie shape over 32 hash bits.
- Immutable equal-hash collision buckets with linear equality probing.
- Last-wins behavior for bulk set/update operations.
- For public construction-only builders, genuine mutable unpublished leaf/collision/bitmap nodes,
  first-key and last-distinct-value representative rules, the final 30-bit hash shift, reusable
  post-freeze state, and detached frozen snapshots. Map/set range factories and bulk-producing set
  operations should route through the builder where the local public contract says they do.
- For one-descent map factories, eager validation of every supplied callback before hashing; one
  hash and one trie descent; exactly one selected add/update factory call; caller-key use on a miss;
  stored-key retention on a hit; exact source identity on a logical no-op; nullable-safe result
  shapes; and failure-atomic publication.
- For persistent hash bags, positive bounded per-class multiplicities, separate distinct and total
  cardinalities, zero-delta identity, first representatives, expanded plus distinct enumeration,
  eager receiver-policy normalization, and checked max/min/subtract/sum algebra. Preserve each
  language's documented wide total-count type rather than forcing the C# `long` shape mechanically.
- Duplicate-key rejection for `Add`/`add`, including no-allocation duplicate try-add paths where
  documented.
- Preservation of originally stored key/value objects when an equivalent no-op replacement occurs.
- `TryGetKey` / `try_get_key` and `TryGetValue` / `try_get_value` recovery semantics.
- Set algebra comparer/policy behavior and any temporary materialization costs.
- Stable-but-unspecified enumeration order for unchanged versions.
- Structural sharing and no-op root/instance behavior expressed in each language's ownership model.
- For one-way editing sessions, adoption/publication without a trie walk, single-owner consumption,
  active-read and version-bound-iteration behavior, exact clean/no-op identity, retained-source
  isolation, policy/representative preservation, all six receiver-policy set relations (subset,
  proper subset, superset, proper superset, overlap, and equality), and failure-atomic edits.
  Require separate evidence before claiming owner-token in-place edits or a performance win.
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
- [TypeScript API notes](../../src/TypeScript/docs/api-notes.md)
- [Python API notes](../../src/Python/docs/api-notes.md)
- [OCaml API notes](../../src/OCaml/docs/api-notes.md)

Validation guides:

- [C# HAMT validation](../../src/CSharp/docs/Hamt/validation.md)
- [C++ HAMT validation](../../src/Cpp/Hamt/docs/validation.md)
- [C HAMT validation](../../src/C/Hamt/docs/validation.md)
- [Haskell HAMT tests](../../src/Haskell/Hamt/test/README.md)
- [Kotlin HAMT validation](../../src/Kotlin/Hamt/docs/validation.md)
- [Rust HAMT validation](../../src/Rust/Hamt/docs/validation.md)
- [TypeScript validation](../../src/TypeScript/docs/validation.md)
- [Python validation](../../src/Python/docs/validation.md)
- [OCaml validation](../../src/OCaml/docs/validation.md)

## FingerTree-Specific Checks

For finger-tree-family changes, verify these contracts across the relevant C#, C++, C, Haskell, Kotlin, Rust, TypeScript, Python, and OCaml surfaces:

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
- Positional and measured cursor peeks must distinguish a missing neighbor from a present nullable
  or dynamic-runtime sentinel value. Use an explicit presence/result shape where `undefined` or
  `None` can itself be stored.
- Cursor `ReplaceNext` is an unconditional edit: it creates a successor version even when element
  equality would say the replacement is unchanged. A measured cursor must invoke the element-
  measure callback for the supplied replacement and publish nothing if that callback fails.
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
- [TypeScript API notes](../../src/TypeScript/docs/api-notes.md)
- [Python API notes](../../src/Python/docs/api-notes.md)
- [OCaml API notes](../../src/OCaml/docs/api-notes.md)

Validation guides:

- [C# FingerTree validation](../../src/CSharp/docs/FingerTree/validation.md)
- [C++ FingerTree validation](../../src/Cpp/FingerTree/docs/validation.md)
- [C FingerTree validation](../../src/C/FingerTree/docs/validation.md)
- [Haskell FingerTree tests](../../src/Haskell/FingerTree/test/README.md)
- [Kotlin FingerTree validation](../../src/Kotlin/FingerTree/docs/validation.md)
- [Rust FingerTree validation](../../src/Rust/FingerTree/docs/validation.md)
- [TypeScript validation](../../src/TypeScript/docs/validation.md)
- [Python validation](../../src/Python/docs/validation.md)
- [OCaml validation](../../src/OCaml/docs/validation.md)

## Ordered-Set-Specific Checks

For `PersistentOrderedSet` changes, verify the shared general contract across all nine languages:

- neutral package ownership and a one-way dependency on public HAMT/FingerTree substrates, with no
  production, test, documentation-oracle, or privileged-access dependency on Tungsten;
- one stored representative and one strictly ordered stamp per equality class, with exact agreement
  between the hash index and ordered sequence;
- first-occurrence construction, addition that never moves an existing class, explicit movement
  that retains its representative, and exact no-op identity;
- positional lookup, insertion, removal, ranges, take/drop, reverse, and stable one-shot sort with
  eager bounds validation and comparer-preserving empty results;
- receiver-policy union/intersection/difference/symmetric-difference order and all six set relations,
  including eager normalization of foreign-policy arguments;
- sparse-label exhaustion and relabel histories under branching persistence, without promising a
  Tungsten constant or cross-branch amortized bound;
- presence-safe lookup/removal result shapes for stored `undefined`/`None` values; and
- retained-version immutability, callback-failure atomicity, invariant validation, generated
  comparer-aware histories, and language-appropriate concurrent-read evidence.

Primary semantic and validation docs:

- [C# Ordered overview](../../src/CSharp/docs/Ordered/overview.md)
- [C# Ordered API specification](../../src/CSharp/docs/Ordered/api-specification.md)
- [C# Ordered validation](../../src/CSharp/docs/Ordered/validation.md)
- [C Ordered workspace](../../src/C/Ordered/README.md)
- [C++ Ordered workspace](../../src/Cpp/Ordered/README.md)
- [Haskell Ordered workspace](../../src/Haskell/Ordered/README.md)
- [Kotlin Ordered workspace](../../src/Kotlin/Ordered/README.md)
- [Rust Ordered workspace](../../src/Rust/Ordered/README.md)
- [TypeScript API notes](../../src/TypeScript/docs/api-notes.md)
- [TypeScript validation](../../src/TypeScript/docs/validation.md)
- [Python API notes](../../src/Python/docs/api-notes.md)
- [Python validation](../../src/Python/docs/validation.md)
- [OCaml API notes](../../src/OCaml/docs/api-notes.md)
- [OCaml validation](../../src/OCaml/docs/validation.md)
- [Cross-language completion audit](../reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md)

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
