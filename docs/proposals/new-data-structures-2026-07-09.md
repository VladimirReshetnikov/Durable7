# Proposal: Next Data Structures for the Repository

- Status: Historical, partially realized proposal — not the current execution schedule
- Created (UTC): 2026-07-09T00:00:00Z
- Repository HEAD: 7ccfa24e0b6444de950aef69c7b2bb485bab41f2
- Audience: Maintainers studying the 2026-07-09 slate and its later disposition
- Scope: A prioritized slate of new structures and API additions, building on the
  [derived-structure catalog](../reference/derived-structure-catalog.md) and on gaps observed
  during the 2026-07-09 [C#/Rust implementation review](../reviews/csharp-rust-implementation-review-2026-07-09.md)

> **Current disposition:** A2's structural equality/diff work and C1's Patricia family have shipped.
> The [Axis 2 final plan](axis2-lifecycle-and-sequence-cursors.md) supersedes A3's cursor sequencing
> and sample integration. Consult the current derived/frontier catalogs before reactivating any
> remaining item; the body below preserves proposal-time rationale and order. B2's proposed
> Tungsten-backed ordered set is specifically superseded by the
> [revised benchmark-independent proposal](benchmark-independent-next-structures-2026-07-14.md) and
> the normative [Tungsten application-leaf boundary](../reference/tungsten-application-leaf-boundary.md):
> a general ordered set must be an independent project, implementation, contract, and test suite.

## Framing

The repository ships four families (fixed-width/sparse numerics, HAMT map/set, the FingerTree
family, Tungsten list/association) across up to six languages. The
[derived-structure catalog](../reference/derived-structure-catalog.md) already establishes which
compositions are sound and which enabling API gaps recur. This proposal originally treated the
application-specific `PersistentAssociation` shipment as shipment of the catalog's general
`PersistentOrderedMap` candidate. Current policy corrects that classification: Association is a
Tungsten-owned realization and design case study; the general ordered-map candidate remains
unshipped and requires an independent owner and contract. The proposal-time slate below remains
ordered by leverage per unit of parity cost. Effort estimates use velocity-independent units: new
public API members, workspaces touched, and test surface.

The catalog's own economics rule applies throughout: a thin facade is cheaper to ship as an API
addition plus a sample than as a six-language family. The slate below is therefore split into
(A) API additions on existing families, (B) thin facades with real payoff, (C) one structurally
new family, and (D) numerics extensions. Within each tier, items are ordered by recommendation.

## Tier A — API additions on existing families (highest leverage per line)

### A1. HAMT `Update` / `GetOrAdd` and transient builder

> **Current-state terminology note (2026-07-12):** the shipped HAMT bulk builders are staging
> builders, not owner-token transients. The genuine transient -> persistent -> frozen lifecycle is
> specified separately by the
> [Axis 2 final lifecycle plan](axis2-lifecycle-and-sequence-cursors.md); this historical item must not be
> used to infer O(1) persistent-root adoption or publication.

Already identified as the two top recurring gaps in the catalog. `Update(key, func)` halves every
read-modify-write (one trie walk instead of two); the builder removes the `O(n · update)`
per-insert path allocation from every `CreateRange` in the repository, including the Tungsten
association's construction (the largest constant-factor loss found in the Tungsten case study).
Both are prerequisites for the Tier B facades below — the hash bag's increment is exactly
`Update`, and every facade constructor wants the builder.

- Surface: ~4 members on `PersistentHashMap`/`PersistentHashSet` (`Update`, `GetOrAdd`,
  `ToBuilder`/`CreateBuilder` + builder type), mirrored in six workspaces.
- Precedent: the rope and sorted-collection builders already establish the builder idiom;
  the C# HAMT enumerator establishes the internal node traversal needed.
- Risk: low. Semantics are forced by the existing no-op-identity and comparer-preservation
  contracts.

### A2. HAMT structural diff / equality / set-vs-set algebra (phased)

The catalog's verdict stands: this is the single highest-leverage addition and the one candidate
that cannot be built by composition (the node layer is internal). The 2026-07-09 review adds a
concrete new motivation: the C# `Except`/`SymmetricExcept` already fold element-wise removals, and
the Rust port was just fixed to match — but a reference-equality-pruned lockstep node traversal
would make version-vs-version set algebra `O(divergence)` instead of `O(m)`, and would give
`MapEquals` a fast path the new Rust content-equality impls (added in commit `6a9970f`) currently
lack.

- Phase 1: `MapEquals` + a `Diff(other)` enumerator yielding added/removed/changed entries.
- Phase 2: structural `Union`/`Intersect`/`Except` between two maps/sets sharing a comparer.
- Phase 3: 3-way `Merge` with an explicit conflict matrix (defer until a consumer exists).
- Surface: ~3 members per phase; node-layer work in each port. This is the priciest Tier A item
  and the only one where the C port's refcounted nodes need real design (diff must not retain
  both versions' spines).

### A3. Focused FingerTree cursor over the measured tree

> **Final design disposition (2026-07-13):** the
> [Axis 2 final cursor plan](axis2-lifecycle-and-sequence-cursors.md) is authoritative. A path stack alone
> cannot both return a canonical immutable rope after every edit and preserve an amortized O(1)
> local-edit claim: rebuilding the root is O(log n). C0 therefore begins with one minimally complete
> cursor-as-version representation, uses gap positions, proves the applicable history class, and
> gives dirty snapshot costs explicitly. A focused-root spike occurs only if
> snapshot-after-every-edit is a predeclared required workload and canonicalization is the measured
> blocker; C0 may also defer the public cursor.

Repeated local edits via `Split`/`Concat` pay `O(log n)` each. A focused finger-tree cursor retains
locality, but the working focused cursor representation and a canonical `Rope<T>` are different
representations: nearby movement and focus edits can be O(1) amortized while rebuilding the rope
spine remains O(log n). The Axis 2
baseline makes that rebuild an explicit `Snapshot()` boundary; a lazily normalized focused-root
variant inside `Rope<T>` is only the conditional escalation described above.

- Surface: C# `RopeCursor<T>` first, with gap movement/editing and explicit snapshot; measured/text
  cursor second. Deque and raw-FingerTree surfaces are consumer-gated.
- Staging: C# first, with Editor/Tour as future measured-text integration targets rather than current
  localized-edit consumers; port only after the positional and measured-text gates clear and the API
  and proved complexity scope stabilize.
- Risk: medium-high — the representation choice affects allocation, snapshot cost, cached
  normalization, structural sharing, and every ordinary Rope operation if focused roots are used.

### A4. Small parity completions (fold into ongoing port maintenance)

From the 2026-07-09 review's deferred list, these are API-addition-sized and should ride along
with normal parity work rather than be scheduled as projects: Rust sorted-deque
`insert_sorted`/`split_at_sorted_*`, Rust `ReversibleDeque` iteration and reversible-typed
results, Rust `MeasuredRope` positional editing and builder, rope sorted-search signposts
(catalog gap), and the C sorted-map floor/ceiling parity item.

## Tier B — thin facades worth shipping as families

### B1. `PersistentHashBag<T>` (hash multiset)

Catalog verdict Strong; unchanged. Facade over HAMT `T -> int` with a cached `long` total count.
Rounds out the bag/set symmetry that already exists on the sorted side. Depends on A1 for
efficient increments (otherwise every `Add` is two walks).

- Surface: ~14 members (mirroring `PersistentHashSet` plus `CountOf`, `AddCopies`,
  `RemoveCopies`, `TotalCount`, distinct-vs-expanded enumeration).

### B2. Insertion-ordered persistent set (`PersistentOrderedSet<T>` / Tungsten set)

> **Superseded ownership and design (2026-07-14):** the proposal-time text below is retained as
> history, but its “same machinery” and Association-ordering recommendation must not be implemented
> as a Tungsten wrapper or semantic dependency. The current design forks useful sparse-label and
> dual-index mechanics into `Durable7.Ordered`, selects set behavior independently, and
> uses an independent model rather than Association as a live oracle.

New since the catalog. The insertion-ordered *map* shipped as `PersistentAssociation`; the set
counterpart is the same stamp-sequence + HAMT machinery with the value side erased — exactly the
relationship `PersistentHashSet` bears to `PersistentHashMap`, both of which are precedent. Every
workspace already contains 100% of the required substrate, so this is the cheapest possible new
family, and it is what "ordered set" means in most practical settings (Python dict-era ordering,
JS `Set`).

- Surface: ~16 members (`Append`/`Prepend`/`Insert`, positional reads, slicing, `Reverse`,
  set-style membership and algebra following the association's ordering rules).
- Decide up front: does set algebra follow association rule semantics (first occurrence keeps
  position) — recommended, and it falls out of `SetItems` — or C# `PersistentHashSet` semantics?

### B3. `PersistentBiMap<TKey, TValue>`

Catalog verdict Strong; unchanged. Two HAMTs behind a bijection-enforcing facade. Ship after A1
(the inverse-side no-op check needs `TryGetKey`, which exists, but updates want `Update`).

### B4. Interval map (`IntervalTree` with values) and `AddressablePriorityQueue`

Both are modest generalizations of shipped facades: the interval map is the existing interval
tree with a value payload (mostly API work; the 2026-07-09 review just locked down the
equal-low-endpoint tie semantics across ports, which the value-carrying variant inherits); the
addressable priority queue is the catalog's Plausible composition (HAMT handle index + sorted
tree) and covers the delete-by-handle timer niche. Schedule opportunistically after B1/B2.

## Tier C — one structurally new family

### C1. Big-endian Patricia trie (`PersistentIntMap<TValue>` / `PersistentIntSet`)

New since the catalog. The canonical Okasaki–Gill structure is the HAMT's ordered sibling:
integer keys, same bit-twiddling flavor, but sorted enumeration, `O(min(n, m))`-flavored
merge/intersection that degrades gracefully to `O(n + m)` worst case, and prefix/range queries.
Merge-heavy integer-keyed workloads beat both the HAMT (unordered, per-key merge) and the sorted
map (comparison-based) with it. It also passes the catalog's load-bearing test in the direction
the HAMT fails: dense/structured integer keys are exactly where the HAMT is least differentiated.

- Surface: a full map/set pair (~30 members) in six languages — a genuine family with the full
  parity bill. That cost is why it is the *only* Tier C item; RRB vector was considered and
  rejected for now (the catenable deque already covers push/pop/concat/split, and indexing is
  `O(log n)` there too; RRB's win is constant factors, which the repository's design notes treat
  as a benchmark-first question).
- Staging: C# reference + property tests against `SortedDictionary`/BCL models first; ports only
  after the merge API is proven against a real consumer.

Radix/prefix string tries were considered and deferred: without a concrete prefix-query consumer
they fail the "consumer-driven" bar the Tungsten case study set, and the text-rope +
sorted-map combination answers most prefix-range needs today.

## Tier D — numerics extensions

### D1. `BigRational` over the existing integers

Exact rational arithmetic composed from the shipped fixed-width/`BigInteger`-interop layer:
normalized `p/q` with gcd reduction, operators, comparisons, and `SparseInteger` interop where
exponent towers appear. The 2026-07-09 review's SparseInteger fix (and its new
`BigInteger`-model fuzz) establishes the testing pattern this type should be born with:
randomized model tests against `BigInteger`-based rationals from the first commit.

### D2. Modular / Montgomery arithmetic for the fixed widths

`ModArith<UIntN>` value type: modular add/sub/mul/pow/inverse with an optional Montgomery form
for repeated same-modulus work. Number-theoretic workloads (which `SparseInteger`'s A002845
provenance suggests) get the most value per line here.

### D3. Numerics parity ports

The review recorded that Numerics exists only in C# — the largest cross-language surface gap in
the repository. A Rust port of `UInt256/512/1024` (+signed) is the natural first target (Rust has
`u128` building blocks and the declaration-parity guardrail translates directly). Recommended
*after* the C# surface questions from the review's deferred list are settled (generic-math
interfaces, `MinValue / -1` policy, hex/format surface), so ports don't inherit unsettled
contracts.

## Historical recommended order

> **Current sequencing note (2026-07-13):** This list records the proposal's original slate. The
> [Axis 2 final plan](axis2-lifecycle-and-sequence-cursors.md) now owns A3's C#
> select/escalate/defer sequence and its later Editor/Tour integration; the slot below is not an
> unconditional cursor or sample commitment.

1. **A1** (HAMT `Update`/`GetOrAdd` + builder) — unblocks B1/B3 and speeds every workspace.
2. **B2** (insertion-ordered set) — cheapest new family; all substrate shipped; high everyday value.
3. **B1** (hash bag) — small, symmetric, immediately useful.
4. **A2 phase 1** (HAMT `MapEquals` + `Diff`) — highest-leverage core work; phases 2–3 follow demand.
5. **A3** (focused cursor) + Editor sample rewrite — historical C#-first slot, superseded by Axis 2.
6. **C1** (Patricia trie family) — the one big new structure; C#-first with a consumer.
7. **D1/D2** (BigRational, modular arithmetic) — as numerics interest dictates.
8. **B3/B4, D3** — opportunistic.

Items A4 (parity completions) ride along with ongoing maintenance and need no scheduling slot.

## Relationship to other documents

- [Derived-structure catalog](../reference/derived-structure-catalog.md) — the verified candidate
  space this proposal selects from; verdicts there are not repeated here.
- [Frontier structure catalog](../reference/frontier-structure-catalog.md) (added 2026-07-11) —
  surveys the candidate space *beyond* composition (new cores, hybrid representation tiers, niche
  specializations). Patricia and RRB now follow their current-state catalog entries; the Axis 2 final
  plan explicitly supersedes this proposal's A3 focused-cursor sequencing and sample-integration slot.
- [C#/Rust implementation review (2026-07-09)](../reviews/csharp-rust-implementation-review-2026-07-09.md)
  — source of the parity-gap and testing-pattern observations cited above.
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md) — the bill every shipped
  family pays; Tier B/C staging assumes C#-first with ports following a stabilized reference.
