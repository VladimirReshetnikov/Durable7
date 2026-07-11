# Haskell FingerTree Tests

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents validating the Haskell FingerTree port
- Scope: `tools-data-structures-fingertree` test executable

Run from `src/Haskell`:

```powershell
.\test.ps1 -Workspace FingerTree
```

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
RRB-vector coverage exercises every 32-way boundary through 100,000 elements, unequal-height
append, regular-versus-relaxed metadata, split round-trips, optimized root reuse, a 10,000-command
list model with retained snapshots, 2,000 adversarial split/rejoin operations, uneven fragments,
and concurrent pure readers. Every history checks cached count/height/size-table invariants and
density ceilings.
