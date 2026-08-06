# Complexity Parity Campaign — Retrospective — 2026-08-06

- Created (UTC): 2026-08-06
- Repository HEAD (written at): ac107dc, revised for detail the same day
- Audience: Maintainers wanting the full story, evidence, and transferable lessons of the
  campaign, written for a reader who did **not** participate in it; the
  [census](complexity-parity-census-2026-08-05.md) remains the row-by-row work order
- Scope: How every asymptotic-complexity divergence across the nine language workspaces was found,
  fixed or explicitly ruled on, and verified — with enough background that each decision can be
  understood, and if necessary revisited, from this document alone

## Background: What Had to Be True, and Wasn't

Durable7 implements one library of persistent (immutable, structurally shared) data structures in
nine languages: C#, C, C++, Rust, Haskell, Kotlin, OCaml, Python, and TypeScript. C# is the
reference workspace: its XML documentation is the normative statement of each structure's
contract, including complexity bounds. The other eight are ports that are supposed to deliver the
same observable behaviour — and, as this campaign made the governing rule, the same asymptotic
complexity, wherever a language makes that possible at all.

The load-bearing substrate in every workspace is the **measured finger tree**: a persistent
sequence in which every node caches a monoidal "measure" of its subtree (a count, a maximum, a
priority summary — whatever the consumer chooses), so that searching by accumulated measure takes
one logarithmic descent instead of a scan. Deques, sorted sets/maps/bags, priority queues,
interval trees, ropes, and several of the research-derived collections are all thin layers over
this one core. Consequently, the core's bounds are almost every other structure's bounds: if the
core's endpoint push is O(1), the sorted map's minimum is O(1); if the core's concatenation is
O(log min), so is the rope's.

Two facts about finger trees matter for everything that follows:

1. **A real Hinze–Paterson finger tree keeps 1–4 elements in "digit" buffers at each end** of
   every level. That is where its cheap ends come from: the first and last elements are literally
   fields of the root, so reading them is O(1) *worst case*, and pushing an element usually just
   widens a digit.
2. **Its amortized O(1) endpoint bound is only valid under persistence if the tree is lazy.**
   Occasionally a push overflows a full digit and cascades into the next level. In a mutable or
   ephemeral structure, classic amortization pays for rare cascades with the cheap operations
   around them. But a *persistent* structure lets a caller keep an old version and re-run the
   expensive operation from it as many times as they like — the "credit" from cheap operations can
   be spent repeatedly, and the amortized bound collapses. Hinze and Paterson's solution is to
   *suspend* the cascade: the overflowing middle is stored as an unevaluated computation, and when
   it is eventually forced, the result is **memoized** — written back into the shared cell — so
   every version that shares that spine pays for the cascade at most once, no matter how many
   branches force it. Laziness is not an optimization here; it is the correctness condition of the
   amortization argument. A language without pervasive laziness must build the memoized suspension
   by hand.

The reference workspace does exactly that: C# holds each deep node's middle behind a memoized
suspension. So do C++ (atomic publication of forced results) and, remarkably, plain C
(hand-rolled memoized middles over reference counts). Haskell gets it from the language. **The
campaign's founding discovery was that the other five workspaces did not have this structure at
all** — they had height-balanced binary "join trees" (AVL or weight-balanced trees storing one
element per node and caching measures), which deliver the same O(log n) *interior* operations but
fundamentally cannot deliver O(1) ends or O(log min) concatenation, because they have no digits
and no laziness. Three of the five nevertheless *called* themselves finger trees, and two
documented bounds their code did not deliver.

## How It Started

On 2026-08-05, while the Python port of the seven research-derived collections was being reviewed,
the maintainer asked why Python's `ContextualRankSequence` documented Θ(s log n) endpoint updates
when the C# reference says O(s) amortized. The honest answer — "because Python's substrate is an
AVL join tree, not a finger tree" — prompted a survey of what each workspace's substrate really
was. That survey immediately caught something worse than a porting compromise: Rust's
`measured.rs` opened with "The measured 2-3 finger tree that most of this crate is built on,"
documented `front()`/`back()` as O(1) (`measured.rs:889–899`), and claimed O(log min)
concatenation — while its node type was `Node { left, right, len, height, measure }`, a plain
join tree, and `front()` recursed the entire left spine (`measured.rs:657–671`). The
documentation was not conservative; it was false.

The maintainer's response set the campaign's charter: *all languages use the same algorithms and
data structures and have the same complexity guarantees (if possible at all); nothing may get
asymptotically worse in the process; and if some language provides a better guarantee, every other
language must be brought up to it (if possible at all).* A second instruction resolved the obvious
objection in advance: where the required bound depends on laziness, strict languages implement
laziness manually.

## What the Census Found

Nine censuses ran in parallel, one per workspace, with explicit instructions to read node
representations and delivered bounds **from the code** and to treat documentation as claims to be
checked — precisely because the founding example was a doc that lied. Before trusting the results,
two of the sharpest claims were re-verified by hand, and both held: Rust's O(1) `front()` claim
against its recursive implementation, and Python's ordered-map position lookup being a binary
search (`persistent_ordered_map.py:588–599`) whose every probe was itself an O(log n) tree
descent — O(log² n) total, where every other workspace pays O(log n).

The full picture, workspace by workspace:

| Workspace | Measured core found | Other findings |
| --- | --- | --- |
| C# | Real lazy H–P finger tree | Reference; one doc under-claim (below) |
| C | Real lazy H–P finger tree, hand-rolled memoized middle | Conforming donor |
| C++ | Real lazy H–P finger tree, atomic publication | Conforming donor |
| Haskell | Real H–P finger tree (native laziness) | But its `SortedSet`/`SortedMap` were **facades over `containers`** (`Data.Set`/`Data.Map`): O(log n) extremes where the reference reads digits in O(1), and `SortedSet`'s rank-slice was Θ(position+length) — it materialized `toList`, applied `take`/`drop`, and rebuilt |
| Rust | **Join tree named `FingerTree`**, false O(1)/log-min claims | Connection forest's CHAMP factor only *expected*: its hasher output was truncated `as u32`, so hash collisions between distinct vertices were possible |
| Kotlin | **Join tree** with amortized-O(1)/log-min claims in `Core.kt` KDoc | — |
| Python | **Implicit AVL join tree** behind a `FingerTree` wrapper name | Ordered family's O(log² n) keyed positions (above); plain deque `reverse()` was Θ(n); reversible-deque cross-orientation concat Θ(n+m) |
| TypeScript | **Measured AVL join tree** in a file family named "finger-tree" | Same two deque gaps as Python |
| OCaml | **Weight-balanced join tree** | Six placeholder substrates (next paragraph); same two deque gaps; plus Θ(n) cursor edits on the reversible deque |

**OCaml deserves its own paragraph, because it was not lying — it was unfinished.** The OCaml
workspace began as a deliberately simplified "checkpoint" port (2026-07-17) whose goal was public
API surface and semantic correctness, with the simplest possible storage underneath and the weaker
bounds *honestly documented*. Six of its structures were still those checkpoints: the sorted
family (`Sorted_set`/`Sorted_bag`/`Sorted_map`) stored each version as a **flat sorted immutable
array** — every write copied the whole array (Θ(n)), building from a list was Θ(n²) repeated
insertion, and set algebra was Θ(n·m); the priority search queue was another flat array with a
cached winner index (Θ(n) writes and pops); the range-update sequence was a flat array that
applied every range tag eagerly to each element (Θ(n) per range edit, where every sibling tags
O(log n) covering nodes lazily); the insertion-ordered family used flat arrays with **linear
scans for membership** (Θ(n) where siblings pay expected O(1) through a hash index); the
Brodal–Okasaki heap stored its elements in a plain list (Θ(n) find-min and delete-min, against
the namesake's O(1)/O(log n) — ironically, a *real* bootstrapped skew-binomial kernel already
existed in the same workspace inside the monotone action heap, which had been ported later to a
higher standard); and `Rrb_vector` was literally `type 'a t = 'a Persistent_deque.t` — a type
alias, not a radix tree. Honest documentation kept these from being bugs; the new parity principle
made them violations anyway, because "documented weaker" was no longer an accepted state.

Finally, one finding ran in the *opposite* direction: Python's freshly ported run-delta vector
documented **worst-case** O(log n) splices, while C# and Rust said "amortized" over code paths
that are just as eager — an under-claim in the reference. Under the new principle, the stronger
true statement wins, so the C# and Rust docs were raised rather than Python's lowered.

Everything above was compiled into a ranked, dependency-ordered work order —
`docs/reviews/complexity-parity-census-2026-08-05.md` — with each violation classified by
severity: placeholder/asymptotically-worse-class first, then the join-tree cores (the keystone,
since most other rows sat on top of them), then amortized-vs-worst-case gaps, conditional
guarantees, and doc-vs-code mismatches.

## What Was Done, Wave by Wave

Twenty-six commits, `7995830` through `4e59684`, over 2026-08-05/06.

### Wave 0 — small, independent fixes that touched no substrate

- **OCaml's Brodal–Okasaki heap** (`f50a7be`) became a real bootstrapped skew-binomial forest by
  finally reusing the kernel its own monotone action heap already contained: O(1) worst-case
  insert/meld/find-min, O(log n) delete-min, and a `statistics` audit that measures the actual
  forest instead of synthesizing shape numbers. A linking-disabled mutant fails the forest-length
  ceiling at 4,095 single-element trees versus 14 real ones at n=4096.
- **Python's ordered family** (`ac8e02f`) replaced the binary-search-over-indexing with the
  representation TypeScript already had: the insertion-order sequence's cached measure became each
  subtree's **maximum stamp** (stamps rise strictly along the sequence), so recovering a position
  from a stamp is one measure-directed descent — O(log n). A counting-measure probe pins it:
  looking up a stamp among 32,768 entries costs at most 24 more `combine` calls than among 1,024 —
  the five extra tree levels — where the old shape would pay hundreds.
- **Rust's and Haskell's connection forests** (`564de90`) got the unconditional CHAMP path factor
  the other five ports already had, by pinning an injective 32-bit vertex hash (an fmix32-style
  bijection over the capped vertex universe) instead of a truncated general-purpose hasher. With
  an injective hash no collision bucket can ever hold two distinct vertices, so "expected O(1) per
  level" becomes "O(1) per level, unconditionally" — the difference between a probabilistic and a
  worst-case guarantee.
- **C#'s and Rust's run-delta-vector docs** (`375e269`) were raised from "amortized" to the
  worst-case wording their eager splice paths actually deliver, matching Python.

### The C1 ruling — the one place "equalize upward" was declined (explained in full below)

### Wave 1 — the keystone: five real cores

The five join-tree workspaces received genuine lazy Hinze–Paterson finger trees **in place — same
module, same public API — so that every consumer inherited the new bounds without source changes.**
The order was chosen for risk: Python first (freshest workspace knowledge, strictest typing
gates, and it became the design document), then Rust, Kotlin, TypeScript, OCaml. Landing commits:
`bc238ca` (Python), `18d216d` (Rust), `0e8aaba` (Kotlin), `9e4a1e1` (TypeScript), `b9434bb`
(OCaml), each followed by a docs commit flipping that workspace's consumer-facing bounds from the
divergence notes to the parity statement (`71ef685`, `b398d73`, `b916882`, `0dfa2ce`, `f344fa0`).

What changed, uniformly:

| Operation | Join-tree workspaces before | All nine workspaces now |
| --- | --- | --- |
| first/last read | Θ(log n) spine descent (Rust *claimed* O(1)) | O(1) worst case — a digit field read |
| endpoint push/pop | Θ(log n), worst *and* amortized (join against a singleton rebuilds the spine) | O(1) amortized, valid under persistent branching; O(log n) worst case per call |
| concatenation | O(height difference), i.e. up to O(log max) — appending a short run to a long sequence paid the long sequence's height | O(log min(n, m)) amortized — the suspended middle recursion terminates at the shallower operand |
| split / index / locate | O(log n) | O(log n), unchanged — the no-regression rule |
| deque `reverse()` (Python, TS, OCaml) | Θ(n) rebuild via to-array-and-back | O(1) — a lazy structural mirror (a fourth suspension operation) |
| reversible-deque concat across orientations (Python, TS, OCaml) | Θ(n+m) re-materialization through lists | O(log min), structural — mirror the smaller operand lazily |

Every strict language expressed the memoized suspension with its own primitive, but all five
publish identically — only after the deferred computation succeeds, so a policy that throws
mid-force publishes nothing and the force can be retried:

| Language | Suspension cell | Notes |
| --- | --- | --- |
| Python | thunk slot rewritten to its value under the GIL | |
| Rust | `Arc` + `OnceLock` | a lost race between threads discards the duplicate result; **also needed an iterative `Drop`** — see lessons |
| Kotlin | `AtomicReference.compareAndSet` | JVM GC handles chain teardown |
| TypeScript | plain field assignment | documented as the one single-threaded workspace where plain assignment *is* the atomic publication |
| OCaml | one mutable field, written under the 4.14 runtime lock | native `Lazy.t` deliberately **not** used — see lessons |

Two implementation facts were non-obvious enough to record. First, **suspensions are stored as
data, not closures**: a sealed/variant type with three cases (push-front overflow, push-back
overflow, concatenate-two-middles), forced by an explicit-stack interpreter. This is not a style
choice — an append loop builds a Θ(n)-deep chain of pending suspensions, and forcing that chain
by recursive calls overflows every bounded stack (see lessons for the experimental evidence).
Second, **element measures are computed eagerly at insertion; only `combine` is ever deferred** —
which is what keeps consumer guarantees like "cached contextual queries invoke the state machine
zero times" true on the lazy substrate.

### Wave 2 — rebuilding OCaml's placeholders on the new core

**Wave 2a** (`a0df414`): the sorted family and the RRB vector. The sorted family moved from flat
arrays onto the new finger-tree core with an order-statistic measure — a `(count,
right-biased-last-key)` pair cached at every node, whose `combine` never calls the user's
comparator — giving O(log n) writes (seek by measure, then split-and-join), O(1) comparator-free
extremes (digit reads), O(n log n) `of_list` (one stable sort preserving first-inserted
representatives, then the eager bottom-up build), and O(m log(n+m)) set algebra. The RRB vector
stopped being a type alias and became a genuine eager 32-way relaxed radix-balanced tree: 5-bit
radix, "packed" regular branches addressed by pure shift arithmetic, size tables only on branches
that relaxation made irregular, concat rebalancing only the seam between the operands.
`Persistent_delta_map`, which keeps three `Sorted_map` roots, inherited the write flip Θ(N+k) →
O(log N) without a source change, and its pinned comparison-budget tests passed untouched.

**Wave 2b** (`dd38499`): the last three placeholders. The priority search queue became the
winner-cached key-ordered AVL every sibling ships — each node caches its subtree's
minimum-priority entry, so `minimum` is an O(1) root read (probe: zero comparator calls against a
live-counter control) and keyed writes are O(log n) path copies (44 comparator calls at n=1,024
versus 64 at n=32,768 — growth tracking depth, not size). The range-update sequence became the
path-copied implicit-key AVL with composable pending tags: tagging a 10-element range and a
10,000-element range of the same 16,384-element sequence now costs 124 versus 162 algebra calls
(an eagerly-recursive mutant pays 20,118), and a whole-sequence tag costs exactly one application
of each kind. The ordered family became the CHAMP-plus-stamped-sequence composite, deliberately
reusing the *fixed* Python design (single-descent stamp lookup) rather than the design Python had
just purged.

### Wave 3 — Haskell's sorted set and map

(`2e7b86f`) Both moved from `containers` facades onto the workspace's own finger tree, following
the pattern its `SortedBag` had always used. Extremes went from O(log n) tree descents to O(1)
digit reads (zero comparisons at n=4,096, against a 30-comparison negative control), and the
linear rank-slice became two measured splits — zero comparisons whether slicing at position 10 or
position 30,000, with allocation differing only 1.29× across that 3,000× position spread. This
wave carried the campaign's one explicit *stop condition*: `Data.Set`'s union is the celebrated
O(m log(n/m + 1)) "hedge" bound, and if the finger-tree rebuild could not match that class, the
no-regression rule required abandoning the extremes upgrade rather than landing a worse union.
The stop was never triggered — run-adopting merges (compare the two fronts, adopt the leading run
of one operand up to the other's front with one boundary split, recurse) achieve the same class,
measured at 2 comparisons for a disjoint 4,096∪4,096 union, a flat 5.0 comparisons per element
for fully interleaved operands, and ~1 split's worth per run for 32 sparse keys into 32,768.
One honest note stayed in: `fromList` remains O(n log n) repeated insertion, and its haddock now
says so instead of implying bulk construction.

### Coda — closing a coverage gap the rebuild exposed

(`4e59684`) The Wave-2b PSQ rebuild had preserved OCaml's existing API surface, which — unlike
every sibling — had never exported the priority-bounded enumeration (`EnumerateAtMost` in C#:
"all entries with key in an inclusive range and priority at most a threshold, in key order").
Under the repository's "both or neither" rule that coverage gap was recorded, then closed: the
new `enumerate_at_most` prunes every subtree whose cached winner priority exceeds the threshold —
the winner cache *is* the pruning structure — delivering the reference's O(log n + v). The probe:
with priorities equal to keys over 4,096 entries and a threshold admitting six, the query costs
at most 168 comparator calls; disable the pruning and it costs 12,289.

## The Two Rulings, in Full

The charter's "if possible at all" clause was exercised twice. Both times the analysis, the
alternatives, and the decision were recorded — in the census's "Deliberate non-violations"
section and here — so a future maintainer can revisit them with the original reasoning in hand.

### Ruling 1: sorted rank-select regressed O(1) → O(log n), and that was the right trade

This concerns **OCaml only** — specifically the flat-array placeholders described above (sorted
family, priority search queue, canonical set, ordered family), all replaced in Wave 2. A flat
sorted array is a wonderful *read-only* structure: `nth i` is direct indexing (O(1)), `min`/`max`
are the ends (O(1)), and binary search gives O(log n) lookups. Its catastrophic cost is
*writing*: an immutable array cannot be edited in place, so every insertion or removal copies all
n slots — Θ(n) per write, and Θ(n²) to build a collection by repeated insertion. The placeholder
arrays were therefore simultaneously **better than the reference at rank-select** (O(1) vs C#'s
O(log n)) and **catastrophically worse at writing** (Θ(n) vs O(log n)).

Fixing the writes means moving to a balanced-tree representation, and this is where the trade
becomes forced: **no known persistent structure supports both O(log n) writes and O(1)
rank-select.** O(1) select effectively requires array-like contiguity (an element's position must
be computable without traversal), and persistent updates to a contiguous representation are
exactly the Θ(n) copies being eliminated; tree representations make both operations O(log n).
One cannot keep the array's select speed and gain the tree's write speed in the same persistent
value.

So the ruling: adopt the **reference profile** — O(log n) writes, O(log n) rank-select, and O(1)
extremes (which a finger tree preserves through its digits, so the most common "fast read" is not
lost) — and treat the arrays' O(1) select as an artifact of the placeholder, not a guarantee to
preserve. What was won and lost, concretely, for the sorted map: writes Θ(n) → O(log n) (the
allocation probe measures ~29 KB per write at n=32,768 where the array copy paid ≥256 KB);
`of_list` Θ(n²) → O(n log n); union Θ(n·m) → O(m log(n+m)); extremes unchanged at O(1); `nth`
O(1) → O(log n), the single loss. Every affected `.mli` states the regression explicitly, so no
one reading the OCaml docs will think it accidental.

### Ruling 2: the level-ancestor arena keeps its Θ(√M) allocation spike

The incremental-ancestor arena is the append-only store shared by two of the research-derived
collections. It keeps its nodes in **blocks of odd sizes 1, 3, 5, …** — a scheme whose square
boundaries make handle-to-slot addressing pure arithmetic and bound wasted slots by O(√M) after M
nodes. The cost of that scheme: when a block fills, the next block (size ~2√M) is allocated in
one step, so although `add_leaf` is O(1) *amortized*, a single unlucky call pays Θ(√M). Haskell's
port, alone, has no arena at all — each node is an ordinary heap object that *is* its own handle —
so its leaf-add is O(1) **worst case**. Under the charter, Haskell's stronger guarantee should
become everyone's target "if possible."

It *is* possible — but the honest mechanism is heavier than it first looks, and the first design
written into the census was actually wrong. "Pre-initialize the next block incrementally" fails
in garbage-collected languages because allocating an array *zeroes it*, which is itself Θ(block)
work in one call, and growing a list by appends hits the allocator's geometric-resize copies. The
correct classic mechanism is **real-time two-table migration**: keep the old and new backing
tables live simultaneously and migrate a constant number of slots on every operation, so no
single call ever performs a large copy. This was analyzed, found implementable in all eight
arena workspaces, costed — permanent extra complexity in the hottest path of eight
implementations, to shave a √M-sized allocation that the memory allocator performs — and
**declined by the maintainer**: "Let's accept Θ(√M) worst-case and document that."

What the parity principle then demanded was uniform *documentation*: all seven arena
implementations now carry the identical two-part statement (O(1) amortized; a block-boundary call
pays Θ(√M) for the next odd block), and Haskell's module doc explicitly marks its O(1) worst case
as *exceeding* the shared profile rather than silently diverging from it (`932d670`). The census
records the declined alternative so the decision is revisitable.

## The Evidence Discipline

The campaign's verification standard came from one observation: **asymptotic claims are invisible
to correctness tests.** A join tree passes every behavioural test a finger tree passes; a linear
scan returns the same entries a pruned search returns. Three practices closed that gap.

**1. Counting probes pin the bound.** Every workspace's policy objects (comparators, measures,
algebras) were given counting variants, and the probe suites assert exact call counts, not
ceilings. The signature result: in **all five new cores, the numbers are byte-identical** — 512
endpoint pushes cost exactly 340 `combine` calls at n=1,024 *and* at n=32,768 (cost independent
of size, the definition of the amortized-O(1) claim); concatenating a 4,096-element tree with a
1-element operand costs 0 immediate combines and with a 1,024-element operand costs 4 (cost
tracking the smaller operand, not the height difference); a 10,000-append spine branched into 49
retained versions pays 3,336 combines when the first branch forces it and exactly 5 per branch
thereafter (memoization shared across persistent branches — the Hinze–Paterson argument made
observable); `front`/`back`/`length` make zero policy calls. Five languages, five memoization
primitives, one algorithm, one fingerprint: cross-language parity as a *measured* property.

**2. Every load-bearing mechanism was mutation-checked** — deliberately broken, shown failing
with a characteristic signature, restored. A selection: removing the arena's jump-link coalescing
turns 2 hops into 32,768 against a bound of 64 while every *answer* stays correct (only the hop
counter can catch it); disabling the RRB seam merge produces exactly the predicted 544 leaves
where merging gives 64; making range-update tags eager costs 20,118 algebra calls against a
600-call bound; disabling the PSQ's winner pruning costs 12,289 comparisons against 168;
reverting the ordered family's stamp descent to binary search trips a *zero-combine lower bound*
(a size-directed index performs no combines at all — so the probe asserts the counter moved,
which the mutant cannot satisfy). A probe that has never been seen to fail proves nothing; these
all failed on demand.

**3. When the obvious counter cannot see the fix, measure something that can.** Two examples.
The OCaml sorted-family rebuild is invisible to a comparator counter, because the *old* array's
binary-search seeks were already logarithmic — the violation was in the Θ(n) write *copies*,
which perform no comparisons — so the probe pins bytes allocated per write instead (~29 KB vs
≥256 KB at n=32,768). And Haskell's slice probe initially measured **zero bytes** for both slice
positions — GHC's optimizer had constant-folded the entire measurement away — so the positions
are routed through an `IORef` the optimizer cannot see through, after which the honest numbers
appeared (and the deliberately broken variant showed a 209× position-dependent signature).

## Equalize-Upward Events

The charter's most distinctive clause — a stronger guarantee *anywhere* becomes the target
*everywhere* — fired twice in directions nobody planned, and both times mid-campaign work in one
language improved another.

**TypeScript strengthened the reversed view; Python was upgraded to match the same day.** The
lazy O(1) `reverse` works by mirroring a tree lazily. Python's original implementation reused
each mirrored node's *cached measure*, which is only correct when the measure's `combine` is
commutative (for a size measure, reading a subtree forwards or backwards gives the same count —
fine; for, say, a string-concatenation measure, the mirror's summary is **not** the cached
summary, it is the parts recombined in the opposite order). Python documented that limitation as
a caller obligation. The TypeScript port, hitting the brief's requirement to prove correctness
under a *non-commutative* measure, instead recombined each mirrored node's summary in mirrored
order — O(1) extra combines per node, paid only when the reversal suspension forces, correct
under **every** monoid with no commutativity caveat at all. That is a strictly stronger
guarantee, so Python's implementation was rewritten to match (`8790f16`), verified by a
spelling-measure probe (measure = the element's string, combine = concatenation) that checks the
mirrored root summary, both halves of an interior split of the mirror, and a double reversal —
a probe that the cached-measure implementation fails and only reversed-order recombination can
pass. OCaml's core, ported afterwards, was required to match the stronger shape from birth.

**Python's honesty raised the reference's docs.** As noted under Wave 0: the reference described
its eager run-delta splices as "amortized" — true but weaker than delivered — and the freshly
ported Python workspace stated the worst-case bound. The stronger true statement became the
standard wording in C# and Rust.

## Transferable Lessons

1. **Documentation is a claim to falsify, not a fact to inherit.** Two core modules asserted
   bounds their code did not deliver; a third module's header described ("range enumeration …
   prunes whole subtrees through the winner caches") an API that no export provided. Every
   census was code-first, and the two most surprising findings were hand-verified before the
   document was trusted. The inverse case also occurred: the *reference* under-claimed a bound
   its own code delivered.
2. **Amortization under persistence requires laziness, and laziness at scale requires
   defunctionalization.** An append loop builds a Θ(n)-deep chain of pending suspensions. Forcing
   that chain with ordinary recursive calls overflows any bounded stack: Python's 1000-frame
   limit made this obvious immediately, and — settled by a direct experiment before the OCaml
   port was designed — **even OCaml's native `Lazy.force` overflows on a 200,000-deep chain**, so
   "the language already has laziness" was not an exemption. Every new core therefore stores
   pending operations as *data* and forces them with an explicit-stack interpreter; each core's
   probe suite forces a 200,000-element organically built chain as a regression test. Rust
   contributed a fourth variant of the hazard: *dropping* an unforced chain recurses through
   `Arc` destructors (tracing GCs hide this; reference counting does not), so its suspension type
   implements an iterative `Drop` that unlinks the chain with `Arc::into_inner`. Whether the
   three original donor cores (C#, C++, C) survive the same 200k-deep force/release chains was
   never tested during the campaign and is filed as an open probe — the hazard the five new cores
   were built to resist has not been checked against the three implementations that inspired
   them.
3. **A no-regression rule needs both an escape valve and a stop condition.** The valve — "if
   possible at all," exercised through explicit recorded rulings — absorbed the two genuine
   impossibilities (select-vs-write, spike-vs-complexity) without either silent regressions or
   heroic over-engineering. The stop condition — the Haskell set-algebra instruction to abandon
   the upgrade rather than regress union below the hedge class — never fired, but its presence is
   exactly why the "it held" outcome is trustworthy rather than lucky.
4. **Parity is testable.** The most durable artifact is not any single fix but the shared probe
   fingerprint: identical operation counts, in every language, asserted as equalities in every
   suite. A future substrate change that silently breaks parity now breaks a pinned number.
5. **The work order is code: review it, and fix it before it executes.** The census was corrected
   twice mid-campaign — once when analysis showed its proposed C1 mechanism (naive block
   pre-initialization) could not deliver the claimed bound in GC languages, once when the ruling
   dissolved that wave entirely — each time *before* an implementing agent could faithfully build
   the wrong design. A final consistency sweep also caught the campaign's own bookkeeping
   violating the repo's checklist (the census was never indexed in the reviews catalog) and two
   rows fixed before the strike-through convention existed that had never been struck.
6. **Preserve the surface, replace the engine.** Every substrate swap kept the public API
   byte-compatible (verified mechanically: comment-stripped `.mli` diffs, `.d.ts` diffs, export
   lists against `git show HEAD:`), which is what allowed consumers — and their pinned
   behavioural tests — to act as free regression harnesses for every rebuild.

## Final State

All nine workspaces conform. Every census row is struck with its landing commit and probe
numbers; the substrate map reads nine conforming rows where it read five violations at the start.
Full gates are green at HEAD in every touched workspace: Python 430 tests; Rust 30 test binaries
(243 in the finger-tree crate); Kotlin 299 results across its three workspaces; TypeScript 265;
OCaml 176; Haskell's finger-tree suite passing with the rebuilt sorted family; C#, C, and C++
unchanged except documentation and passing where touched. The single open follow-up is the donor
deep-chain probe described in lesson 2. Everything through this retrospective is pushed to
`origin/main`.
