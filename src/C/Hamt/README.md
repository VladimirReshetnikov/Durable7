# src/C/Hamt

- Status: Active port workspace
- Created (UTC): 2026-07-02T18:18:57Z
- Repository HEAD: 3444f5ee27357d86c43db484993f8f12dfd4887c
- Audience: Maintainers implementing and reviewing the pure C HAMT port
- Scope: Project layout and validation entry points for `src/C/Hamt`

`src/C/Hamt` is the C17 port of the persistent HAMT collection family. It provides a type-erased C API
for immutable unordered collections backed by a hash-array mapped trie:

- `tds_hamt_map`, a persistent map from `void *` keys to `void *` values.
- `tds_hamt_set`, a persistent set wrapper over the map core.
- `tds_hamt_map_transient` / `tds_hamt_set_transient`, explicit one-way edit-session handles over
  the persistent CHAMP values. Adoption and terminal publication are O(1) handle operations; point
  edits deliberately reuse the persistent path-copy engine rather than claiming owner-token
  in-place-update performance.
- `tds_int_map` / `tds_long_map`, explicit-width persistent maps backed by a big-endian Patricia
  trie, plus `tds_int_set` / `tds_long_set` wrappers.
- `tds_merkle_search_tree`, a persistent ordered content-addressed map implementing the exact
  cross-language `mst-sha256-b16-v2` policy and canonical `MST2` block wire contract, with verified
  persistence, bounded import/load, `MSP2` proofs, synchronization planning, and typed three-way
  merge.

The implementation preserves the C# and C++ libraries' core shape: 32-way logical branching, five
hash bits per trie level, canonical CHAMP branches with separate data/node maps, inline type-erased
payload runs and child-only subtrie runs, immutable equal-hash collision
buckets, custom hash/equality policy callbacks, first equivalent key/item retention, no-op root
reuse, cached subtree cardinalities, and slot-aligned structural map/set algebra that prunes
pointer-identical subtries. Because this is C, ownership is explicit: maps and
sets are value structs whose roots are reference-counted, and callers use `clone`/`destroy` to manage
version lifetimes. Transient session states are also reference-counted: explicit transient clones
alias one active session, terminal publication consumes every alias, and each initialized handle is
destroyed independently.

The Patricia family sign-flips signed keys before branching, so visitor traversal is ascending
signed order for both 32- and 64-bit keys. Compressed prefixes support subtree-aware union,
intersection, and difference; map union/intersection also accept typed combining callbacks. Nodes
cache subtree cardinality, and algebra preserves a source root whenever the result is a semantic
no-op.

Maps expose policy-compatible content equality and visitor-based typed diff without requiring a
result allocator. Inline payload rebuilds retain through the configured callbacks and unwind every
completed retain when allocation fails.

The Merkle search tree uses fallible type/copy/comparison/codec/store hooks, library-owned canonical
bytes, atomic reference-counted immutable objects and nodes, strong alias-safe publication, and
allocation-free intrusive release. Its persistence tier provides immutable block/pack/proof/plan
handles, a synchronized in-memory block store, seven independent verification budgets, destination
preflight, closure-checked import, iterative frontier sync, and present-null-safe merge conflicts.
See the [local Merkle specification](docs/merkle-search-tree.md).

The HAMT and Patricia intrusive node reference counts are deliberately non-atomic. Already-retained snapshots support
concurrent read-only access, but copying, updating, clearing, or destroying versions that share a lineage must
be serialized; those operations retain or release shared nodes. Fully independent maps/sets with no shared
nodes may be updated on separate threads, subject to the thread-safety of their policy callbacks.
Transient session state and its alias count are likewise non-atomic and single-owner. Serialize all
operations across transient clones, and do not concurrently read a source/published snapshot while
an edit on a structurally shared transient lineage may retain or release its nodes.
The Merkle search tree instead uses atomic policy/object/byte/entry/node reference counts; its caller
callbacks and callback-owned contexts remain responsible for their own synchronization.

## Layout

- `include/Tools/DataStructures/Hamt/hamt.h` contains the public C API.
- `include/Tools/DataStructures/Hamt/patricia.h` contains the integer Patricia map/set API.
- `include/Tools/DataStructures/Hamt/merkle_search_tree.h` contains the Merkle policy, codec, tree,
  traversal, diff, wire-block, store, persistence, proof, sync, merge, and validation API.
- `src/hamt.c` contains the HAMT implementation.
- `src/patricia.c` contains the shared 32-/64-bit Patricia implementation.
- `src/merkle_search_tree.c` contains the canonical Merkle search tree implementation and CNG /
  OpenSSL SHA-256 backend.
- `tests/` contains the [deterministic native test executable](tests/README.md).
- `build.ps1` imports the MSVC toolchain through Scriptorium and compiles all three native test
  executables.
- `docs/api-specification.md` documents the C API adaptation and complexity guarantees.
- `docs/usage.md` provides practical policy, lifetime, update, iteration, and set-algebra examples.
- `docs/validation.md` records the local build script, warning policy, Debug/Release commands, and
  native test coverage.

## Validation

Use the local MSVC toolchain:

```powershell
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests
```

Build outputs are written under `build/<Configuration>/`, which is ignored by the repository. See
[`docs/validation.md`](docs/validation.md) for compiler flags, generated outputs, and test coverage.
