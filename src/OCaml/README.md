# OCaml Workspace

- Created (UTC): 2026-07-17T17:58:43Z
- Repository HEAD: b2312bd763b0ad7e9bc1628963dcea5ec43b5e68
- Audience: Maintainers and AI agents working on OCaml ports of repository-owned data structures
- Scope: OCaml package layout, policy foundations, build entry points, and validation

This workspace is the OCaml port of the repository-owned persistent data structures and numerics.
It uses immutable OCaml values for published snapshots, runtime policy records where the source
families retain hashing or comparison behavior, and separately identified mutable builders,
editing sessions, cursors, and streaming cores.

The general-purpose modules under `lib/numerics`, `lib/hamt`, `lib/finger_tree`, and `lib/ordered`
must not depend on `lib/tungsten`. Tungsten is an application-specific leaf and may consume the
general libraries only in that direction.

## Layout

| Path | Responsibility |
| --- | --- |
| `lib/common` | Runtime hashing and comparison policies shared by the general families |
| `lib/numerics` | Fixed-width and sparse integer values |
| `lib/hamt` | Persistent hash, Patricia, concurrent-snapshot, and authenticated-map families |
| `lib/finger_tree` | Persistent sequence, sorted, priority, interval, rope, and streaming families |
| `lib/ordered` | Independently owned insertion-ordered set, map, and grouped multimap |
| `lib/tungsten` | Application-specific persistent List and Association collections |
| `tests` | Alcotest examples, regression tests, models, and QCheck properties by family |

## Toolchain

The workspace requires OCaml 4.14 or newer, Dune 3.20 or newer, Zarith, and Digestif. Alcotest and
QCheck are test-only dependencies; odoc is the documentation dependency. The package constraints
are recorded in `tools-data-structures.opam`.

## Build And Test

From `src/OCaml`:

```powershell
opam install . --deps-only --with-test --with-doc
opam exec -- dune build -j 1
.\test.ps1
opam exec -- dune build @doc -j 1
```

Use `-Workspace Common`, `Numerics`, `Hamt`, `FingerTree`, `Ordered`, or `Tungsten` for a focused
test run. The launcher enforces one opam/Dune job and imports the repository's noninteractive test
failure handling.

The shipped numerics modules expose `UInt256`/`Int256`, `UInt512`/`Int512`,
`UInt1024`/`Int1024`, `Bit_converter_ex`, and non-negative `Sparse_integer` values. The fixed-width
modules preserve modulo arithmetic, checked overflow, signed truncating division, width-constrained
bit operations, and exact 32/64/128-byte two's-complement conversion. Zarith is an implementation
substrate only; the public fixed-width contract never widens at runtime.

The HAMT core is a persistent 32-way bitmap-indexed trie with immutable equal-hash collision
buckets. `Persistent_hamt` retains key representatives and policies, exposes one-descent
`get_or_add`/`add_or_update`, reusable detached-freeze builders, and one-way path-copy transient
sessions. `Persistent_hash_set`, `Persistent_hash_bag`, and strict `Persistent_bi_map` compose that
core without introducing mutable published state.

The HAMT composition layer adds set-valued `Persistent_hash_multimap`, bidirectional
`Persistent_relation`, strict presence-aware and invertible `Persistent_map_patch`, explicit-vertex
`Persistent_directed_graph`, and `Persistent_indexed_map` with one automatically maintained
nonunique secondary index. Multi-index edits validate or calculate every successor before the new
facade is returned.

`Persistent_patricia` provides structurally shared big-endian 32- and 64-bit integer maps and sets.
`Concurrent_hash_trie` is the OCaml runtime checkpoint for a thread-safe live map: a mutex
serializes writers and readers capture immutable HAMT roots in O(1). It preserves generation and
snapshot behavior while explicitly making no lock-free progress claim.

`Merkle_search_tree` provides the policy-bound authenticated ordered map. Canonical codecs,
SHA-256 policy domains, and B=16 construction produce exact `MST2` blocks shared with the sibling
ports. Verified stores and packs enforce seven independent resource limits, reject malformed or
noncanonical graphs before publication, and support missing-block synchronization. `MSP2`
membership, nonmembership, and range proofs account for every supplied block and query byte, while
typed three-way merge distinguishes a missing key from a present encoded value. The current proof
producer deliberately sends a complete authenticated block set; verification and wire semantics
are exact, but proof minimization is not claimed.

The FingerTree foundation is a structurally shared measured sequence with cached monoidal
summaries, balanced concatenation, indexed editing and splitting, range measurement, and prefix
search. `Persistent_deque` specializes it with the size monoid, `Measured_sequence` retains an
arbitrary runtime measurement policy, and `Reversible_deque` adds an O(1) logical reversal bit.
Published snapshots remain immutable across end edits, concatenation, splits, and indexed changes.

The first derived sequence facades add order-statistic `Sorted_bag`, representative-retaining
`Sorted_set` and `Sorted_map`, a stable `Priority_queue`, max-high `Interval_tree`, and the
payload-bearing `Persistent_interval_map`. Sorted collections retain comparator identity for
cross-value operations; set/map builders are mutable construction aids whose frozen snapshots stay
detached from later edits. Exact interval identity remains separate from overlap and point queries.

Indexed sequence derivatives include `Rrb_vector` over the shared balanced sequence,
`Persistent_chunked_bit_set` with sparse rank/select and navigation, and
`Range_update_sequence` with an explicit algebra-law admission gate. The initial range-update
checkpoint rebuilds affected immutable arrays and their measures at publication, so it preserves
the sibling semantics without yet claiming the implicit-AVL implementation's logarithmic lazy
update bound. Vector builders remain reusable and publish snapshots detached from subsequent edits.

Each remaining collection-family checkpoint adds its public modules and corresponding focused tests
before the repository-level indexes claim that family as shipped.
