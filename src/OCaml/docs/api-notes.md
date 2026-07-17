# OCaml API Notes

- Created (UTC): 2026-07-17T22:45:00Z
- Repository HEAD: 87dc70271d808e50b52c868cc4956a8da69b2504
- Audience: OCaml consumers and cross-language maintainers
- Scope: Public module inventory, language-local mappings, and implementation distinctions

The `tools-data-structures` package uses Dune's qualified subdirectories. Consumers open
`Tools_data_structures`, then the relevant `Numerics`, `Hamt`, `Finger_tree`, `Ordered`, or
`Tungsten` namespace. Public immutable values return successor values; fallible indexed operations
use `result`, absence uses `option`, and runtime hash/comparison/measurement policies are retained by
the values that need them.

## Public Families

| Namespace | Modules |
| --- | --- |
| `Numerics` | `Wide_integer` (`UInt256`/`Int256`, `UInt512`/`Int512`, `UInt1024`/`Int1024`, `Bit_converter_ex`), `Sparse_integer` |
| `Hamt` | `Persistent_hamt`, `Persistent_hash_set`, `Persistent_hash_bag`, `Persistent_bi_map`, `Persistent_hash_multimap`, `Persistent_relation`, `Persistent_map_patch`, `Persistent_directed_graph`, `Persistent_indexed_map`, `Persistent_patricia`, `Concurrent_hash_trie`, `Merkle_encoding`, `Merkle_search_tree`, `Merkle_persistence`, `Merkle_proof_merge` |
| `Finger_tree` | `Measures`, `Measured_tree`, `Persistent_deque`, `Measured_sequence`, `Reversible_deque`, `Sorted_bag`, `Sorted_set`, `Sorted_map`, `Priority_queue`, `Interval_tree`, `Persistent_interval_map`, `Rrb_vector`, `Persistent_chunked_bit_set`, `Range_update_sequence`, `Rope`, `Rope_cursor`, `Measured_rope`, `Text_rope`, `Text_rope_cursor`, `Canonical_sorted_set`, `Brodal_okasaki_heap`, `Priority_search_queue`, `Daba_lite` |
| `Ordered` | `Persistent_ordered_set`, `Persistent_ordered_map`, `Persistent_ordered_multimap` |
| `Tungsten` | `Persistent_list`, `Persistent_association` |

## Language-Local Semantics

- `Common.Hash_policy` and `Common.Comparator` use object identity when a cross-value operation
  requires the exact same policy. Equivalent keys retain their first stored representative unless a
  contract, such as Tungsten Association movement, deliberately adopts the caller representative.
- The HAMT transient is one-way and rejects access after publication. Changed edits remain
  persistent path copies; OCaml makes no owner-token in-place performance claim. Its reusable
  `Bulk_builder` likewise retains path copies while providing detached freezes; it makes no
  unpublished-mutable-node construction claim.
- `Concurrent_hash_trie` serializes operations with a mutex and captures immutable HAMT snapshots in
  O(1). It is a thread-safe consumer facade, not a lock-free Ctrie claim.
- The authenticated map uses exact SHA-256 policy domains and byte-compatible `MST2` blocks.
  Persistence enforces all seven verification budgets. The proof producer currently supplies the
  complete authenticated block set rather than a minimized path proof.
- `Text_rope` validates UTF-8 and indexes Unicode scalar values (`Uchar.t`), matching OCaml's natural
  text element rather than UTF-16 code units or raw bytes.
- `Range_update_sequence` requires an explicit law-verification admission flag. Its initial OCaml
  storage rebuilds affected immutable arrays and cached measures, so it does not claim the sibling
  implicit-AVL lazy-update bound.
- `Rrb_vector` reuses the persistent balanced sequence facade and does not claim relaxed-radix
  topology or a transient RRB kernel. `Priority_search_queue` uses a key-sorted immutable array and
  cached winner, so it makes no winner-cached balanced-tree or pruned-query complexity claim.
- `Canonical_sorted_set` derives the shared `ZZT2` HMAC-SHA256 ranks and preserves canonical sorted
  contents across insertion histories. Its initial storage delegates to `Sorted_set`, so it does
  not claim canonical zip-tree topology or zip-zip complexity bounds.
- `Brodal_okasaki_heap` and `Daba_lite` preserve the public semantic and failure-atomic contracts,
  but the initial simple OCaml storage does not claim the specialized sibling cores' worst-case
  asymptotic or callback bounds.
- `Tungsten` is an application leaf. No module in `Numerics`, `Hamt`, `Finger_tree`, or `Ordered`
  imports it or treats it as a semantic baseline.

The shared semantic authority remains the repository
[semantic contracts](../../../docs/reference/semantic-contracts.md); this document records the OCaml
surface and intentional runtime mappings.
