# TypeScript Collection Port — 2026-08-06

- Created (UTC): 2026-08-06
- Repository HEAD (reviewed): `main`, post complexity-parity campaign
- Audience: Maintainers reviewing the completion of the nine-language shipment
- Scope: The TypeScript port of all seven research-derived collections plus the level-ancestor seam
  two of them share, the intentional divergences it makes, and the verification evidence

## Decision Recorded

The seven research-derived collections are now ported to TypeScript, making coverage **all nine
languages**: C#, Rust, C, Haskell, Kotlin, C++, OCaml, Python, and TypeScript. No workspace remains
unported, so the parity-economics rule that governed the staged rollout no longer has an open case.
The seven earlier port reviews carry current-state notes; the most recent points here.

## What Shipped

| Collection | TypeScript module |
| --- | --- |
| `AncestralSliceQueue<T>` | `finger-tree/ancestral-slice-queue.ts` |
| `BilateralAncestralDeque<T>` | `finger-tree/bilateral-ancestral-deque.ts` |
| `ContextualRankSequence<TElement, TMachine>` | `finger-tree/contextual-rank-sequence.ts` |
| `PersistentDeltaMap<TKey, TValue>` | `finger-tree/persistent-delta-map.ts` |
| `PersistentRunDeltaVector<T>` | `finger-tree/persistent-run-delta-vector.ts` |
| `PersistentMonotoneActionHeap<TElement, TPriority, TAction>` | `finger-tree/persistent-monotone-action-heap.ts` |
| `PersistentAncestralConnectionForest` | `hamt/persistent-ancestral-connection-forest.ts` |

`finger-tree/incremental-ancestor.ts` carries the level-ancestor seam: the
`IncrementalAncestorArena<T>` interface plus the shipped `MyersIncrementalAncestorArena<T>`. Every
name is exported from its family subpath and from the root `durable7` namespace, which resolves all
of them with no collision across the three packages.

`statistics()` is deliberately **not** on the seam interface — it describes one backend's
representation (odd-block counts, hop counters) rather than the contract. Each consuming collection
therefore takes a caller-supplied arena through a `withArena` factory alongside a convenience
factory that builds its own, so a test wanting an exact query profile constructs the Myers arena
itself and reads counters off its own reference. This is the same shape the Python port settled on
and it costs nothing here, because TypeScript's structural typing means a backend satisfies the
interface without declaring that it does.

## The First Port That Inherits Reference-Strength Substrates

TypeScript was ported last, after the complexity-parity campaign
([census](complexity-parity-census-2026-08-05.md),
[retrospective](complexity-parity-retrospective-2026-08-06.md)) rebuilt five workspaces' measured
cores. It is consequently the only port that never had to weaken a bound:

- **`ContextualRankSequence` states the reference's own bounds** — O(s) amortized endpoint updates,
  valid under persistent branching, and O(s log(min(n, m))) amortized concatenation — because
  `measured-sequence.ts` is a genuine lazy Hinze–Paterson finger tree with memoized defunctionalized
  suspensions. Rust, Kotlin, OCaml, and Python all originally shipped Θ(s log n) endpoints and a
  height-difference concatenation bound, and all four carried substrate-divergence notes until their
  cores were replaced. The claim here is probe-backed rather than asserted: a warm whole-sequence
  `evaluate` charges exactly 0 summary compositions at 4,096, 32,768 and 262,144 elements, and
  `get(index)` charges 0 compositions and 0 machine transitions at all three lengths.
- **`PersistentDeltaMap` gets O(1) worst-case extremes**, because `SortedMap.minEntry`/`maxEntry`
  are `front()`/`back()` digit reads on the same core. Its range-restricted change enumeration is a
  genuine boundary seek through `SortedMap.getKeyRange` — an inverted-range check under the retained
  comparator, then `lowerBound`/`upperBound` and two measured splits — performing zero value-equality
  callbacks. The C port's filter-over-all-changes defect had no way to recur, and a mutation confirms
  two tests fail if it is introduced.
- **`PersistentRunDeltaVector` states worst-case splice bounds**, because `rrb-vector.ts` is a real
  eager 32-way RRB that defers nothing — matching the wording the C# and Rust docs were corrected to
  during the campaign, not the "amortized" they originally carried.
- **`PersistentMonotoneActionHeap`** inherits O(1) worst-case insert, meld and minimum and O(log n)
  delete-min from a real bootstrapped skew-binomial forest.
- **`PersistentAncestralConnectionForest` claims the unconditional CHAMP path factor**, the stronger
  of the two claims the sibling ports disagree on, by pinning an injective vertex hash over a capped
  universe. The CHAMP is real (32-way bitmap nodes, 5 bits per level, a hard 7-level ceiling), and
  injectivity was verified by round-tripping through an explicitly constructed modular inverse
  rather than asserted.

## Four JavaScript Hazards, Handled Rather Than Documented Away

- **No lock is documented on the arena.** The managed reference serializes every operation under a
  private lock. JavaScript has no shared-memory concurrency for ordinary objects, so there is no
  such contract to reproduce and none is invented.
- **The baseline's three arithmetic ceilings are unreachable and are not imitated.** Ancestry depth,
  node count, and coalesced jump distance are all memory-bound long before `Number.MAX_SAFE_INTEGER`,
  so no artificial limit is synthesized and the arena's counters are exact rather than saturating.
- **Identity needs boxing.** `===` is value equality on numbers and interned strings, so "a clean
  position reuses its exact checkpoint cell" would be vacuous for precisely the payload types a test
  reaches for first. The run-delta vector boxes every element in a private identity-bearing cell.
  Notably `rrb-vector.ts`'s leaf writer short-circuits on `Object.is`, not on value equality, so it
  can never silently keep an equal-but-different object — the boxing exists to make the invariant
  *observable*, not to defeat a substrate rule.
- **Presence must be explicit.** `undefined` is a legal stored value, so change endpoints and absent
  results use explicit wrappers wherever a bare `T | undefined` would confuse "absent" with
  "present and undefined".

Naming follows the sibling ports where JavaScript idiom demands it: `Slice` becomes `getRange`
(`slice` reads as `Array.prototype.slice`'s start/end convention), C# indexers become `getAt`,
`TryXxx(out …)` pairs become `Xxx(): Result | undefined`, and `Create`/`CreateRange` become the
workspace's `empty`/`from` static factories. Mixing two retained policy objects throws `TypeError`,
matching `BrodalOkasakiHeap.meld`.

## Adversarial Audit

Sixteen agents audited all eight modules against the C# reference, each finding passed to a separate
skeptic instructed to refute it by default and to settle vacuous-test claims by mutating a scratch
copy rather than by argument. Fourteen candidates were raised and **ten confirmed; four were refuted
by execution**. The largest module — the monotone action heap, at ~1,000 lines the most defect-prone
of the seven — produced **no findings at all**, and all three candidates against the connection
forest were refuted.

**Exactly one confirmed finding was a behavioural divergence**, and the other nine were unfalsifiable
tests or claims that did not match the substrate — the failure mode this repository cares most about.

1. **`removeAt` published a non-canonical empty** (behavioural). The reference canonicalizes a
   removal that drains a sequence, and this module canonicalizes in `from`, `splitAt` and `getRange`
   — but not in `removeAt`, so two structurally identical empty sequences over the same retained
   machine could coexist with different identities. Fixed with one branch; the existing assertion
   checked only `.isEmpty`, so a regression test now pins identity against the canonical instance and
   fails without the fix.
2. **A laziness claim no test could observe.** `dirtyRuns()` documents that abandoning the walk early
   skips the rest, but the test only pulled two values — which an iterator over a fully materialised
   array satisfies identically. A mutation replacing the generator with an eager form passed the
   whole file. The test now checks the returned object is a generator, that it is its own iterable,
   and that it resumes after abandonment; the eager mutant fails it.
3. **Three assertions that could not fail.** The anchored-empty payoff compared two freshly published
   arena handles, which always differ because handles are never recycled — it now compares the
   *anchors* those appends hang off, which is the operative property. A block-boundary check asserted
   the integer identity (r+1)² − r² = 2r+1, touching no module code — it now reads the arena's real
   block schedule. And a negative control allocated its right operand inside the `expect` call, so no
   value could ever equal it — it now compares against a structurally equal object held in a variable.
4. **Two untested policy paths in the delta map.** Cancellation is decided by the retained value
   relation, but every cancellation assertion used the default relation on primitives, where it
   coincides with `Object.is`; replacing the retained call with `Object.is` left the file green. And
   `sharesStorageWith` is a conjunction over three roots, but no test pair shared *some but not all*
   of them, so `&&` could be `||`. Both gaps now have tests, and both mutations now fail.
5. **Two claims that did not match the substrate.** `getChanges` documented Θ(log(k + 1)) for the
   first change — spine-descent wording inherited from balanced-BST iteration. A finger tree keeps
   its leading elements in the root's prefix digit, so the true cost is Θ(1): an *understatement*,
   which the parity rule treats as a defect exactly like an overstatement. And a comment I wrote in
   `index.ts` claimed seven collections above a block exporting six plus the seam.

Findings 3 and 5's second item were in code the integrator wrote rather than a porting agent —
the same pattern as the Python port, where the audit's most valuable catch was also against the
integrator's own arena test.

## Verification

- `npm run validate` passes end to end: strict `tsc --noEmit` under `isolatedDeclarations`,
  `exactOptionalPropertyTypes`, `noUncheckedIndexedAccess` and `verbatimModuleSyntax`; the full
  Vitest suite; a clean rebuild of the ESM output and declaration files.
- **403 tests across 46 files, up from 265 at baseline.** The 138 new cases are 7 for the arena seam,
  13 + 17 for the ancestry-interval sequences, 21 for the contextual sequence, 17 for the delta map,
  14 for the run-delta vector, 26 for the action heap, and 23 for the connection forest.
- Every load-bearing property was mutation-checked rather than assumed to bite. Beyond the audit
  remediations above, the porting agents confirmed: reversing either tag-pushdown composition site
  fails a named case (the forest-spine site isolated by a scenario where every tree-side composition
  is against the identity, so only the stacked spine cell can catch it); an eleven-mutation battery
  against the run-delta vector killed ten by test, the eleventh being the laziness gap the audit then
  closed; and a fifty-one-mutation sweep of the deque's indexed reads was killed entirely by the
  exact query-profile tables.
- The arena's headline bound is discriminating by construction: with coalescing removed, a
  32,768-node chain needs 32,768 hops against a stated ceiling of 64 while every ancestor *answer*
  stays correct, so only the hop count can catch it. The branching-shape construction is itself
  pinned — a fork count, not a distinct-parent count, since a pure chain maximises distinct parents
  and the original guard was implied by its neighbour.
