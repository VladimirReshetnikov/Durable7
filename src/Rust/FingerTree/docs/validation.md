# Rust FingerTree Validation

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers validating the Rust FingerTree-family workspace
- Scope: Cargo commands, warning policy, and test coverage

Run from `src/Rust`:

```powershell
.\test.ps1 -Workspace FingerTree
```

The wrapper locates Cargo on `PATH` or under the default rustup profile and applies inherited,
non-interactive Windows error handling before Cargo starts the test executable.

The crate uses `#![forbid(unsafe_code)]`. Unit tests are inline in the module files under
`FingerTree/src/`; representation-scale integration tests live under `FingerTree/tests/`. Together
they cover:

- DABA Lite exhaustive short histories under a noncommutative matrix monoid, a 100,000-operation
  variable-window sum model, all four six-cursor fixup phases, exact three/two/one combine ceilings,
  63/64/65 and 127/128/129 chunk boundaries, steady-window churn, callback-free structural
  statistics, provisional successor rollback, identity/combine panic injection at every reachable
  callback ordinal, state reuse after failure, prompt retired-slot/chunk release, and clear/reuse;
- persistent deque updates, subtree sharing, bounded-depth tree shape, splitting, concatenation, cached endpoint
  signposts, logarithmic sorted bounds, sorted lower/upper/equal-range splits, insertion/removal, custom ordering,
  and randomized model replay;
- 32-way RRB radix boundaries, regular-versus-relaxed branch invariants, unequal-height boundary-spine
  concatenation, exact-boundary leaf sharing, 10,000 randomized persistent edits, adversarial density/height
  bounds, cached builder snapshots, non-`Clone` structural operations, and concurrent readers;
- canonical zip-tree SHA-256/HMAC and stable-rank-hash golden vectors, a fixed C#-compatible `ZZT2`
  rank/topology vector (including unsigned secondary ordering), insertion-order and delete/reinsert
  convergence, caller-key ownership, random/seeded/keyed trust-mode separation, independently
  reconstructed keyed-policy parity, comparator/hash incoherence rejection, receiver-comparer
  asymmetry and borrowed deduplication, non-`Clone` bulk/read/equality/clear coverage with direct
  first-representative identity, full rank collisions, algebra and owned diff, no-op/root-branch
  identity, 20,000-operation model replay with retained snapshots, validation statistics,
  stack-safe deep destruction, and barrier-started cold digest publication across `Send + Sync`
  readers;
- direct Brodal-Okasaki fused-tree validation; ascending, descending, equal, and melded 4,096-item
  drains; a 20,000-operation branching multiset history with up to 256 retained versions;
  representative retention; custom-policy identity, caller-shared comparer identity, and canonical-natural interoperability;
  root/off-path sharing; `Option` and non-`Clone` elements; comparison ceilings through 65,536
  elements; and concurrent immutable readers;
- winner-cached PSQ replacement semantics, first-key representatives, custom comparator policies,
  `Option` and non-`Clone` components, every AVL rotation/deletion path, 50,000 ascending inserts,
  a 20,000-operation retained `BTreeMap` model, exact range-pruning comparison equations, inclusive
  range/threshold results, no-op/root/far-subtree sharing, priority-then-key tie deletion, and
  concurrent immutable readers;
- general measured tree cached-measure validation, subtree-sharing splits, randomized prefix-measure locate checks,
  key lower/upper-bound splits, product-measure component splits, cumulative-weight selection, and min/max
  extraction helpers;
- reversible-deque O(1) storage-sharing reversal, reversible-typed split/pop results, borrowed/owned logical
  iteration, wrapper-preserving logical edits, and mixed-orientation concat/split/pop paths over the shared tree;
- measured sequence size/sum/min/max/key/product/order-statistic policies;
- sorted bag/set/map rank, navigation, streaming O(n + m) set algebra, proper set relations, duplicate handling,
  inclusive value/key ranges, cached order-statistic measures, and shared-storage edits/ranges;
- stable priority dequeue, meld behavior, cached minimum-priority measures, and shared-storage
  enqueue/meld/dequeue paths;
- closed interval overlap, containment, coalescing, last-low/maximum-high measured descent (including a
  100,000-interval sparse-hit case), and shared-storage insert/remove paths;
- chunked positional rope construction from chunks, caller-supplied copy targets, edits, cached length measures,
  and chunk/subtree sharing; positional cursor gap boundaries and non-`Clone` navigation/snapshot,
  chunk-edge edits, exact seek/empty-insert no-ops, retained branch isolation, root-sharing snapshots,
  deterministic gap-vector model replay, and logarithmic shared-DAG length-overflow panic atomicity;
- measured-rope cached count-plus-user measures, measure navigation, persistent point/range insertion and removal,
  slicing, deterministic vector-model replay, subtree sharing, and append-builder snapshot isolation;
- cached-newline text line helpers, Rust string/display conversions, Unicode scalar and UAX #29 extended-grapheme
  addressing, LF/CRLF/CR/mixed detection, CRLF-aware line text, and builder output.

The current validation proves structurally shared Rust storage across the public FingerTree-family facades and the
observable semantic checkpoint behavior, not final C#/C++ lazy-spine asymptotic parity for the whole crate.
