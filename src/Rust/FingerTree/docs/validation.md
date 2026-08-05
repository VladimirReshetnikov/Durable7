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

## Current derived-structure evidence

On 2026-07-17, the focused chunked-bit-set suite passed 7/7 tests, in addition to the prior 6/6
payload interval-map suite. The serialized full Rust workspace subsequently passed in both Debug
and Release, including all FingerTree unit, integration, compile-fail, and documentation tests.
Benchmarks were not run.

## Research-derived collection port evidence

On 2026-08-04, the seven research-derived collections were ported from the C# reference into this
crate, and the C#/Rust enhancement backlog landed in both languages the same day. The full
serialized Rust workspace passes 452/452 tests across 30 test binaries with zero failures;
`cargo build --workspace` reports zero warnings and `cargo clippy --workspace --all-targets` reports
no diagnostics attributable to the added modules. The added coverage spans `equality`,
`incremental_ancestor`, `ancestral_slice_queue`, `bilateral_ancestral_deque`,
`contextual_rank_sequence`, `delta_map`, `monotone_action_heap`, and `run_delta_vector`, plus the
Hamt connection-forest tests recorded in that crate's validation notes. The backlog additions
covered are position-addressed run accept/revert, range-restricted change enumeration, and bulk
assignment, each tested for equivalence with the operation it composes. An adversarial parity review against the C# baseline confirmed twelve
documentation-accuracy, policy, and coverage findings — all fixed — and found no correctness defect
in any port; see the
[port review](../../../../docs/reviews/rust-collection-port-review-2026-08-04.md). No benchmark was
run.

This evidence proves the ported semantics and the structural invariants asserted by the tests. It
does not prove the theoretical instantiations the design proposals describe: in particular, the
ancestry-interval sequences ship the Myers reference arena with O(1)-amortized addition and
O(log M) ancestor queries, and no Alstrup-Holm backend exists in any port.

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
- payload interval-map strict/replacing edits, lexicographic same-low order, invalid-interval
  rejection, first key representatives, equal-value storage sharing, overlap-model parity,
  removal, retained versions, and full-key/maximum-high annotation invariants;
- sparse chunked-bit-set word boundaries, duplicates, negative-index policy, ascending iteration,
  inclusive rank, zero-based select, all four algebra operations, root-sharing no-ops, retained
  versions, and cached word/population annotations;
- chunked positional rope construction from chunks, caller-supplied copy targets, edits, cached length measures,
  and chunk/subtree sharing; positional cursor gap boundaries and non-`Clone` navigation/snapshot,
  chunk-edge edits, exact seek/empty-insert no-ops, retained branch isolation, root-sharing snapshots,
  deterministic gap-vector model replay, and logarithmic shared-DAG length-overflow panic atomicity;
- measured-rope cached count-plus-user measures, measure navigation, persistent point/range insertion and removal,
  slicing, deterministic vector-model replay, subtree sharing, and append-builder snapshot isolation; immutable
  measured cursor empty/boundary and non-`Clone` navigation, ordered noncommutative before/after measures,
  absolute hit/miss/chunk-boundary measure search, callback-panic retry, retained edit branches, nullable values,
  a 750-command gap-vector model, and exact-`usize::MAX` shared-DAG overflow before measure callbacks;
- cached-newline text line helpers, Rust string/display conversions, Unicode scalar and UAX #29 extended-grapheme
  addressing, LF/CRLF/CR/mixed detection, CRLF-aware line text, builder output, and the nominal text cursor's
  exact-facade snapshots, newline-prefix search, scalar line/column positions, and Unicode edits.

- retained equality policies: natural-versus-custom compatibility, clone-preserved identity, and
  coarse/fine closure policies that change which writes are no-ops and which changes cancel;
- the shared incremental level-ancestor arena: branched ancestor queries checked against a naive
  parent-walk oracle, exact odd-block capacity at square boundaries, and a conservative logarithmic
  hop-count envelope;
- ancestry-interval queue and deque semantics: anchored-empty provenance, retained-branch
  independence, every slice/split boundary against a sequence oracle, O(1) reverse by segment
  exchange, and the arena-statistics ceilings proving that boundary-specialized operations issue no
  ancestor query and that no scalar operation issues more than the documented number;
- contextual rank sequences: exhaustive short inputs against an independent scanner, contextual
  event rank/select including multi-event transitions, and cached-summary reuse showing reads do not
  rescan the input;
- checkpoint-differential map and vector: coalescing and cancellation of repeated writes, exact
  representative retention across delete/re-add round trips, checkpoint-root canonicalization when
  the change index empties, maximal non-adjacent dirty runs, run-length-independent accept/revert
  splicing, and output-optimal descriptor counts for clustered dirty positions;
- monotone-action heaps: identity/associativity/monotonicity of clamp composition, exact
  representative preservation under a coarse comparer, temporal insert-after-transform semantics,
  melding of independently transformed heaps without cross-applied actions, and constant policy-call
  and allocation counts for whole-heap transformation.

The current validation proves structurally shared Rust storage across the public FingerTree-family facades and the
observable semantic checkpoint behavior, not final C#/C++ lazy-spine asymptotic parity for the whole crate.
