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

The crate uses `#![forbid(unsafe_code)]`. Unit tests are inline in the module files under `FingerTree/src/` and
cover:

- persistent deque updates, subtree sharing, bounded-depth tree shape, splitting, concatenation, cached endpoint
  signposts, logarithmic sorted bounds, sorted lower/upper/equal-range splits, insertion/removal, custom ordering,
  and randomized model replay;
- 32-way RRB radix boundaries, regular-versus-relaxed branch invariants, unequal-height boundary-spine
  concatenation, exact-boundary leaf sharing, 10,000 randomized persistent edits, adversarial density/height
  bounds, cached builder snapshots, non-`Clone` structural operations, and concurrent readers;
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
  and chunk/subtree sharing;
- measured-rope cached count-plus-user measures, measure navigation, persistent point/range insertion and removal,
  slicing, deterministic vector-model replay, subtree sharing, and append-builder snapshot isolation;
- cached-newline text line helpers, Rust string/display conversions, Unicode scalar and UAX #29 extended-grapheme
  addressing, LF/CRLF/CR/mixed detection, CRLF-aware line text, and builder output.

The current validation proves structurally shared Rust storage across the public FingerTree-family facades and the
observable semantic checkpoint behavior, not final C#/C++ lazy-spine asymptotic parity for the whole crate.
