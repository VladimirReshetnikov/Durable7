# Rust HAMT API Notes

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers implementing and reviewing the Rust HAMT port
- Scope: Rust naming, contracts, and intentional differences from the C# and C++ workspaces

The public crate is `tools-data-structures-hamt`, with library name
`tools_data_structures_hamt`.

Primary entry points:

- `PersistentHashMap<K, V, S = RandomState>`;
- `PersistentHashSet<T, S = RandomState>`;
- `BulkBuilder<K, V, S = RandomState>`;
- `DuplicateKey`.

The port follows the repository HAMT semantics:

- updates return new persistent values and keep old versions usable;
- nodes are immutable and shared through `Arc`;
- the trie uses 32-way bitmap-indexed branching over 32 truncated hash bits;
- equal full-hash collisions are kept in immutable collision buckets;
- no-op value replacement and absent removal preserve the existing root;
- duplicate `add`/`try_add` calls reject the key without changing the root;
- replacing an existing key retains the originally stored key object;
- bulk map construction is last-wins.
- set algebra includes union, intersection, difference, symmetric difference, subset/superset, proper
  subset/superset, overlap, and equality checks.

Set difference removes each probe element from the receiver, and symmetric difference toggles the
distinct probe elements on the receiver, so subtrees untouched by the probe stay structurally shared
and an empty probe preserves the existing root — matching the C# `Except`/`SymmetricExcept`
complexity contract of O(m) single-element updates.

`BulkBuilder` mirrors the C# reference's transient bulk builder (commit `c092016`): unpublished
leaf, collision, and branch nodes are mutated in place and frozen into detached persistent nodes, so
one-pass construction costs O(n (w + c)) node mutations — bounded trie depth plus the applicable
equal-hash collision scan — with no persistent path copies between successive entries. `set_item`
follows the map's duplicate rule (first stored key instance, last supplied value, earlier stored
value retained on an equal replacement), `to_immutable` freezes a detached snapshot and leaves the
builder usable, and `into_immutable` consumes the builder by moving nodes without cloning. Map and
set `FromIterator`, set intersection, and the receiver-policy probe sets built by the binary set
relations route through the builder; incremental updates on existing collections keep their
structural-sharing paths. The Tungsten association's relabel/sort/reverse and small-side
`get_range` index rebuilds consume the builder as well.

Rust-specific differences:

- key equivalence is Rust `Eq`; the hash policy is supplied through `BuildHasher`;
- duplicate insertion returns `Result<_, DuplicateKey>` rather than throwing; `DuplicateKey`
  implements `Display` and `std::error::Error`;
- lookups return references, and removal returns owned cloned values; `try_remove_entry` also
  surfaces the stored key;
- `shares_root_with` exposes root sharing for tests and diagnostics;
- iteration streams trie order through an explicit traversal stack rather than materializing all
  entries up front; map iteration yields `Iter` and set iteration yields `SetIter`, both `Clone` +
  `ExactSizeIterator` + `FusedIterator`, and `&map` / `&set` implement `IntoIterator`;
- trait bounds are per-operation: construction, length, sharing probes, and iteration require no
  bounds; lookups require `K: Eq + Hash, S: BuildHasher`; removal additionally requires
  `K: Clone, V: Clone, S: Clone`; only the insert family requires `V: PartialEq` (for the
  no-op value check). The C# reference imposes no compile-time constraints, so relaxed bounds are
  the closest Rust analogue;
- both collections implement content-based `PartialEq`/`Eq` (the C# reference uses reference
  equality and makes no value-equality claim), `Debug`, and `Default` for any `S: Default`;
  `FromIterator` is available for any default-constructible hasher policy; the map implements
  `Index<&K>` which panics on a missing key.

The hash contract is the standard Rust hash-map contract: keys that compare equal must hash equally under the
chosen `BuildHasher`. The implementation truncates `Hasher::finish()` to 32 bits to match the repository HAMT
shape.
