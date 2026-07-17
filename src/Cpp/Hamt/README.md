# src/Cpp/Hamt

- Status: Active port workspace
- Created (UTC): 2026-07-02T17:58:46Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Updated (UTC): 2026-07-16T22:52:15Z
- Updated against repository HEAD: 88164edb086096800b2fb32eeaa7e7a1e556e183
- Audience: Maintainers implementing and reviewing the native C++ HAMT port
- Scope: Project layout and validation entry points for `src/Cpp/Hamt`

`src/Cpp/Hamt` is the C++20 port of the C# HAMT project under
`src/CSharp/src/Tools.DataStructures.Hamt`. It provides immutable hash-trie, integer-Patricia, and
content-addressed ordered-map cores:

- `tools::data_structures::hamt::persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>`
- `tools::data_structures::hamt::persistent_hash_set<T, Hash, KeyEqual>`
- `tools::data_structures::hamt::persistent_hash_bag<T, Hash, KeyEqual>`
- `tools::data_structures::hamt::persistent_bi_map<Key, T, KeyHash, KeyEqual, ValueHash, ValueEqual>`
- `persistent_hash_multimap<Key, Value, ...>` and `persistent_relation<Left, Right, ...>`
- `persistent_int_map<T>` / `persistent_long_map<T>` and the corresponding explicit-width
  `persistent_int_set` / `persistent_long_set` types.
- `merkle_search_tree<K, V>`, `merkle_search_tree_policy<K, V>`, canonical codecs, and exact
  `mst-sha256-b16-v2` / `MST2` block output, plus verified persistence, exact `MSP2` proofs,
  synchronization, and three-way merge.

The implementation preserves the C# library's core shape: 32-way logical branching, five hash bits
per trie level, canonical CHAMP branches with separate data/node maps, compact inline payload and
child-only vectors, immutable equal-hash collision buckets, custom
hash/equality policy objects, structural sharing across versions, first equivalent key/item
retention, no-op root reuse, cached subtree cardinalities, and slot-aligned structural map/set
algebra that prunes pointer-identical subtries. Maps additionally provide one-hash, one-descent
`get_or_add` and `add_or_update` factory updates that select exactly one callable and retain stored
key/value representatives on semantic no-ops. A construction-only `bulk_builder` mutates
unpublished nodes in place, supports checked combine-on-duplicate staging, and freezes into
detached persistent maps; `create_range`, hash-bag aggregation/normalization, and set intersection
build through it. `persistent_hash_bag` adds checked 32-bit per-class multiplicities, a 64-bit
expanded total, receiver-policy union/intersection/difference/sum, and expanded/distinct/entry
enumeration. `persistent_bi_map` composes two policy-independent CHAMP maps into a strict immutable
bijection with non-displacing replacement, symmetric lookup/removal, first-representative
retention, shared-root no-op results, and O(1) value-semantic inversion.
`persistent_hash_multimap` composes the public map and set into nonempty value groups
with independent key/value policies and a checked 64-bit pair count. `persistent_relation` keeps
two such multimaps mutually inverse, normalizes representatives globally, and exposes constant-time
inverse root swapping. Separately, the map and set CHAMP facades expose move-only, one-way
`transient` editing sessions through `create_transient` and `to_transient`. Clean and logical-no-op
sessions publish the original shared root; real point edits deliberately call the immutable
path-copy operations, so this lifecycle surface makes no owner-token mutation or throughput claim.
Generation-bound iterators fail after a content change, and every later collection observation,
mutation, iteration request, or publication attempt fails deterministically after publication or
on a moved-from session. A throwing custom policy move terminally invalidates the affected source
and destination sessions and their iterator lineages before partially moved maps can be observed.
Maps also expose `map_equals` and owned typed
added/removed/changed diff. Those two operations have a caller precondition that stateful `Hash`
and `KeyEqual` objects define compatible semantics; arbitrary C++ policy objects expose no general
identity/equality operation, so the library cannot enforce the C# reference's comparer-identity
check. Because C++ collections use value semantics, identity guarantees are expressed as shared
root identity rather than object reference identity.

The integer family is a separate big-endian Patricia core. It sign-flips keys for ascending signed
iteration, compresses unary prefixes, caches subtree cardinality, and aligns prefixes for structural
union/intersection/difference. Map union/intersection have resolver overloads for overlapping values;
all algebra preserves shared roots when the semantic result is unchanged.

The Merkle search tree is a separate immutable ordered map. An explicit policy binds comparator
semantics and versioned canonical key/value codecs into the `mst-sha256-b16-v2` SHA-256 domain.
Key-derived geometric levels produce canonical B=16 wide blocks independent of update history;
updates path-copy changed blocks and share untouched subtrees. The C++ core emits byte-for-byte
cross-language `MST2` blocks, exposes exact block and shape diagnostics, and deeply validates stored
representatives, cached metadata, canonical bytes, and digests. Immutable blocks and packs, a
thread-safe in-memory store, seven bounded verification limits, complete/partial import, exact
membership/nonmembership/range proofs, closure-pruned synchronization, and present-null-safe
three-way merge extend that core without weakening move-only key/value support.

## Layout

- `include/Tools/DataStructures/Hamt/hamt.hpp` is the aggregate public include for all C++ HAMT,
  Patricia, and Merkle surfaces.
- `include/Tools/DataStructures/Hamt/persistent_hash_map.hpp` contains the template map
  implementation, construction builder, and move-only edit session.
- `include/Tools/DataStructures/Hamt/persistent_hash_set.hpp` contains the set wrapper and set
  algebra plus its map-backed edit session.
- `include/Tools/DataStructures/Hamt/persistent_hash_bag.hpp` contains the immutable unordered
  multiset, checked multiplicity operations, receiver-policy algebra, and enumeration views.
- `include/Tools/DataStructures/Hamt/persistent_bi_map.hpp` contains the strict bidirectional map
  facade, domain-conflict reporting, symmetric point edits, inversion, and invariant validation.
- `include/Tools/DataStructures/Hamt/persistent_hash_multimap.hpp` contains the set-valued multimap;
  `persistent_relation.hpp` contains the bidirectional relation built from two inverse multimaps.
- `include/Tools/DataStructures/Hamt/persistent_int_map.hpp` contains both widths of Patricia maps
  and sets.
- `include/Tools/DataStructures/Hamt/merkle_encoding.hpp` contains SHA-256 digests, strict canonical
  codecs, comparers, and policy-domain construction.
- `include/Tools/DataStructures/Hamt/merkle_search_tree.hpp` contains the immutable canonical wide
  tree, ordered-map operations, diagnostics, exact block enumeration, and deep validation.
- `include/Tools/DataStructures/Hamt/merkle_persistence.hpp` contains immutable transfer values,
  the concurrent block store, bounded verified load/import, and synchronization algorithms.
- `include/Tools/DataStructures/Hamt/merkle_proofs.hpp` contains exact `MSP2` proof creation and
  verification plus typed three-way merge.
- `tests/` contains the [deterministic native suites and copied-header consumer](tests/README.md).
- `build.ps1` imports the MSVC toolchain through Scriptorium, stages a package-style include tree,
  and compiles the CHAMP/Patricia suite, Merkle suite, and installed-header consumer.
- `docs/api-specification.md` documents the C++ API adaptation and complexity guarantees.
- `docs/merkle-search-tree.md` specifies the policy, codecs, canonical topology, `MST2` wire bytes,
  and core ownership/lifetime contract.
- `docs/merkle-persistence.md` specifies block stores, packs, seven verification limits, `MSP2`,
  iterative sync, merge, failure ordering, and publication guarantees.
- `docs/usage.md` provides practical include, value-semantics, map/set, Merkle policy, iteration,
  diagnostics, and set-algebra examples.
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
