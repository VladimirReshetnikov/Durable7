# Rust FingerTree Validation

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers validating the Rust FingerTree-family workspace
- Scope: Cargo commands, warning policy, and test coverage

Run from `src/Rust`:

```powershell
cargo test -p tools-data-structures-fingertree
```

If Cargo is available under rustup but not on `PATH`, use:

```powershell
& $env:USERPROFILE\.cargo\bin\cargo.exe test -p tools-data-structures-fingertree
```

The crate uses `#![forbid(unsafe_code)]`. Unit tests are inline in the module files under `FingerTree/src/` and
cover:

- persistent deque updates, subtree sharing, bounded-depth tree shape, splitting, concatenation, sorted search, and
  randomized model replay;
- general measured tree cached-measure validation, subtree-sharing splits, randomized prefix-measure locate checks,
  key lower/upper-bound splits, product-measure component splits, cumulative-weight selection, and min/max
  extraction helpers;
- reversible-deque O(1) storage-sharing reversal and wrapper-preserving logical edits over the shared deque tree;
- measured sequence size/sum/min/max/key/product/order-statistic policies;
- sorted bag/set/map rank, navigation, algebra, proper set relations, duplicate handling, inclusive value/key
  ranges, cached order-statistic measures, and shared-storage edits/ranges;
- stable priority dequeue, meld behavior, cached minimum-priority measures, and shared-storage
  enqueue/meld/dequeue paths;
- closed interval overlap, containment, coalescing, cached maximum-high measures, and shared-storage
  insert/remove paths;
- chunked positional rope construction from chunks, caller-supplied copy targets, edits, cached length measures,
  chunk/subtree sharing, measured-rope cached count-plus-user measures, measure navigation, and subtree sharing,
  cached-newline text line helpers, Rust string/display conversions, and builder output.

The current validation proves structurally shared Rust storage across the public FingerTree-family facades and the
observable semantic checkpoint behavior, not final C#/C++ lazy-spine asymptotic parity for the whole crate.
