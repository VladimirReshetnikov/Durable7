# OCaml API Notes

- Created (UTC): 2026-07-17T22:45:00Z
- Repository HEAD: 87dc70271d808e50b52c868cc4956a8da69b2504
- Audience: OCaml consumers and cross-language maintainers
- Scope: Public module inventory, language-local mappings, and implementation distinctions

The `durable7` package uses Dune's qualified subdirectories. Consumers open
`Durable7`, then the relevant `Hamt`, `Finger_tree`, `Ordered`, or
namespace. Public immutable values return successor values; fallible indexed operations
use `result`, absence uses `option`, and runtime hash/comparison/measurement policies are retained by
the values that need them.

## Public Families

| Namespace | Modules |
| --- | --- |
| `Hamt` | `Persistent_hamt`, `Persistent_hash_set`, `Persistent_hash_bag`, `Persistent_bi_map`, `Persistent_hash_multimap`, `Persistent_relation`, `Persistent_map_patch`, `Persistent_directed_graph`, `Persistent_indexed_map`, `Persistent_patricia`, `Concurrent_hash_trie`, `Merkle_encoding`, `Merkle_search_tree`, `Merkle_persistence`, `Merkle_proof_merge`, `Persistent_ancestral_connection_forest` |
| `Finger_tree` | `Measures`, `Measured_tree`, `Persistent_deque`, `Measured_sequence`, `Reversible_deque`, `Sorted_bag`, `Sorted_set`, `Sorted_map`, `Priority_queue`, `Interval_tree`, `Persistent_interval_map`, `Rrb_vector`, `Persistent_chunked_bit_set`, `Range_update_sequence`, `Rope`, `Rope_cursor`, `Measured_rope`, `Text_rope`, `Text_rope_cursor`, `Ordered_search_cursor`, `Canonical_sorted_set`, `Brodal_okasaki_heap`, `Priority_search_queue`, `Daba_lite`, `Incremental_ancestor`, `Ancestral_slice_queue`, `Bilateral_ancestral_deque`, `Contextual_rank_sequence`, `Persistent_delta_map`, `Persistent_run_delta_vector`, `Persistent_monotone_action_heap` |
| `Ordered` | `Persistent_ordered_set`, `Persistent_ordered_map`, `Persistent_ordered_multimap`, `Persistent_ordered_cursor` |

## Persistent Cursors

Every OCaml cursor is a **Profile R snapshot-plus-gap/rank checkpoint**: an immutable value pairing a
retained collection snapshot with a validated integer position or search state. Navigation that only
rewrites the integer is O(1); every peek, seek, and edit calls the ordinary persistent operation of
the owning module and therefore inherits exactly that operation's cost, policy handling, and failure
behavior. No OCaml cursor retains digit, node, chunk, or path context, and none of them inherit the
C# rope tier's focused representation, fragment memo, bounded callback ceiling, allocation bound, or
amortized-locality claims. Cursor state is local navigation state only; it never enters an `MST2`
block, an `MSP2` proof, a pack, or a store.

Cursors are ordinary immutable values, so there is no uninitialized, moved-from, or disposed state to
guard: the type system makes an invalid cursor unrepresentable, and every value returned by a factory
is fully usable. A cursor keeps its snapshot alive, and a snapshot accessor never consumes the
cursor.

### Shared Result Shapes And Error Channels

`Ordered_search_cursor` and `Persistent_ordered_cursor` both publish two record carriers plus
accessors:

```ocaml
type 'cursor search    = { found : bool;  search_cursor    : 'cursor }
type 'cursor insertion = { added : bool;  insertion_cursor : 'cursor }
```

A `search` miss still returns a usable gap cursor, so `found` is the only hit discriminator. The
failure channels are not uniform across the port and callers must read them per family:

| Channel | Used by |
| --- | --- |
| `option` | boundary movement, rank seek, adjacent deletion, `set_next_value`, all Patricia edits |
| `(_, string) result` | `sorted_map_insert`, `ordered_map_insert`, `chunked_bit_set_add`, every Merkle edit, interval-map overlap search and edits, range-update measure/update operations, all rope cursor construction and deletion |
| `insertion` record | `try_insert` on sorted map, priority search, interval map, and all three neutral Ordered families |

### Stored-Key And No-Op Rules

OCaml **retains the stored key representative** on every cursor replacement. `Sorted_map.set`,
`Priority_search_queue.set`, and `Persistent_ordered_map.set` all rebuild the entry from the
stored key — `{ key = stored.key; value }` — so a supplied key that is equivalent to but not
physically the stored key does not replace it. The repository design delegates this to each port's stored-key
contract; this is what the OCaml source does. (Cross-port comparison is out of scope for this
document, which is verified only against OCaml source.)

Value no-op rules are not uniform:

- `Persistent_ordered_map` carries a `value_equal` field (default structural `( = )`) and its `set`
  returns the receiver unchanged when the payload compares equal, so `ordered_map_set_next_value`
  inherits a genuine value-equality no-op.
- Both Patricia families short-circuit only on **physical identity**: `set_tree` keeps the existing
  leaf when `stored_value != value` is false, i.e. when the new value is the same boxed object, and
  otherwise rebuilds the path. A structurally-equal but physically-distinct value is not a no-op.
- `Sorted_map` and `Priority_search_queue` carry no value-equality policy, so a present-key `set`
  always publishes a new version by rebuilding one root path (the queue additionally recomputes
  the winner caches along that path) even when the payload is equal.

`result` payloads carry human-readable prose rather than a structured code or an error variant, so
how precisely a rejection can be identified varies by family:

- **Patricia `insert` cannot distinguish its two rejections at all.** It returns `None` both when the
  key is already present and when the key's lower-bound rank is not the current gap.
- Merkle `insert` separates them **only in prose**: `"the Merkle key is already present"` versus
  `"the key belongs at gap N, not at the current gap M"`. Telling them apart requires matching the
  message text, which is not a stable contract.
- `interval_map_try_insert` itself string-matches the owning module's
  `"interval map already contains the exact interval"` message to choose between reporting
  `added = false` and propagating `Error`. That duplicate/validation split is coupled to the exact
  message string.

No cursor family in this port exposes a structured error code, and none should be assumed.

### Integer Patricia Maps And Sets

`Persistent_patricia.Int32_map`, `Int64_map`, `Int32_set`, and `Int64_set` each expose a nested
`Cursor` module over **ascending signed key order**: `to_bits` applies the sign-bit XOR transform
(`0x8000_0000` for `int32`, `Int64.min_int` for `int64`), so unsigned order over transformed bits is
signed order over keys across the minimum, zero, and maximum boundaries. Factories are `start`,
`at_end`, `at`,
`lower_bound`, `upper_bound`, and `exact`; `exact` returns a `cursor * bool` pair so a miss still
yields the insertion gap. Navigation is `count`, `position`, `is_at_start`, `is_at_end`,
`peek_previous`, `peek_next`, `move_previous`, `move_next`, and `seek`. Map edits are `insert`,
`set`, `set_next_value`, `delete_previous`, and `delete_next`; the set exposes `add` in place of the
three map writers. `snapshot` returns the retained map or set.

Gap conventions: `insert` and a key-adding `set` leave the gap **after** the new entry; a replacing
`set` and `set_next_value` leave it unchanged; `delete_previous` moves the gap left; `delete_next`
keeps it fixed. `insert` returns `None` when the key is already present **or** when its lower-bound
rank is not the current gap; `set` returns `None` only for the wrong-gap case.

Branch nodes cache subtree counts, so `entry_at` and `lower_bound_rank` are O(W) descents for key
width W rather than list walks. Movement is an O(1) integer rewrite, but because the cursor retains
no path frames, the subsequent peek is an unconditional O(W) descent from the root. **A complete
in-order traversal is therefore Θ(n · W); this tier makes no O(1)-amortized movement claim.** Edits
delegate to the ordinary `add`/`set`/`remove`, so structural sharing and key semantics are identical
to direct map edits. There is no value-equality policy: a present-key `set` is a no-op only when the
new value is the **same boxed object** as the stored one (`set_tree`'s `!=` check) and otherwise
rebuilds the path.

### Merkle Search Tree

`Merkle_search_tree`'s `Cursor` module owns a canonical tree snapshot plus a comparator-order rank
gap. Factories are `start`, `at_end`, `at`, `lower_bound`, `upper_bound`, and `exact` (returning
`cursor * bool`). Navigation mirrors Patricia. Edits are `insert`, `set`, `set_next_value`,
`delete_previous`, and `delete_next`, all returning `(_, string) result`; `set_next_value`,
`delete_previous`, and `delete_next` return `(cursor option, string) result` so a boundary miss is
distinguished from a failure. Gap conventions match Patricia.

Every edit calls the canonical tree operation, so policy, stored representative, canonical encoding,
root digest, callback behavior, failure atomicity, and block-persistence rules are byte-identical to
a direct tree edit. Cursor state is never serialized.

Blocks cache each child's total subtree count but not cumulative child-prefix ranks, so with block
height `h` and visited-block occupancies `e_i`: `lower_bound`/`upper_bound`/`exact` cost
`O(sum log(e_i + 1))` comparisons plus `O(sum e_i)` count summation, while `at`, rank seek, and
**every peek** cost `O(sum (e_i + 1))` because `entry_at_rank` scans a block's children linearly.
`count` and `position` are O(1) reads and `move_previous`/`move_next` are O(1) integer rewrites, so a
complete traversal by move-plus-peek is `O(n · sum (e_i + 1))`, **not O(n)**.

### Sequence Families

| Module | Cursor factories | Positional edits |
| --- | --- | --- |
| `Measured_tree` | `cursor_at_start`, `cursor_at_end`, `cursor_by_measure` | `cursor_insert`, `cursor_replace_next`, `cursor_delete_previous`, `cursor_delete_next` |
| `Persistent_deque` | `cursor`, `cursor_at` | plus `cursor_insert_many` |
| `Rrb_vector` | `cursor`, `cursor_at` | plus `cursor_insert_many`, `cursor_insert_vector` |
| `Reversible_deque` | `cursor`, `cursor_at` | plus `cursor_insert_many`, `cursor_reverse` |
| `Range_update_sequence` | `cursor`, `cursor_at` | plus `cursor_update_previous`, `cursor_update_next` |

All five share `cursor_position`/`cursor_is_at_start`/`cursor_is_at_end`, `cursor_peek_previous`,
`cursor_peek_next`, `cursor_move_previous`, `cursor_move_next`, `cursor_seek`, and `cursor_snapshot`;
the four positional families also expose `cursor_length`. Insert leaves the gap after the inserted
element or range, `cursor_delete_previous` moves the gap left, and `cursor_delete_next` and
`cursor_replace_next` leave it fixed. Movement and seek return `option` and fail only at a boundary
or an out-of-range index.

`Measured_tree` is the only module carrying the full measured surface — `cursor_measure_before`,
`cursor_measure_after`, `cursor_by_measure`, and `cursor_seek_by_measure`, the last two returning
`bool * cursor` so an unsatisfied predicate still yields the end gap. A predicate already true at the
monoid identity selects position 0 of a nonempty sequence. **`Measured_sequence`, the public measured
facade, exposes no cursor at all**; consumers needing a measured gap must use `Measured_tree`
directly.

`Measured_tree`, `Persistent_deque`, and `Rrb_vector` share one measured substrate — since the
Wave-1 core upgrade, a lazy Hinze–Paterson finger tree with memoized defunctionalized suspensions
(`Rrb_vector` is a type alias of `Persistent_deque`, which wraps `Measured_tree` under
`Measures.size`), giving O(1) worst-case ends and amortized O(1) endpoint updates. Their cursor costs are therefore: factories, `cursor_position`, `cursor_length`,
`cursor_seek`, `cursor_move_*`, and `cursor_snapshot` O(1); `cursor_peek_*`,
`cursor_insert`, `cursor_replace_next`, and `cursor_delete_*` O(log n) through split/join;
`cursor_by_measure`/`cursor_seek_by_measure` and `cursor_measure_before`/`cursor_measure_after`
O(log n), reading cached node measures without remeasuring elements; and range insertion
O(m + log n). `cursor_seek_by_measure` searches the **whole snapshot** from the identity — it is an
absolute prefix search, not a search relative to the current gap. `Rrb_vector` reuses this balanced
sequence rather than packed/relaxed radix nodes, so no `log32` bound and no radix frame applies.
`cursor_insert_many` on the deque and reversible deque and `cursor_insert_vector` on the vector
return the receiver unchanged for an empty range.

`Range_update_sequence` adds `cursor_measure_previous`, `cursor_measure_next`,
`cursor_update_previous`, and `cursor_update_next`, all returning `(_, string) result` for a bad
length. Its cursor preserves
the owning collection's tag composition order and non-double-application invariant, but its storage
is a plain `'element array` plus one cached whole-sequence measure, so it claims no lazy-tag bound.
The concrete consequences are:

- `cursor_position`, `cursor_length`, `cursor_seek`, `cursor_peek_*`, `cursor_move_*`, and
  `cursor_snapshot` are O(1), but **`cursor_insert`, `cursor_delete_*`, and `cursor_replace_next` are
  each Θ(n)**: they copy the whole array and then refold the whole measure, invoking
  `measure_element` and `combine` once per element.
- `cursor_measure_previous k`/`cursor_measure_next k` fold the requested subrange, so they are
  Θ(k). **Zero length correctly returns the monoid identity with no element or tag callbacks.**
- **`cursor_update_previous 0` and `cursor_update_next 0` do not behave as the repository design
  requires.** The design specifies that a zero-length apply returns the same cursor without
  callbacks. In OCaml a zero-length update still copies the whole array, still invokes
  `apply_measure` once (as a discarded law exercise), still refolds the whole measure with Θ(n)
  `measure_element`/`combine` calls, and returns a **fresh version** rather than the receiver.
- Tags are applied **eagerly, element by element**. There are no pending tags, no tag pushes, and the
  algebra's `compose` is never invoked by any cursor update; it is reachable only through
  `compose_tags`.

**`Reversible_deque` cursor edits are Θ(n) with no structural sharing.** All four of
`cursor_insert_many`, `cursor_delete_previous`, `cursor_delete_next`, and `cursor_replace_next`
materialize the deque with `to_list`, rebuild with `of_list`, and discard the previous spine.
`cursor_reverse` flips the logical orientation and maps position `p` to `length - p`.

### Ropes And Text

`Rope_cursor.t` is a positional snapshot-plus-index checkpoint: `create ?position`, `rope`,
`position`, `move_to`, `peek_before`, `peek_after`, `insert`, `insert_many`, `delete_before`, and
`delete_after`. Construction, `move_to`, and both deletions return `(_, string) result`; insertion is
total.

`Text_rope_cursor.t` adds `line_column`, `move_to_line_column`, `insert_utf8`, and `find_forward`.
**The text unit is the Unicode scalar value (`Uchar.t`)**, matching `Text_rope`, not UTF-16 code
units or raw bytes.

Both rope cursors sit on the same measured-tree substrate, so `create`, `position`, `move_to`, and
the snapshot accessor are O(1) and `peek_before`/`peek_after`, insertion, and deletion are O(log n)
plus O(m) for inserted length. Three costs are larger than the substrate suggests and are stated
here rather than inferred:

- `Rope_cursor.insert_many` has **no empty-range fast path**. Unlike the deque, reversible-deque, and
  vector cursors, it always splits and reconcatenates, so inserting `[]` publishes a rebuilt rope
  and a new cursor rather than preserving the receiver.
- `Text_rope_cursor.line_column` and `move_to_line_column` materialize the whole rope to a list and
  walk it, so each is **Θ(n)**; there is no cached newline measure behind them.
- `Text_rope_cursor.find_forward` materializes the haystack and needle into arrays and runs a naive
  scan, so it is **Θ(n + (n − start) · m)** and allocates the full text on every call.
  `insert_utf8` also decodes its argument twice, once for the length and once inside
  `Text_rope.insert_utf8`.

`Measured_rope`'s cursor is deliberately narrower than its siblings, and the gap is a facade gap
rather than a substrate gap. It ships `create_cursor ?position`, `cursor_rope`, `cursor_position`,
`cursor_move_to`, `cursor_measure_before`, `cursor_insert`, and `cursor_delete_after` — and **no
`cursor_measure_after`, no measure-based cursor seek, no `cursor_replace_next`, no unit
`move_next`/`move_previous`, and no cursor `count`**. Two consequences are worth stating plainly:

- The law `combine(measure_before, measure_after) = measure(whole)` is **not expressible through this
  cursor**, because only the before-measure is reachable.
- `docs/reference/semantic-contracts.md` (the sequence beginning "Ordered before/after measures,
  absolute prefix search, ... unconditional replacement") attributes three cursor capabilities to
  this checkpoint. Of those, **only the before half of the ordered measures exists** on the cursor:
  there is no `cursor_measure_after`, no measure-predicate (absolute-prefix) cursor seek, and no
  replacement operation at all. The positional `cursor_move_to` exists but is a separate index-based
  seek, not the attributed prefix search.

The gap is a facade choice: `Measured_tree` in this same workspace carries the complete measured
cursor surface (`cursor_measure_after`, `cursor_seek_by_measure`, `cursor_replace_next`, and unit
`cursor_move_previous`/`cursor_move_next`), and the module-level `Measured_rope.locate` performs the
absolute prefix search off the cursor, so the missing operations exist one layer down.

### Ordered Search Families

`Ordered_search_cursor` is one module holding seven prefixed cursor families over comparator or
deterministic interval order. Each family follows the same shape — `<family>_at`,
`<family>_lower_bound`, `<family>_upper_bound`, `<family>_find`, then `_position`, `_peek_previous`,
`_peek_next`, `_move_previous`, `_move_next`, `_seek_rank`, the family edits, and `_snapshot`:

| Family | Cursor type | Edits |
| --- | --- | --- |
| Sorted bag | `'element sorted_bag_cursor` | `sorted_bag_add`, `_delete_previous`, `_delete_next` |
| Sorted set | `'element sorted_set_cursor` | `sorted_set_add` (duplicate-rejecting), `_delete_previous`, `_delete_next` |
| Sorted map | `('key, 'value) sorted_map_cursor` | `sorted_map_insert`, `_try_insert`, `_set`, `_set_next_value`, `_delete_previous`, `_delete_next` |
| Canonical sorted set | `'element canonical_cursor` | `canonical_add`, `_delete_previous`, `_delete_next` |
| Priority search queue | `('key, 'priority, 'value) priority_search_cursor` | `priority_search_try_insert`, `_set`, `_set_next`, `_delete_previous`, `_delete_next` |
| Interval tree | `'endpoint interval_tree_cursor` | `interval_tree_insert`, `_delete_previous`, `_delete_next` |
| Persistent interval map | `('endpoint, 'value) interval_map_cursor` | `interval_map_try_insert`, `_set_next_value`, `_delete_previous`, `_delete_next` |
| Persistent chunked bit set | `chunked_bit_set_cursor` | `chunked_bit_set_add`, `_delete_previous`, `_delete_next` |

The priority-search family adds `priority_search_minimum`; the interval families add
`_find_overlap`, `_find_containing`, and `_seek_next_overlap`, whose exclusive continuation rule
starts the next scan at `position + 1` and so cannot loop. The bit-set family replaces the bound
factories with `chunked_bit_set_at_or_after`. Comparators, hash policies, and priority orders are
carried by the retained snapshot and are never re-derived by the cursor.

Honest costs for this group, all verified against the implementation bodies:

- `Sorted_bag`, `Sorted_set`, `Sorted_map`, and `Canonical_sorted_set` are backed by the lazy
  measured finger-tree core (Wave 2a). Bound and rank lookups are O(log n) measure- or
  size-directed descents, cursor edits are O(log n) split-and-join writes sharing structure, and
  peeks are O(log n) rank reads — the census-ruled regression from the array's O(1) indexing,
  while extremes stay O(1) digit reads.
- `Priority_search_queue` is the winner-cached key-ordered AVL (Wave 2b): key search and bound
  ranks are O(log n), cursor edits are O(log n) path-copied writes, the minimum is the root's O(1)
  cached winner, and rank peeks are O(log n) size-directed descents. It still exposes no pruned
  range-enumeration query, as already recorded below.
- `Canonical_sorted_set` delegates its storage to `Sorted_set` and has no Cartesian tree; the cursor
  inherits that, so no canonical zip-zip bound applies. See the existing note in
  [Language-Local Semantics](#language-local-semantics).
- **The interval families never use their augmentation.** `Interval_tree` maintains
  `cached_maximum_high` on every edit, but it is read only by the `maximum_high` accessor — no query
  path consults it, and `Persistent_interval_map` stores no augmentation at all. Both
  `_find_overlap`, `_find_containing`, and `_seek_next_overlap` are linear scans from the start
  index, so each is O(n) and driving `seek_next_overlap` to exhaustion is O(n²). **No augmented or
  output-sensitive bound holds for these cursors.** `interval_tree_find` and the ordinary bound
  factories are O(log n) binary searches; edits copy the array (O(n)), and `remove_at` additionally
  recomputes the cached maximum by a full fold.
- **The interval-tree miss cursor does not identify where the matching insert lands.**
  `interval_tree_find` returns the *lower* bound on a miss, while `interval_tree_insert` places the
  interval at the *upper* bound of its low endpoint and leaves the gap just after it. For a run of
  equal low endpoints the two positions differ.
- **`Persistent_chunked_bit_set` has no words, no chunks, and no cached population.** It is
  `Set.Make (Int)` over individual bit indexes. `rank` folds the entire set and `select` iterates the
  entire set **with no early exit even after the target is found**, so both are Θ(n); every cursor
  peek, rank seek, and movement inherits that bound, and `statistics.chunk_count` is synthesized for
  reporting rather than read from storage. The module's `.mli` header, "Sparse persistent
  non-negative bit set with rank/select queries", describes the intended semantics; **it carries no
  sparse-word or O(log w) complexity claim, and none should be inferred from it.**

### Neutral Ordered Set, Map, And Multimap

`Persistent_ordered_cursor` holds `'element ordered_set_cursor`,
`('key, 'value) ordered_map_cursor`, and `('key, 'value) ordered_multimap_cursor` over
insertion/explicit-position order. Each has `<family>_at` and `<family>_find` factories (the multimap
adds `ordered_multimap_find_group`), then `_position`, `_count` (`ordered_multimap_pair_count` on the
multimap), `_at_start`, `_at_end`, `_peek_previous`, `_peek_next`, `_move_previous`, `_move_next`,
`_seek`, the edits, and `_snapshot`. Insert leaves the gap after the new element,
`_delete_previous` moves it left, `_delete_next` keeps it fixed, and a rejected duplicate insert
returns the **receiver unchanged**, which is what `try_insert` reports through `added`. Sparse labels
are never exposed through any cursor operation.

Two deviations must be read before relying on any repository-level bound for this family:

- **The OCaml neutral Ordered collections are the CHAMP-plus-stamped-sequence composite
  (Wave 2b).** `Persistent_ordered_set.t` and `Persistent_ordered_map.t` pair a CHAMP stamp index
  (`Hamt.Persistent_hamt`) with a maximum-stamp-measured order sequence (`Ordered.Stamped_order`
  over `Finger_tree.Measured_tree`), and the multimap is an ordered map of ordered sets over the
  same pair. Membership and `find` are expected-O(1) hashed lookups, `index_of` is one O(log n)
  measure-directed descent, and every insertion, deletion, or movement is an O(log n) structural
  write. The root `README.md` statement that the Ordered indexes "compose public CHAMP and
  FingerTree surfaces" now holds for OCaml. Rank access (`nth`, and therefore the set and map
  cursor peeks) is O(log n) — the census-ruled regression from the array placeholder's O(1)
  indexing.
- On a miss, `ordered_set_find`, `ordered_map_find`, `ordered_multimap_find`, and
  `ordered_multimap_find_group` return a cursor at the **end gap**, not at a lower bound. `found` is
  the only usable discriminator.

The multimap cursor is a flat global pair rank rather than the nested group state the repository
design specifies, which the design explicitly instructs ports not to invent. Its concrete OCaml
consequences are:

- `ordered_multimap_peek_previous`/`_peek_next` call `List.nth_opt` over a freshly materialized pair
  flattening, so **each peek is O(P) in the total pair count** and a complete traversal is O(P²).
- `ordered_multimap_insert` derives the published gap from the inserted key's group end, walking the
  flattened pairs with the key policy alone and never re-scanning for the value. A value that is not
  reflexive under the value hash policy — `Float.nan` is the reachable case — is therefore handled
  like any other and never raises.
- `ordered_multimap_delete_previous`/`_delete_next` peek, remove by content re-lookup, and **thread
  the removed flag**: a delete that changes nothing returns `None` rather than publishing an
  unchanged version as a success, so the same non-reflexive values do not report a false deletion.

The O(P) peek cost is a known limitation of the flat pair encoding; the insert and delete
derivations above are its correctness-preserving consequences. All are properties of the flat pair
encoding, not of the surrounding immutability model.

## Language-Local Semantics

- `Common.Hash_policy` and `Common.Comparator` use object identity when a cross-value operation
  requires the exact same policy. Equivalent keys retain their first stored representative unless a
  contract deliberately adopts the caller representative.
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
- `Range_update_sequence` requires an explicit law-verification admission flag. Its storage is the
  sibling path-copied implicit AVL with composable pending tags (Wave 2b), so it delivers the
  sibling lazy-update bound: a range update is O(log n) independent of the range's length.
- `Rrb_vector` is a genuine eager 32-way relaxed radix-balanced tree (Wave 2a) — 5-bit radix,
  size tables only on relaxed branches, seam-only concat rebalance — though it still ships no
  transient RRB kernel. `Priority_search_queue` is the winner-cached key-ordered AVL (Wave 2b) and
  delivers the sibling winner-cached bounds; it exposes no pruned range-enumeration query on its
  OCaml surface, so no pruned-query bound is claimed.
- `Canonical_sorted_set` derives the shared `ZZT2` HMAC-SHA256 ranks and preserves canonical sorted
  contents across insertion histories. Its initial storage delegates to `Sorted_set`, so it does
  not claim canonical zip-tree topology or zip-zip complexity bounds.
- `Brodal_okasaki_heap` is now the bootstrapped skew-binomial heap every sibling workspace ships —
  the untagged specialization of `Persistent_monotone_action_heap`'s kernel — delivering the
  namesake bounds: O(1) worst-case insert, meld, and minimum, O(log n) worst-case delete-minimum,
  O(1) count. Its `statistics` walks the real forest and reports the same shape fields the sibling
  audits report (walked count, root-forest length, maximum rank, maximum depth), measured from the
  nodes rather than synthesized. `Daba_lite` still preserves the public semantic and failure-atomic
  contracts under simple OCaml storage and does not claim the specialized sibling core's worst-case
  callback bounds.

The shared semantic authority remains the repository
[semantic contracts](../../../docs/reference/semantic-contracts.md); this document records the OCaml
surface and intentional runtime mappings.
