# Review: Axis 2 Lifecycle And Sequence-Cursor Plan

- Status: Historical review — dispositioned in the authoritative final plan
- Created (UTC): 2026-07-13T04:01:36Z
- Repository HEAD (reviewed): 4376db84b198ee5be7d3ee9dc62cb3e9c8b46149
- Audience: Maintainers studying the historical Axis 2 design review and its disposition
- Scope: A design review of the
  [`docs/proposals/axis2-lifecycle-and-sequence-cursors.md`](../proposals/axis2-lifecycle-and-sequence-cursors.md) at revision `4376db8`
  — soundness, feasibility against the then-current C# representations, gaps and risks, and a
  sequencing/scoping recommendation. A companion re-sequenced alternative is proposed separately in
  [`docs/proposals/axis2-cursor-first-alternative-2026-07-13.md`](../proposals/axis2-cursor-first-alternative-2026-07-13.md).

> **Final disposition (2026-07-13):** The
> [Axis 2 final plan](../proposals/axis2-lifecycle-and-sequence-cursors.md) incorporates this review's
> cursor priority, frozen signal gate, branched-history proof obligation, transient workload
> qualification, and documentation duties. It corrects the review's claims about the samples as
> current consumers, a cheap calendar-sized transient spike, few-edit transient wins, canonical
> frozen order, and measurement alone proving amortized branching. The findings below remain the
> review of the pinned SHA, not the current execution plan.

## Overall assessment

This is a strong, unusually honest plan, and its designs should be adopted. It correctly separates
the five lifecycle states that were previously conflated (builder / transient / persistent / frozen /
snapshot view), it retracts the naive "cursor makes a canonical rope reappear in O(1)" claim that the
[2026-07-09 proposal's A3](../proposals/new-data-structures-2026-07-09.md) implied, it gates every
speculative tier on measured evidence, and it defers automatic size-tiering and key-type dispatch
with a named re-entry rule. The complexity tables are correct and refuse to overclaim (no worst-case
O(1) local edit; amortized bounds attributed to the finger tree's memoized spine; line/column
correctly flagged as needing a richer monoid). The cross-language posture is realistic and the
per-language hazards (Rust `Arc` adoption may not be O(1); C++ move-only breaks copy-on-first-write; C
needs a failure-atomic ownership design; Haskell prefers `ST`) are accurate.

The recommendation is: **adopt the designs, but re-sequence and add two cheap pre-gates.** The plan
leads with, and ships first, the two tracks whose value is benchmark-contingent and which have no
in-repo consumer (the CHAMP transient and the frozen tier), while the one deliverable with named
in-repo consumers and the clearest differentiation — the rope cursor — is sequenced fourth. That
inverts the repository's own consumer-driven rule. The details follow.

## Feasibility verified against the current code

Every load-bearing feasibility claim checks out against the shipped C#:

- **Owner-token CHAMP nodes are addable.** `PersistentHashMap` has a `Node` hierarchy (`HashNode`,
  `BitmapIndexedNode`, `CollisionNode`, `LeafNode`); an optional token reference on the
  mutable-capable bitmap node is the standard Clojure-transient shape. The plan is honest that the
  retained token is a real per-edited-node memory cost and refuses to hide an O(n) tag-clearing pass
  inside "publication" — the correct discipline.
- **The transient is genuinely distinct from the shipped `BulkBuilder`.** `CreateBulkBuilder` /
  `ToImmutable` ([PersistentHashMap.cs:1498](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.cs))
  is list/bucket staging that rebuilds canonically — not owner-token copy-on-write over adopted
  persistent nodes. The plan's vocabulary table draws this line correctly.
- **L4 is grounded.** `SnapshotView.ToPersistentHashMap()` already exists
  ([ConcurrentHashTrie.cs:777](../../src/CSharp/src/Durable7.Hamt/ConcurrentHashTrie.cs)),
  so "retain it and add `SnapshotView.Freeze()`" is a small, well-scoped addition.
- **The Rope chunk model supports the cursor.** `Rope<T>` already maintains `MinChunkSize`/
  `MaxChunkSize`, a chunk-count ≈ n/Target invariant, boundary coalescing, and `TryViewLeft`/
  `TryViewRight` endpoint views ([Rope.cs](../../src/CSharp/src/Durable7.FingerTree/Rope.cs)).
  The cursor's carry/flush model (at most one partial carry per side, flush only target-sized chunks,
  pull via endpoint views, pack carries at `Snapshot()`) maps directly onto that invariant, and its
  motivating inefficiency is real: `AddFirst`/`AddLast` are documented as "copies the boundary chunk
  each call."

So the plan is buildable as written; the review's concerns are about **risk-adjusted sequencing and
two under-specified spots**, not feasibility.

## Strengths worth preserving

1. **Vocabulary discipline.** Naming builder/transient/persistent/frozen/snapshot-view as distinct
   states, and reserving `FrozenHashMap` for the terminal read tier, prevents the "frozen prefix" vs
   "frozen Ctrie generation" vs "frozen collection" confusion the repo was drifting toward.
2. **The complexity correction (Track C).** Explicitly killing the O(1)-canonical-rope myth and
   giving `Snapshot()` its true O(log n)-when-dirty / O(1)-when-clean cost is exactly right, and the
   honest cursor complexity table is a model for how the repo documents amortized structures.
3. **Prepare/commit failure atomicity.** Requiring every failure-prone callback and every allocation
   before the first in-place write — operation-wide, not per-node — is the correct strong-exception
   discipline for an in-place transient and matches the repo's existing failure-atomic native
   contracts.
4. **Benchmark gates as shipment preconditions.** The transient must demonstrate O(1) adoption/
   publication counters (no traversal); the frozen tier must demonstrate a named read-heavy win.
   Making the counters and the break-even read count explicit shipment gates is the right rigor.
5. **Deferral hygiene.** The "explicitly postponed" list plus the "re-entry requires a named consumer
   or benchmark" rule keeps the speculative frontier from leaking into committed scope.

## Findings

### F1 — Sequencing inverts the repository's consumer-driven rule (Major; recommend re-order)

The rollout order ships the CHAMP transient (L1) first and the positional cursor (C1) fifth, after
the frozen bake-off and frozen map/set (L2/L3). Yet:

- The **cursor has named in-repo consumers** — the Editor and Tour samples are text-editing programs
  whose localized insert/delete/movement is precisely the cursor's workload; the plan's own C3 step
  rewrites them onto it. The **transient and frozen tiers have no named consumer**; both are gated on
  benchmarks alone.
- The **cursor is the most differentiated** deliverable: no mainstream .NET persistent-collection
  library ships an editor-grade version-bound rope cursor, whereas frozen hash maps (`FrozenDictionary`)
  and transient builders (Clojure, `ImmutableDictionary.Builder`) are well-trodden.
- The **cursor's value does not depend on beating an already-fast structure** (see F2), only on
  beating indexed `Rope` edits on local histories — a gap the plan itself documents as large
  (`AddFirst` copies a boundary chunk every call).

The plan invokes the consumer-driven rule repeatedly, then sequences against it: the first
user-visible Axis 2 feature to ship is a performance tier, not the consumer-backed capability. The
stated rationale for transient-first ("contract-simple," and L1/C0 run in parallel) is real but does
not require *shipping* the transient before the cursor. Recommend leading with the cursor.

### F2 — The frozen tier is the highest-risk deliverable and enters its bake-off before any evidence it can win (Major; add a pre-gate)

`FrozenDictionary` earns its construction cost because its baseline, `ImmutableDictionary`, is a slow
AVL-of-hash-buckets. **This repository's baseline is not slow.** The shipped `PersistentHashMap`
already has bounded-depth (≤ 7 levels), **allocation-free** lookups
([overview](../../src/CSharp/docs/Hamt/overview.md)). So `FrozenHashMap` must beat a fast structure on
lookups purely through packed-array cache locality and a denser slot table — a real but *modest and
uncertain* win, and the one claim in the plan most likely to fail its own shipment gate.

The plan's L2 nevertheless commits to building three prototype layouts (Robin-Hood, linear/quadratic,
and a `FrozenDictionary` control) before any evidence that even the best packed layout beats the
persistent HAMT at all. Recommend a **cheap pre-gate before L2**: one throwaway micro-benchmark of a
packed-entry-array + slot-table lookup against `PersistentHashMap.TryGetValue` and
`FrozenDictionary` across the read-heavy datasets. If the packed layout does not clear a
maintainer-set margin over the shipped HAMT on positive *and* negative lookups, the frozen tier is
deferred wholesale rather than after three prototypes are built. This spends a day to avoid spending
the L2/L3 effort on a tier that cannot clear its gate.

### F3 — The cursor's amortized bound under *branched* cursor histories is not gated (Medium gap)

The plan makes the cursor "a persistent working version": old cursors remain valid and can be edited
to create branches. It correctly attributes the *tree's* amortized endpoint bounds to the finger
tree's memoized suspensions (the deque's persistence-robustness mechanism). But the cursor's **active
window and the two partial carries are cursor-local state outside the memoized tree.** When a cursor
is retained and two descendants each edit near the same position, the amortized O(1) local-edit
accounting can be re-paid per branch unless the window/carry copy is bounded (it is — bounded by the
focus cap) *and* the analysis accounts for it. This is the same class of question the finger-tree
deque had to answer for branching histories, and it is the cursor's analogue.

The validation suite tests branched cursors for *correctness* ("retained old cursors and branches
from stale-but-valid versions"), and the complexity table gives worst-case O(log n) on boundary
repair — but neither explicitly measures whether the *amortized* local-edit bound survives adversarial
branching (repeatedly branching a retained cursor and editing each branch at the boundary). Recommend
adding "branch a retained cursor and edit both at the focus/chunk boundary" as an explicit C0
counter and a stated bound (the expected answer is that bounded window copies keep it O(1) amortized,
but it must be measured, not assumed).

### F4 — State the transient's win regime, so the benchmark targets it (Minor)

A batch of N edits to a size-M map costs O(N·w) both as N persistent `SetItem`s and through the
transient. The transient's advantages are specifically: (a) no allocation of N intermediate immutable
wrappers, and (b) in-place re-editing of a path already owned this session (repeated edits to the
same or nearby paths avoid re-copying). It is a near-wash for N edits to N disjoint paths on a small
map. The plan's benchmark list covers "edit counts ranging from one to the full count" but does not
name the regime where the transient is expected to win versus where it is a wash. Stating that (few
edits to a large retained base; clustered/repeated edits; wrapper-allocation-bound loops) sharpens
the shipment-gate evidence and prevents a "no measurable win" result from an ill-targeted benchmark.

### F5 — Minor documentation and scope notes (Low)

- The frozen `CreateRange` canonicalizes to CHAMP trie order, not insertion order. This is consistent
  with the persistent map's unspecified-order contract, but because a *frozen* collection reads as
  "final and fixed," the enumeration-order contract should be stated on the public type, not only in
  the plan, so consumers do not assume insertion order.
- `Transient.Persist()` returning `source` by reference for a clean no-op chain is a nice property;
  it should be added to the semantic-contracts document's no-op-identity section when the surface
  ships, since it extends that contract to the lifecycle boundary.
- The plan is C#-only in scope and says so, but the frontier catalog's Axis 2 rows should carry a
  one-line "status: planned, C#-first, not shipped" marker so the survey and the plan cannot be read
  as a parity commitment (the plan states this; the catalog should mirror it).

## Recommendation

Adopt the plan's designs — the transient mechanics, the frozen layout, and the focused cursor gap
model are all sound and correctly gated. Change two things:

1. **Re-sequence to cursor-first.** Ship the consumer-backed, differentiated capability (the rope
   cursor) as the first Axis 2 deliverable; treat the transient and frozen tiers as parallel
   benchmark-gated performance tracks that ship when and if their gates clear.
2. **Add fail-fast pre-gates** for the two speculative tiers: a packed-vs-HAMT lookup micro-spike
   before the frozen bake-off (F2), and a stated transient win-regime for its benchmark (F4); and add
   the branched-cursor amortized measurement to C0 (F3).

The companion [cursor-first alternative](../proposals/axis2-cursor-first-alternative-2026-07-13.md)
works these into a concrete re-sequenced plan and argues the trade in full. It is a re-sequencing and
re-gating of this proposal's own designs, not a competing design — the original's engineering is
kept.

## Relationship to other documents

- [Axis 2 final lifecycle and sequence-cursor plan](../proposals/axis2-lifecycle-and-sequence-cursors.md) —
  the authoritative synthesis; this review examined its pinned earlier revision.
- [Axis 2 cursor-first historical alternative](../proposals/axis2-cursor-first-alternative-2026-07-13.md) —
  the review-time re-sequencing proposal incorporated with corrections by the final plan.
- [Frontier structure catalog](../reference/frontier-structure-catalog.md) — the Axis 2 survey rows
  the plan refines.
- [Next data structures proposal (2026-07-09)](../proposals/new-data-structures-2026-07-09.md) — item
  A3, whose API/complexity sketch the plan (correctly) supersedes.
- [Semantic contracts](../reference/semantic-contracts.md) — the no-op-identity, policy-preservation,
  and snapshot-isolation rules the planned surfaces inherit.
