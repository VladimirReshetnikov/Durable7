# Haskell FingerTree Tests

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents validating the Haskell FingerTree port
- Scope: `durable7-fingertree` test executable

Run from `src/Haskell`:

```powershell
.\test.ps1 -Workspace FingerTree
```

The wrapper appends `--jobs=1`, so Cabal compiles and links with one build job even when a caller
configuration requests parallel workers.

The dependency-free executable covers measured-tree split/view semantics, deque indexing and sorted
search, reversible deque orientation plus all mixed-orientation append combinations, sorted
bag/set/map facades, stable priority dequeue, interval queries and coalescing, positional ropes,
measured ropes, newline-aware text helpers, and `forkIO` concurrent readers over shared immutable
snapshots. A 200,000-element construction stress case guards the strict bulk-fold path against
space-leaking thunk chains. Boundary-focused rope tests cross the 64-element chunk limit and use
`StableName` identity to prove an edit retains an untouched far chunk under optimized GHC. Interval
coverage exercises equal-low order plus max-high prefix pruning and overlap enumeration; measured
rope and text cases cross multiple chunks while checking cached totals, splits, and line navigation.
Sorted-bag rank coverage exercises 20,000 distinct keys plus a 100,000-instance single bucket,
including measured count bounds, final-rank access, and slices both across and within buckets.
Deque sorted-bound coverage uses a 65,536-element tree and a counting comparator to enforce a
logarithmic comparison ceiling for lower bound, upper bound, and binary search.
Interval-map coverage adds exact payload replacement, retained-source lookup, overlap enumeration
and counting, removal, and cross-index validation over equal-low intervals.
Chunked-bit-set coverage crosses word seams, checks duplicate and missing no-ops, inclusive rank,
select, union/intersection/difference, retained snapshots, ascending enumeration, and cached
measure validation.
Priority-search-queue coverage includes last-wins keyed construction, minimum/tie semantics,
range/priority filtering, ascending-key adversarial AVL construction, cached-winner validation,
and a 10,000-operation map model with retained immutable snapshots.
Brodal-Okasaki coverage checks persistent insert/meld, self-meld, retained operands, sorted drains,
fused child/embedded-forest invariants, rank/depth bounds, and a 20,000-value deterministic random
history against `Data.List.sort`.
RRB-vector coverage exercises every 32-way boundary through 100,000 elements, unequal-height
append, regular-versus-relaxed metadata, split round-trips, optimized root reuse, a 10,000-command
list model with retained snapshots, 2,000 adversarial split/rejoin operations, uneven fragments,
and concurrent pure readers. Every history checks cached count/height/size-table invariants and
density ceilings.

Range-update-sequence coverage exercises affine assignment/addition composition in both observable
orders, value-distinct identity tags, noncommutative ordered measures, every indexed edit and
split/join/range boundary, retained snapshots, exact root reuse for no-op operations, and a
deterministic 240-command list model whose every version is structurally validated. A shared-DAG
construction reaches the exact `Int` maximum and proves insertion and concatenation reject growth
before corrupting the retained maximum-count source.

Positional-cursor coverage locks empty/start/end gap behavior, nested-`Maybe` peeks, invalid seek
and edit results, exact snapshot/no-op identity under optimized GHC, chunk-boundary edits,
equality-free representative replacement, retained branches, and far-chunk sharing. Measured-cursor
coverage adds ordered noncommutative before/after partitions, absolute monotone-prefix search over
clean and edited versions, callback failure/retry, exact measured snapshots, and text-cursor helper
parity at every `Char` gap across supplementary-plane, combining-mark, CRLF, and LF content. Separate
deterministic 750-command positional and measured list/gap models cover movement, seek, insertion,
deletion, replacement, measures, and snapshots; concurrent readers exercise retained positional,
measured, and text cursors. Shared-DAG constructions reach the exact `Int` maximum without
materializing their logical elements, then check every positional/measured rope and cursor growth
path for length overflow before new element/monoid callbacks, exception atomicity, lazy range-spine
short-circuiting, and continued usability of all sources. An all-newline maximum DAG also locks
checked line-count derivation.

Canonical zip-zip-set coverage pins caller-keyed and `ZZT2` public-seed HMAC rank vectors, unsigned
secondary-word priority, hidden random-key separation, construction-order-independent bulk and
incremental topology, first-representative retention, incoherent equivalence hashing, and separate
same-seed policy identity. A 10,000-command `Data.Set` model retains immutable snapshots; a fully
colliding 4,096-node chain exercises stack-safe deletion, reinsertion, enumeration, digesting, and
validation. Algebra and all set relations cover policy gating plus receiver-defined cross-policy
asymmetry. `StableName` diagnostics require at least 90 percent off-path node retention for edits,
and a hostile mutable test hash proves the validator detects non-reproducible ranks.

The seven research-derived collections add six groups to this executable. Ancestral slice queue
coverage exercises the anchored-empty rule at every boundary, front- versus back-drained empties
keeping different anchors, all 2,211 ranges of a 65-element source, every take/drop/split boundary of
a 257-element source with branching from both sides, the odd-block square seams through 128 squared,
and an ancestor-query discipline built on hop counts: suffix slices, whole prefixes, zero drops, and
a split at the count are proved query-free while a split at zero is proved to query. Bilateral
ancestral deque coverage asserts exact query profiles rather than ceilings alone - the four cached
endpoints index for free, the slice ceiling of two is reached and never exceeded across all 45
range pairs, both split endpoints and the centre boundary are free, and a cross-centre drain
exercises the count-equals-two shortcut - plus reversal laws, exhaustive construction words for
lengths zero through eight, and a five-seed randomized model with retained-version re-checks.

Contextual rank sequence coverage pins explicit composition order through a non-commutative pair,
exhaustive words of length zero through seven over a three-symbol alphabet, and a counting machine
that proves construction costs exactly s times n transitions, ranks cost none, and each select costs
exactly one. Delta map coverage pins every checkpoint-differential rule: policy-equal writes sharing
every root, first-write before-capture and coalescing, set-then-restore and add-then-remove
cancellation, snap-back to the checkpoint root, representative retention across delete and re-add,
and a callback budget showing full enumeration of a 16,384-entry index costs no policy calls. A
3,000-step randomized history checks both models at every step. Run-delta vector coverage proves the
splice is comparison-free under a counting policy with a positive control, that cancellation
restores the exact checkpoint representative under both token and StableName identity, all five
run-index transitions, the runs-versus-dirty-positions gap over 8,192 positions, and the
non-reflexive NaN hazard the reflexive-IEEE policy exists to avoid. Monotone action heap coverage
pins the clamp algebra's collapse cases and boundary representatives, composition direction and
associativity over an eight-action cross product, the temporal rule that an insertion after a
transform is not retroactively transformed, tie-breaking, deeply tagged structural validation, and a
randomized retained-branch model.
