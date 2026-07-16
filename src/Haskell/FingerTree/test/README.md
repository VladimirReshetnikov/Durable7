# Haskell FingerTree Tests

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents validating the Haskell FingerTree port
- Scope: `tools-data-structures-fingertree` test executable

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
