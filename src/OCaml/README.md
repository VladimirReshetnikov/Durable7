# OCaml Workspace

- Created (UTC): 2026-07-17T17:58:43Z
- Repository HEAD: b2312bd763b0ad7e9bc1628963dcea5ec43b5e68
- Audience: Maintainers and AI agents working on OCaml ports of repository-owned data structures
- Scope: OCaml package layout, policy foundations, build entry points, and validation

This workspace is the OCaml port of the repository-owned persistent data structures.
It uses immutable OCaml values for published snapshots, runtime policy records where the source
families retain hashing or comparison behavior, and separately identified mutable builders,
editing sessions, cursors, and streaming cores.

The general-purpose modules under `lib/hamt`, `lib/finger_tree`, and `lib/ordered`
general libraries only in that direction.

## Layout

| Path | Responsibility |
| --- | --- |
| `lib/common` | Runtime hashing and comparison policies shared by the general families |
| `lib/hamt` | Persistent hash, Patricia, concurrent-snapshot, and authenticated-map families |
| `lib/finger_tree` | Persistent sequence, sorted, priority, interval, rope, and streaming families |
| `lib/ordered` | Independently owned insertion-ordered set, map, and grouped multimap |
| `tests` | Alcotest examples, regression tests, models, and QCheck properties by family |

## Toolchain

The workspace requires OCaml 4.14 or newer, Dune 3.20 or newer, Zarith, Digestif, and Uutf. Alcotest and
QCheck are test-only dependencies; ocamlformat and odoc provide formatting and documentation gates. The package constraints
are recorded in `durable7.opam`.

## Build And Test

From `src/OCaml`:

```powershell
opam install . --deps-only --with-test --with-doc --with-dev-setup
opam exec -- dune clean
opam exec -- dune build -j 1 @check @fmt @doc
.\test.ps1
```

Use `-Workspace Common`, `Hamt`, `FingerTree`, or `Ordered` for a focused
test run. The launcher enforces one opam/Dune job and imports the repository's noninteractive test
failure handling.

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

`Rope`, `Measured_rope`, and their immutable snapshot-bound cursors provide split/concat editing,
indexed insertion/deletion, and cursor-local measurement. `Text_rope` validates UTF-8 through Uutf,
stores Unicode scalar values, indexes by code point, and supports newline-aware line/column mapping;
its cursor adds Unicode-safe insertion, deletion, peeking, positioning, and forward search. Cursor
edits return newly bound values and never mutate the source rope snapshot.

The remaining advanced facades are `Canonical_sorted_set` with seeded deterministic SHA-256 rank
policies, `Brodal_okasaki_heap`, the key-ordered winner-cached `Priority_search_queue`, and mutable
`Daba_lite` FIFO aggregation. They preserve canonical insertion-history independence, comparator
identity, priority-then-key tie breaking, persistent heap/queue snapshots, FIFO monoid order, and
failure-atomic callback publication. The initial heap and DABA implementations favor simple OCaml
storage and do not claim the specialized sibling cores' worst-case callback or asymptotic bounds.

The independently owned neutral ordered family provides `Persistent_ordered_set`,
`Persistent_ordered_map`, and grouped `Persistent_ordered_multimap`. Equality policy defines
identity while array position defines order; first key/value representatives survive equivalent
lookups and replacements. The facades own insertion, final-index movement, positional ranges,
reversal, stable one-shot sorting, receiver-policy set algebra, independent key/value policies, and
nested value movement. These modules depend only on general repository code.

`Persistent_association` with its kernel-specific ordering rules. Association replacement retains
the stored key and position; append/prepend deliberately move an existing class and adopt the
caller representative; indexed insertion adjusts its target after removing an earlier occurrence.
Join, key selection, ranges, reversal, and stable key/value sorting follow the same sibling contract.
No general module imports this leaf namespace.

All repository-owned collection families now have OCaml modules and focused tests in
this workspace. Repository-level navigation and validation documents identify any language-local
implementation distinctions and the exact commands used to validate the port.
