# Axis 2, Cursor-First: Historical Alternative Sequencing And Gating

- Status: Historical alternative — incorporated with corrections into the authoritative final plan
- Created (UTC): 2026-07-13T04:01:36Z
- Repository HEAD: 4376db84b198ee5be7d3ee9dc62cb3e9c8b46149
- Audience: Maintainers studying the review-time Axis 2 sequencing rationale
- Scope: A historical alternative rollout of the same three Axis 2 deliverables (rope cursor, CHAMP transient,
  frozen hash tier) that re-sequences and re-gates them; it adopts the engineering of the
  [reviewed Axis 2 plan at `4376db8`](https://github.com/VladimirReshetnikov/DataStructures/blob/4376db84b198ee5be7d3ee9dc62cb3e9c8b46149/docs/proposals/axis2-lifecycle-and-sequence-cursors.md)
  and changes only the order of execution and the evidence gates

> **Current disposition (2026-07-13):** The
> [Axis 2 final plan](axis2-lifecycle-and-sequence-cursors.md) is authoritative. It accepts cursor
> priority, a frozen signal gate, and a proof obligation for branched cursor histories. It qualifies
> the Editor/Tour as future integration targets rather than measured current consumers, replaces the
> calendar-sized throwaway transient spike with T0 workload qualification plus a
> production-representative T1 kernel, corrects the transient win regime, and rejects canonical
> frozen enumeration order. The review-time argument below is preserved as rationale, not current
> execution instruction.

## What this is and is not

This is **not a competing design.** The transient owner-token mechanics, the frozen packed-CHAMP-order
layout, the Ctrie `SnapshotView.Freeze()` addition, and the focused rope cursor representation in the
[reviewed plan](https://github.com/VladimirReshetnikov/DataStructures/blob/4376db84b198ee5be7d3ee9dc62cb3e9c8b46149/docs/proposals/axis2-lifecycle-and-sequence-cursors.md)
are all sound, and this alternative keeps their mechanics unchanged. It changes three things:

1. **Order:** ship the rope cursor first, not fourth.
2. **Gates:** put a one-day fail-fast pre-gate in front of each of the two benchmark-contingent tiers
   (transient, frozen) so effort is committed only after the tier's central bet is shown to pay.
3. **One gap closed:** make the cursor's amortized-edit bound *under branched cursor histories* an
   explicit C0 measurement.

The claim is that this ordering delivers a shippable, differentiated, consumer-backed win sooner and
spends less effort on the two tiers most likely to fail their own gates — using the repository's own
stated rules as the decision criterion. The review that motivates it is
[axis2-lifecycle-and-cursors-review-2026-07-13](../reviews/axis2-lifecycle-and-cursors-review-2026-07-13.md).

## Why cursor-first is better — three arguments from the repository's own rules

### 1. Consumer-driven ordering (the repo's load-bearing rule) points at the cursor

The frontier catalog and the Tungsten case study established a consumer-driven bar: build the thing a
named consumer needs, not the thing another library happens to have. Applying that bar to the three
Axis 2 deliverables:

| Deliverable | Named in-repo consumer | Differentiation | Value depends on |
| --- | --- | --- | --- |
| **Rope cursor** | **Yes** — the Editor and Tour samples are text editors; localized insert/delete/move is their hot path | **High** — no mainstream .NET persistent library ships an editor-grade version-bound rope cursor | Beating indexed `Rope` edits on local histories (a gap the plan documents as large: `AddFirst` copies a boundary chunk every call) |
| CHAMP transient | No | Low — Clojure transients and `ImmutableDictionary.Builder` are well-trodden | A benchmark showing owner-token editing beats repeated persistent `SetItem`, net of a real retained-token memory cost |
| Frozen hash tier | No | Low — `FrozenDictionary` is the reference | A benchmark showing a packed layout beats an *already allocation-free, bounded-depth* HAMT lookup |

The original plan ships the two no-consumer, benchmark-contingent tiers first (L1 transient, then
L2/L3 frozen) and the consumer-backed capability (C1 cursor) fifth. This alternative inverts that: the
cursor leads. The repository prefers "substantial, production-ready work"; a shipped editor-grade
cursor with the Editor/Tour samples riding on it is a visible capability, where a transient perf tier
with no consumer is an internal optimization whose payoff is a benchmark result.

### 2. Fail-fast gating spends effort in proportion to certainty

Both performance tiers rest on a single empirical bet, and both bets are uncertain:

- The **frozen** bet is that a packed entry array plus a slot table beats the shipped HAMT's
  bounded-depth allocation-free lookup. Unlike `FrozenDictionary` (which beats the *slow*
  AVL `ImmutableDictionary`), this competitor is fast, so the win is modest and may not clear a
  shipment-worthy margin at all. The original commits to building **three** prototype layouts in L2
  before testing that bet.
- The **transient** bet is that owner-token in-place editing beats N persistent `SetItem`s by enough
  to justify a permanent per-edited-node token reference. It is a genuine win for few-edits-to-a-large-base
  and clustered/repeated edits, and close to a wash for N disjoint edits on a small map.

A one-day throwaway micro-spike settles each bet before the full surface is built. This alternative
makes those spikes hard pre-gates (G-F and G-T below). If a bet fails its spike, the tier is deferred
wholesale — not after its full L2/L3 or L1 surface exists. This is the benchmark-first rule taken one
step earlier: measure the central assumption before building the API around it.

### 3. Risk isolation: the cursor's risk is design, the tiers' risk is "no win"

The cursor's risk is representational (cursor-as-version vs focused-root) and is resolved by the C0
spike — a design question with a definite answer. The tiers' risk is that, after real implementation
effort, there is no measurable win to ship. Leading with the deliverable whose risk is *resolvable by
design work* rather than *contingent on an uncertain measurement* front-loads certainty: Axis 2
produces a shippable result first, and the uncertain tiers are proven-or-dropped cheaply alongside.

## The re-sequenced rollout

```text
P0 contract lock (shared)
│
├── TRACK C (lead): rope cursor
│     C0 cursor-as-version vs focused-root spike  ── adds: branched-cursor amortized measurement (F3)
│         └── C1 positional rope cursor  ── SHIP FIRST
│               ├── C2 measured/text cursor
│               │     └── C3 Editor + Tour integration
│               └── C4 deque cursor evaluation (consumer-gated)
│
├── TRACK T (parallel, gated): CHAMP transient
│     G-T pre-gate: owner-token batch edit vs N persistent SetItem micro-spike
│         └── [clears] L1 one-way CHAMP transient
│
└── TRACK F (parallel, gated): frozen hash tier
      G-F pre-gate: packed-array+slot-table lookup vs PersistentHashMap vs FrozenDictionary micro-spike
          └── [clears] L2 layout bake-off ── L3 frozen map/set ── L4 Ctrie SnapshotView.Freeze()
```

Recommended execution order:

1. **P0 contract lock** — identical to the original: benchmark skeletons plus executable
   contract-oracle tests for `PersistentHashMap`/`PersistentHashSet` and `Rope<T>`, and the catalog
   status markers.
2. **C0 spike** — the cursor-as-version vs focused-root prototypes, instrumented as the original
   specifies, **plus** the branched-cursor amortized-edit counter (below).
3. **C1 positional rope cursor** — the first shipped Axis 2 feature, validated by its dedicated
   command-model tests and `RopeCursorBenchmarks` against indexed `Rope` edits and a mutable
   gap-buffer baseline.
4. **G-F and G-T pre-gate spikes**, run in parallel with C0/C1 once the P0 contracts are stable. Each
   is a throwaway benchmark, not a public surface.
5. **C2/C3** — measured/text cursor and the Editor/Tour rewrite, delivering the consumer win
   end-to-end.
6. **L1 transient** *iff* G-T clears; **L2/L3/L4 frozen** *iff* G-F clears — each on its own timeline,
   with the original's shipment gates unchanged.
7. **C4 deque cursor, RRB transient, packed sorted frozen types** — consumer-gated, as in the
   original.
8. **Sibling-language promotion** — only after a C# compatibility round, as in the original.

The only structural change from the original's dependency graph is that Track C no longer waits behind
L2/L3, and the two performance tracks each acquire a one-day pre-gate. The three tracks still share
contract review and benchmark infrastructure and have no implementation dependency.

## The added pre-gates, precisely

**G-F (frozen pre-gate).** Build a *test-only* packed-entry-array + offline slot table (one layout,
Robin-Hood is fine) and benchmark `TryGetValue` against `PersistentHashMap` and `FrozenDictionary`
across the read-heavy datasets the original names (positive/negative/mixed hit ratios; random,
colliding, string, custom-comparer). Gate: the packed layout must beat the shipped HAMT by a
maintainer-set margin on *both* positive and negative lookups, *and* show a construction break-even at
a realistic read count. If it does not, Track F is deferred and the L2 three-way bake-off is never
built. Rationale: the entire frozen tier exists to win reads against a competitor the repository
already made fast; prove that before designing the public `FrozenHashMap` surface.

**G-T (transient pre-gate).** Build a *test-only* owner-token edit path (no public `Transient` type,
no invalidation surface) and benchmark a batch of K edits applied to a size-M persistent base against
K persistent `SetItem`s, sweeping (K, M) so the few-edits-large-base and clustered-repeated-edit
regimes are both present, and recording allocated bytes and retained bytes (including the token
overhead). Gate: a documented throughput/allocation win in at least one named regime that survives the
retained-token cost. If it does not clear, the transient is deferred; the shipped `BulkBuilder`
remains the construction path. Rationale: the transient's cost (a permanent per-edited-node token
reference) is certain; its win is regime-specific — measure the win before committing the public
one-way-transient surface and its invalidation contract across six eventual ports.

Both spikes reuse the P0 benchmark skeletons and the contract-oracle tests, so they are cheap and
their results feed directly into the original's L1/L2/L3 designs unchanged if the gate clears.

## The one design addition: branched-cursor amortization (C0)

The original makes the cursor a persistent working version (old cursors stay valid and branchable) and
correctly attributes the *tree's* amortized endpoint bounds to the finger tree's memoized spine. But
the cursor's active window and its two partial carries are cursor-local state outside the memoized
tree. The C0 spike must therefore add one measurement: **repeatedly branch a retained cursor and edit
each branch at the focus/chunk boundary**, and confirm the local-edit cost stays O(1) amortized rather
than re-paying boundary repair per branch. The expected result is that bounded (focus-cap) window/carry
copies keep it amortized-O(1), matching the deque's persistence-robustness story — but it is the
cursor's analogue of the question the deque had to answer, and it must be a measured C0 counter and a
stated bound, not an assumption. Everything else in Track C is adopted from the original unchanged.

## Where the original's ordering has merit (honest counter-arguments)

- **The transient is contract-simpler than the cursor.** Its one-way publish/invalidate contract is
  smaller than the cursor's gap semantics + representation spike, so "transient first" front-loads an
  easy, clean lifecycle deliverable. The counter: contract-simplicity is not the repo's prioritization
  axis — consumer value and differentiation are — and the transient's simplicity does not offset its
  lack of a consumer or its uncertain win.
- **L1 and C0 already run in parallel in the original.** True, but the original still *ships* C1 after
  L2/L3; parallel design does not change ship order. This alternative changes ship order, which is the
  point.
- **Frozen and transient share the P0 CHAMP contract lock, so doing them first amortizes that work.**
  True, but P0 is shared by all three tracks in both plans, so it is amortized either way; leading with
  the cursor loses none of it.

These are real, and they are why this is a re-sequencing rather than a rejection. If a maintainer
weights contract-simplicity and lifecycle-completeness over consumer-first delivery, the original order
is defensible. This alternative weights consumer-backed differentiation and fail-fast risk retirement,
which are the criteria the rest of the repository's roadmap already uses.

## What is unchanged from the original

Adopted verbatim: the builder/transient/persistent/frozen/snapshot-view vocabulary; the lifecycle
state machine and its asymmetry; the L1 owner-token mechanics, prepare/commit failure atomicity, and
semantic-contract preservation; the L2 layout candidates and L3 `FrozenHashMap`/`FrozenHashSet` surface;
L4 `SnapshotView.Freeze()`; the cursor gap semantics, provisional `RopeCursor<T>` surface, internal
focused-cursor carry/flush model, honest complexity table, and C2 measured/text cursor with its
line/column caveat; every shipment gate; the cross-language posture; and the explicitly-postponed
list with its re-entry rule. The alternative changes sequencing and adds gates; it does not touch
the designs.

## References

- [Axis 2 final lifecycle and sequence-cursor plan](axis2-lifecycle-and-sequence-cursors.md) — the
  authoritative synthesis that incorporates and corrects this alternative.
- [Review of the Axis 2 plan](../reviews/axis2-lifecycle-and-cursors-review-2026-07-13.md) — the
  findings (F1 sequencing, F2 frozen risk, F3 branched-cursor gap, F4 transient win regime) this
  alternative operationalizes.
- [Frontier structure catalog](../reference/frontier-structure-catalog.md) — the Axis 2 survey rows and
  the consumer-driven re-entry rule.
- [Next data structures proposal (2026-07-09)](new-data-structures-2026-07-09.md) — item A3, superseded
  by the original plan and inherited here.
