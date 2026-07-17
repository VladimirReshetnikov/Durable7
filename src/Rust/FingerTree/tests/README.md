# Rust FingerTree Tests

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers navigating Rust FingerTree-family test coverage
- Scope: Test location, command, and coverage map

Unit tests live inline in the module files under [`../src`](../src); representation-scale
integration suites live beside this index. Run them from `src/Rust`:

```powershell
.\test.ps1 -Workspace FingerTree
```

Coverage groups:

- `brodal_okasaki_heap.rs`: direct fused-tree invariants; 4,096-element ascending, descending,
  equal, and melded drains; a 20,000-operation branching retained multiset model; shared-comparer and custom-policy
  identity and canonical-natural interoperability; concrete representatives; root/off-path
  sharing; `Option` and non-`Clone` values; comparison ceilings through 65,536 elements; and
  concurrent readers;
- `priority_search_queue.rs`: exact comparer/equality replacement semantics, first concrete key
  representatives, custom policies, `Option` and non-`Clone` components, AVL rotation/deletion
  rebalancing, 50,000 ascending inserts, a 20,000-operation retained `BTreeMap` model, audited
  range-pruning comparison equations, inclusive threshold order, root/far-subtree sharing,
  deterministic priority-then-key ties, and concurrent readers;
- `daba_lite.rs`: the six-cursor DABA Lite state machine, noncommutative/exhaustive and 100,000-step
  FIFO models, all fixup phases and callback ceilings, 63/64/65 and 127/128/129 chunk boundaries,
  exact callback-panic atomicity, provisional-link rollback, callback-free invariants, deterministic
  retired-slot/chunk release, and clear/reuse;
- `deque.rs`: structurally shared persistent deque, cached endpoint-signpost validation, logarithmic sorted bounds,
  sorted split/equal-range/insert/remove operations (including custom ordering), model replay, subtree-sharing
  checks, and reversible deque O(1) mirrored views with reversible-typed results, logical iteration, and
  mixed-orientation concat/split/pop coverage;
- `rrb_vector.rs`: 32-slot relaxed radix-balanced vectors, radix/relaxed layout validation,
  unequal-height concat, exact leaf sharing, range and endpoint edits, 10,000-operation vector-model
  replay, adversarial density/height checks, builder snapshot isolation, and concurrent readers;
- `canonical_sorted_set.rs`: RustCrypto SHA-256/HMAC vectors, fixed stable-hash outputs, and a
  C#-compatible `ZZT2` rank/topology vector with unsigned-secondary ordering; fresh, seeded, and
  caller-keyed policy modes; comparer/hash coherence; receiver-defined cross-policy asymmetry and
  borrowed deduplication; non-`Clone` bulk/read/validation/equality/clear plus direct retained-
  representative identity; insertion-order canonicality and deterministic full-rank ties;
  immutable branch sharing and retained snapshots; algebra, owned diff, memoized inequality, model
  replay, validation statistics, deep stack safety, and barrier-started cold digest publication
  across concurrent `Send + Sync` readers;
- `measured.rs`: structurally shared measured sequence core, cached-measure validation, prefix locate, built-in
  measure policies, key lower/upper-bound splits, product-measure component splits, cumulative-weight selection,
  priority extraction helpers, and order-statistic count plus last-key measures;
- `sorted.rs`: sorted bag, set, and map facades with cached order-statistic measures, rank/key-boundary edits,
  streaming large-set algebra, inclusive value/key ranges, proper set relations, shared measured storage, and ranges;
- `priority_queue.rs`: stable minimum-priority queue, meld, cached minimum-priority measures, and shared-storage
  updates;
- `interval_tree.rs`: closed intervals, last-low/maximum-high measured overlap descent, a 100,000-interval
  sparse-hit regression, coalescing, and shared-storage updates;
- `persistent_interval_map.rs`: payload-bearing lexicographic interval keys, strict and replacing
  edits, invalid-interval errors, first representatives, overlap-model parity, point stabbing,
  removal, storage sharing, retained versions, and annotation invariants;
- `persistent_chunked_bit_set.rs`: sparse-word boundaries, duplicate/negative-index behavior,
  ascending iteration, inclusive rank, zero-based select, persistent algebra, storage-sharing
  no-ops, retained versions, and cached word/population invariants;
- `rope.rs`: chunked positional and measured rope storage over cached count/user-measure summaries, chunk-copy
  construction, caller-supplied copy targets, positional cursor boundaries/non-`Clone` navigation/chunk-edge
  edits/no-op seeks and inserts/retained branches/root-sharing snapshots/gap-vector model replay,
  checked-length shared-DAG overflow atomicity, structurally shared measured-rope point/range edits and
  slices, deterministic vector-model replay, append-builder measure tracking and snapshot isolation,
  measured-cursor non-`Clone` navigation, ordered measures, absolute hit/miss search, retained edit branches,
  callback retry, a 750-command gap model, exact-maximum count overflow before policy callbacks, and
  text-cursor Unicode/LF line semantics with exact text-facade snapshots;
- `text_extras.rs`: Unicode-scalar addressing, UAX #29 extended grapheme segmentation and offset conversion,
  LF/CRLF/CR/mixed newline detection, and CRLF-aware line text over both character and text ropes.
- `lib.rs`: public `Send`/`Sync` assertions, including positional, measured, and text rope cursors, and spawned-thread
  readers over the canonical set and shared immutable deque, reversible deque, rope, and
  measured-rope snapshots.
