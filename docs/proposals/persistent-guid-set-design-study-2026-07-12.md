# Design Study: A Persistent Set of GUIDs

- Status: Design study — no committed work; recommendation is consumer-gated
- Created (UTC): 2026-07-12T18:59:30Z
- Repository HEAD: 3301b14921ecc46f88451f0a5278fb33b939af3f
- Audience: Maintainers and consumers choosing or building a persistent set keyed by `System.Guid`
- Scope: Which shipped collection best serves a persistent set of GUIDs; whether a custom
  collection can do materially better; the design, trade-offs, and adoption economics of that custom
  collection; and a workload-indexed recommendation

## The question

Given a need for "a persistent (immutable, structurally shared) set of GUIDs," two questions follow:
which of the repository's shipped collections fits best, and can a purpose-built collection beat it?
This study answers both, grounding every claim in the C# sources (the semantic reference for all
ports) and settling the one factual point where a naive analysis goes wrong (GUID sort order) with a
measured probe.

The short answers:

- For the common case — an in-process set with single-element membership, add, and remove over
  trusted (randomly generated) GUIDs — `PersistentHashSet<Guid>` is the right default and needs no
  new code.
- A custom collection can do materially better, but as a **capability and robustness** tool, not as
  a general "faster membership set." Its decisive win is immunity to a hash-flooding
  denial-of-service that the default set is vulnerable to when GUIDs are attacker-influenced; its
  secondary wins are ordered/range iteration and structural (version-vs-version) set algebra. For
  trusted random GUIDs doing plain membership, the elegant form of the custom collection is *slower
  and heavier* than the default, and the form that restores parity is essentially the default's own
  engine re-keyed.

The rest of this document justifies those answers in detail.

## Baseline: `PersistentHashSet<Guid>`

`PersistentHashSet<T>` is a thin wrapper over `PersistentHashMap<T, Unit>` (the value is a zero-field
internal `Unit` struct), so its behavior is the CHAMP HAMT's. For `T = Guid` the relevant facts,
each verified against the source:

### What it does well

- **Unboxed inline storage.** `Guid` is a 16-byte value type, and the node payload is a generic
  value-type `Entry` struct with a `public readonly TKey Key` field held in typed `Entry[]` arrays
  ([`PersistentHashMap.cs:1302`](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.cs),
  `:1491`). Nothing is stored as `object`/`object[]`, so a GUID sits inline in the array with no
  per-element boxing (which would otherwise add an object header plus ~16–24 bytes each). The only
  per-node overhead beyond the key is the node header and a cached 4-byte hash.
- **Bounded, allocation-free reads.** The trie consumes 5 hash bits per level and branches exist
  only at shifts 0…30, so depth is capped at 7 levels (≈ 4 expected at a million keys). Lookups
  allocate nothing; updates clone only the search path and share every untouched subtree across
  versions.

For membership-dominated workloads on GUIDs, this is already adequate.

### Its three limitations for GUIDs

1. **The 128-bit key is collapsed to a 32-bit hash.** Slotting is driven by
   `unchecked((uint)comparer.GetHashCode(key))` — for the default comparer, `Guid.GetHashCode()`, a
   32-bit value ([`PersistentHashMap.cs:559`](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.cs)).
   Every lookup/add/remove computes it once. When two distinct GUIDs share the full 32-bit hash, the
   trie stores them in a `CollisionNode` whose `Entry[]` is scanned linearly and re-allocated (with
   an `Array.Copy`) on each insert ([`PersistentHashMap.cs:1419`](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.cs),
   `:1433`). For random GUIDs these buckets are birthday-bound and modest — see [the numbers](#the-numbers-that-matter)
   — but the 32-bit ceiling is intrinsic regardless of hash quality.
2. **Historical baseline: set algebra was element-wise, not structural.** `Union`, `Intersect`, `Except`, and
   `SymmetricExcept` all take `IEnumerable<T>` and operate element-by-element: `Union` folds
   `result.Add(item)`, `Except` folds `result.Remove(item)`, `Intersect` materializes a probe
   `HashSet<T>` and rebuilds ([`PersistentHashSet.cs:210`](../../src/CSharp/src/Durable7.Hamt/PersistentHashSet.cs)–`290`).
   Merging two large GUID sets that share history therefore costs O(m) re-hash-and-insert operations
   (or an O(n) rebuild), never work proportional to the non-shared structure. The *map* layer does
   have reference-equality-pruned `MapEquals`/`Diff` ([`PersistentHashMap.cs:441`](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.cs)–`455`,
   `:573`), but that is a change-enumeration over aligned nodes, not an algebraic combine, and it is
   not surfaced on the set at all.

   **Current-state correction (2026-07-14):** same-type structural CHAMP map/set algebra now ships
   across all six language workspaces, with reference-equal subtree pruning and stored-hash reuse.
   Arbitrary-`IEnumerable<T>` overloads remain element-wise. The historical limitation above no
   longer distinguishes a full-key GUID collection from the current same-type hash-set surface.
3. **Enumeration is bitmap order, not sorted.** There is no ordered iteration and no range or prefix
   query — the 32-bit hash destroys key locality.

## The custom idea: treat the GUID as its own key

The repository already ships the pattern that fixes limitations (1) and (2) for integers: the
Okasaki–Gill big-endian Patricia trie behind `PersistentIntSet`/`PersistentLongSet`, where the key
*is* the discriminator (no hash function) and `Union`/`Intersect`/`Except` are structural,
reference-pruned merges ([`PatriciaMapCore.cs:245`](../../src/CSharp/src/Durable7.Hamt/Internal/PatriciaMapCore.cs)–`318`).
A GUID is 128 bits of (for v4) high-quality entropy, so the same idea extends directly:

> **`PersistentGuidSet`** — a persistent set keyed by `System.Guid`, implemented as a big-endian
> radix trie over the 16-byte GUID treated as its own 128-bit key. No `GetHashCode`, no separate
> hash. Because two distinct GUIDs must differ within 128 bits, **there are never hash collisions and
> no collision-bucket code path exists at all.** It inherits Patricia's structural set algebra and
> sorted iteration for free.

There are two representations of this idea, and the distinction is load-bearing:

- **1-bit Patricia** (the textbook, "simple extension of the int/long core"): each internal node
  branches on a single discriminating bit. Path-compressed, so depth is ~log₂N *branch points* for N
  random keys, but a binary trie over N leaves always has exactly N−1 internal branch nodes.
- **Wide (5-bit) branching** over the 128-bit key: 32-way child arrays with an occupancy bitmap and
  popcount compaction — i.e. the CHAMP node layout, but keyed on the full 128 bits instead of a
  32-bit hash. Depth ~log₃₂N (≈ the baseline's), denser memory.

The key carrier is **native `System.UInt128`** (.NET 10+): it is a built-in value type (no
allocation), it is exactly 128 bits = one GUID, and it implements `IBinaryInteger<UInt128>`, so the
five bit primitives the trie needs (leading-zero count, shift, and, not, compare) come for free. The
repository's own `Durable7.Numerics` is the wrong choice here — it starts at `UInt256` (double width,
wasteful), is not `IBinaryInteger`-shaped for those helpers, and would introduce a new
`Hamt → Numerics` cross-project dependency. Two `ulong` limbs are a distant third, reintroducing the
manual carry/leading-zero/compare logic `UInt128` already provides.

## What the custom collection genuinely wins — and what it does not

All three of the adversarial design passes run for this study reached the same conclusion: the strong
pitch ("beats the baseline generally, ship 1-bit Patricia as the default") **does not hold**, but the
targeted wins are real and, in one case, decisive.

### The decisive win: immunity to hash-flooding

`Guid.GetHashCode()` is a pure XOR-fold of the four 32-bit lanes with **no avalanche**. An attacker
who can influence GUID values (accepting them from a client, deriving them from user input, etc.) can
therefore mint an unbounded family of distinct GUIDs — on the order of 2⁹⁶ of them — that all fold to
a single 32-bit hash: choose three lanes freely and set the fourth to `H ⊕ L0 ⊕ L1 ⊕ L2`. On the
baseline every such key lands in one `CollisionNode`; each insert scans the bucket linearly and
copies an `Entry[]` of growing length, so building k colliding keys is **O(k) per operation and O(k²)
overall** — a classic algorithmic-complexity denial of service, and it survives structural sharing
(each retained version keeps its own oversized bucket copy).

The full-key trie is immune by construction: distinct GUIDs always differ within 128 bits, so every
key gets its own leaf and no bucket path exists. It also gives a **hard, N-independent, adversary-proof
depth bound** — a discriminating bit strictly descends from most-significant to least-significant, so
any root-to-leaf path has at most 128 branches (1-bit) or 26 (5-bit) regardless of N or attacker
input. The hash set can offer no such bound; its bucket size is unbounded under adversarial load.

This is the single strongest reason to build the custom collection, and it applies only when GUIDs are
**untrusted**. For CSPRNG-random GUIDs it is a robustness/cleanliness improvement, not a performance
fix (see below).

### Real but conditional wins: ordered iteration and structural set algebra

- **Ordered iteration + range/prefix queries.** A big-endian-encoded full-key trie iterates in
  ascending GUID order and answers "all GUIDs under this byte prefix" — neither of which the hash set
  can do. The sort-order details are subtle enough to warrant [their own section](#sort-order-a-measured-fact-and-a-footgun).
  Time-ordered v7/COMB GUIDs are a *strength* here, not a weakness: their long shared timestamp
  prefix is path-compressed into a shallow shared spine (stored once, never as per-bit nodes), the
  random low bits keep subtrees balanced, and iteration emerges in time order. Monotonic insertion
  cannot degenerate the trie because shape depends on key bits, not insertion order.
- **Structural, reference-pruned set algebra.** For two GUID sets that share ancestry (derived
  versions), `Union`/`Intersect`/`Except` skip whole reference-equal subtrees, giving work
  proportional to the divergence rather than the set size. This is a clear asymptotic win over the
  baseline's element-wise O(m) algebra — **but only for shared-ancestry operands.** Two independently
  built sets share no nodes, so the merge degrades to a single-pass O(n + m) (still better than O(m)
  re-hash-insert, but not O(divergence)). And the frontier catalog's HAMT structural-diff work would
  give the hash set its own (unordered) structural set algebra, narrowing even this gap for
  order-agnostic workloads.

  **Current-state correction (2026-07-14):** that frontier work has shipped. Both designs now prune
  shared same-type subtrees; the full-key design's remaining differentiators are collision freedom,
  key order, range/prefix operations, and its different depth/memory trade rather than structural
  algebra alone.

### Where the custom collection loses

For **trusted random GUIDs doing plain membership**, the elegant 1-bit variant is worse than the
baseline on every axis that matters:

- **Latency.** Depth ~log₂N (≈ 20 pointer hops at 10⁶) versus the baseline's `min(⌈log₃₂N⌉, 7)` (≈ 4,
  hard-capped at 7) — roughly 5× more cache-missing hops on a heap-scattered persistent trie, with no
  offsetting saving (both perform the full 128-bit equality at the leaf).
- **Memory.** A binary Patricia over N leaves has exactly N−1 internal branch nodes — ~2 GC objects
  per element. A `Branch` carrying a `UInt128` prefix and mask is ~68–72 bytes, giving ~110 bytes per
  element versus the baseline's up-to-32-way packing (well under one branch per element amortized) —
  roughly 1.5–2× heavier, and a genuine memory wall at N = 10⁸ (~10⁸ branch nodes versus CHAMP's
  ~30× fewer). Version-retention (snapshot/undo) amplifies this: each update path-copies ~20 nodes
  versus ~4.
- **Build throughput.** ~20-node path-copies per insert versus ~4.

Restoring per-op parity requires the **5-bit variant** — which reintroduces exactly the bitmap +
popcount + compaction machinery the "simple Patricia" pitch claims to avoid. At that point the honest
description is not "a simple extension of the int/long core" but "CHAMP re-keyed on the full 128-bit
GUID." **Current-state correction (2026-07-14):** feeding that discriminator through the current
CHAMP is not a thin facade or unchanged-node reuse. The shipped node entries, routing helpers, bitmap
descent, collision representation, diagnostics, and every port carry a 32-bit hash/path contract; a
full-key version must deliberately widen that representation and revalidate the node layer.

## The numbers that matter

Birthday-bound collisions in the 32-bit hash space (2³² ≈ 4.29 × 10⁹), expected colliding pairs
≈ N² / 2³³:

| N (GUIDs) | ~Expected 32-bit hash collisions | Baseline impact (trusted input) |
| ---: | ---: | --- |
| 10⁴ | ~0.01 | none in practice |
| 65,536 | ~0.5 (≈ 50% chance of one) | first collisions appear |
| 10⁶ | ~116 two-element buckets (~0.01% of elements) | tiny, still O(1)-bucket, correct |
| 10⁷ | ~11,600 | small, grows quadratically |
| 10⁸ | ~1.16 million small buckets | present but each still tiny |

So for trusted random GUIDs the collision path is rare and always correct — the custom collection
**removes a rarely-exercised, already-correct code path and a dependency on hash quality; it does not
fix a performance cliff.** The cliff only appears under *adversarial* input, where the same table
becomes O(k²) build cost concentrated in one bucket.

Per-operation and per-element comparison (N random GUIDs):

| Axis | `PersistentHashSet<Guid>` | 1-bit Patricia GUID set | 5-bit / CHAMP-on-raw-key |
| --- | --- | --- | --- |
| Point op depth | `min(⌈log₃₂N⌉, 7)` (≈ 4, cap 7) | ~log₂N (≈ 20 at 10⁶) | ~log₃₂N (≈ 4–5) |
| Hash collisions | birthday-bound buckets | none, ever | none, ever |
| Adversarial worst case | O(k²) build (DoS) | hard O(128) bound | hard O(26) bound |
| Memory / element | dense (<1 branch/elt amortized) | ~110 B (~2 objects/elt) | ~parity with baseline |
| Set algebra | element-wise O(m) | structural O(divergence) / O(n+m) | structural (if implemented) |
| Ordered / range | no | yes (big-endian) | yes |

## Sort order: a measured fact, and a footgun

"Sorted iteration for free" is only useful if the encoding produces the order callers expect, and this
is the one place a naive implementation silently goes wrong. Measured on this workstation over
1,000,000 random GUID pairs (probe: `TryWriteBytes(bigEndian:…)` + unsigned `SequenceCompareTo` versus
`Guid.CompareTo`):

- **Big-endian RFC-4122 byte order == `Guid.CompareTo` / `SortedSet<Guid>` order: 0 mismatches** — and
  crucially, ~500,000 of those pairs differed in the high bit of the first field, so the comparison is
  **unsigned** (a signed first-field comparison would have flipped exactly those). On modern .NET the
  first three integer fields are compared as unsigned, so unsigned big-endian lexicographic order is
  the `Guid.CompareTo` order.
- **Raw little-endian in-memory byte order mismatches `Guid.CompareTo` ~50% of the time** (500,354 /
  1,000,000). Keying the trie on `Guid.ToByteArray()` (little-endian by default) or on the in-memory
  layout would therefore produce an arbitrary order matching neither `Guid.CompareTo` nor anything
  else useful.

Consequences for the design:

- The trie **must** encode big-endian RFC-4122 bytes (`Guid.TryWriteBytes(dst, bigEndian: true, …)`),
  which is exactly the order the repository's existing `guid-rfc4122-v1` Merkle codec already uses
  ([`MerkleEncoding.cs:156`](../../src/CSharp/src/Durable7.Hamt/MerkleEncoding.cs)).
- This equality is *runtime behavior*, not a language guarantee, and the encoding choice is an easy
  mistake — so any implementation must **pin the order with an explicit `Guid.CompareTo`-parity test**.
- Even correct RFC-4122 order is **not** SQL Server `uniqueidentifier` order (which compares a
  different field sequence). "Sorted iteration" claims must name the reference order to avoid a
  correctness-of-expectation bug.

## Alternatives already in the repository

The custom set is not the only lever. Two shipped collections own adjacent niches:

- **`CanonicalSortedSet<Guid>`** (zip-zip tree). For equality/fingerprint-heavy, mostly-read
  workloads — snapshot many GUID sets and repeatedly ask "did this set change?" or "are these two the
  same set?" — its memoized 64-bit `ContentHash` gives an **O(1) inequality reject**
  ([`CanonicalSortedSet.cs:49`](../../src/CSharp/src/Durable7.FingerTree/CanonicalSortedSet.cs),
  `:282`) and its history-independent canonical shape enables cheap dedup of equal sets. Two limits:
  the digest is a *non-cryptographic* 64-bit fingerprint (inequality is proven; equality still needs a
  structural walk) and is process-random by default, so it is a within-process accelerator, not a
  durable identity. Holding GUIDs requires supplying both a comparer and a rank-hash constant on the
  comparison-equivalence classes; equality/comparison are not O(1), only inequality is.
- **`MerkleSearchTree<Guid, TValue>`** (content-addressed). The right answer *only* when the set must
  cross a process, host, or storage boundary and needs at least one of: content-addressed set identity
  (compare or dedup whole sets by one 32-byte root digest without shipping elements), O(divergence)
  reconciliation between diverged replicas, authenticated membership/non-membership/range proofs, or a
  three-way merge of concurrently edited sets. It already ships the big-endian `guid-rfc4122-v1`
  codec. Its costs make it overkill for an in-process set: a SHA-256 and a canonical wire block per
  node on every update, codec/policy ceremony, and the fact that it is a *map* — there is no Merkle
  set, so a "set of GUIDs" must invent a unit value plus a value codec. When cross-process durable
  identity is the actual requirement, it is the only candidate that provides it (a collision-resistant
  256-bit digest); `CanonicalSortedSet`'s digest is neither cryptographic nor cross-process.

## Recommendation, indexed by workload

| Workload | Best fit | Why |
| --- | --- | --- |
| Trusted random GUIDs; membership/add/remove; any N, incl. 10⁸ | **`PersistentHashSet<Guid>`** (shipped) | Bounded depth, dense memory, unboxed; the custom trie loses on latency and memory here |
| **Untrusted / attacker-influenced** GUIDs | **Custom full-key GUID set** | Immune to the zero-avalanche `GetHashCode` hash-flooding DoS; hard O(128) depth bound |
| Ordered iteration, range/prefix, v7/COMB time-ordered streams | **Custom full-key GUID set** (big-endian) | Trie-only capability; iterates in `Guid.CompareTo` order |
| Union/intersect/diff of history-sharing large sets | **Custom full-key GUID set** | Structural O(divergence) vs element-wise O(m) |
| "Did this set change?" / fingerprint whole sets | **`CanonicalSortedSet<Guid>`** (shipped) | O(1) inequality via memoized digest |
| Cross-process sync / diff / merge / proofs / content identity | **`MerkleSearchTree<Guid, unit>`** (shipped) | Ships the RFC-4122 codec; O(divergence) block sync (supply a unit value codec) |

## Should the custom collection be built now?

Under the repository's own rules, **not speculatively.**

- **Consumer-driven bar.** There is no GUID-keyed consumer anywhere in the repository today — `Guid`
  appears only in the Merkle wire codec, its tests, and one API-spec doc. The frontier catalog's
  [key-type-specialized map construction](../reference/frontier-structure-catalog.md#key-type-specialized-map-construction)
  entry explicitly gates exactly this class of new radix core on "a consumer with real prefix-query or
  byte-ordered-key needs." Building it now inverts the Tungsten-established consumer-first rule.
- **This is not a thin facade.** `PatriciaMapCore` is hardwired to a 64-bit `ulong` path
  (`IPatriciaKey.Encode(TKey) → ulong`, [`PatriciaMapCore.cs:8`](../../src/CSharp/src/Durable7.Hamt/Internal/PatriciaMapCore.cs)),
  and the trie's `HighestBit`/`PrefixOf`/`Matches`/`GoesLeft`/`Join` helpers operate on `ulong`. A
  128-bit path is genuine new code, and a full family pays the priciest parity bill (~30 public
  members across up to six languages — the reason the proposal document rates the Patricia family its
  only Tier-C item).
- **The headline win is workload-specific.** Collision-freedom is decisive for untrusted input and
  merely tidy for trusted input; structural algebra fires only across shared-ancestry versions;
  ordered iteration matters only when order is needed. None of these is the plain-membership case.

The smallest changes that capture most of the value, in order of leverage:

1. **If GUIDs are untrusted** — this is the one case that justifies building now, because it closes a
   real algorithmic-DoS exposure in the baseline rather than optimizing a micro-benchmark. The
   smallest correct form is **CHAMP keyed on the full 128-bit GUID** (via `UInt128`), but it requires
   a widened path/hash representation throughout each language's CHAMP node layer rather than a thin
   facade over the shipped 32-bit implementation. It retains HAMT-class depth and memory goals while
   eliminating the equal-full-hash collision path.
2. **If ordered/range/structural-diff over GUIDs is the need** — generalize the existing Patricia
   core's path type **once** to `TPath : IBinaryInteger<TPath>` (making int, long, and 128-bit all
   instances of one core; behavior of the shipped int/long maps is unchanged) and add a thin `Guid`
   facade over `UInt128`. The trie algorithms are already width-agnostic, so the algorithmic risk is
   low; the blast radius is one interface plus one core class re-parameterized, the two existing
   policies and facades renamed to name `ulong` as `TPath`, and a small `GuidPatriciaKey` policy plus
   `PersistentGuidSet`/`PersistentGuidMap` facades.
3. **Otherwise** — keep `PersistentHashSet<Guid>` and treat this document as the decision record; a
   one-row entry in the frontier catalog's key-type-specialization section can point here.

**Prerequisite for any of the above:** a **GUID-keyed benchmark**, which does not exist today — every
current HAMT/Patricia benchmark uses sequential integer keys whose identity hash never exercises the
collision path and produces a shallow, dense trie unrepresentative of random GUIDs. That benchmark
(build throughput, membership latency to N = 10⁸, memory per element, and an adversarial-collision
build against the baseline) should land first, so the trade is measured rather than asserted.

## Relationship to other documents

- [Frontier structure catalog](../reference/frontier-structure-catalog.md) — the
  [key-type-specialized map construction](../reference/frontier-structure-catalog.md#key-type-specialized-map-construction)
  entry this study instantiates for GUIDs, and the CHAMP / Patricia / Merkle / canonical-set cores it
  draws on.
- [Next data structures proposal (2026-07-09)](new-data-structures-2026-07-09.md) — rates the Patricia
  family (Tier C1) the sole structurally-new family precisely because of the parity bill quantified
  here; a GUID set is a width-generalization of that family, not a new one.
- [Data structure catalog](../reference/data-structure-catalog.md) — the shipped `PersistentHashSet`,
  `CanonicalSortedSet`, and Merkle surfaces compared above.
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md) — the workflow any of the
  code options would follow if a consumer justifies them.
