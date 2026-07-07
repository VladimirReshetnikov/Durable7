# Wolfram Collections Overview

- Created (UTC): 2026-07-07T15:05:40Z
- Repository HEAD: 754f2e474caf2419bfabd5f88565341ddadbf449
- Audience: Maintainers, consumers, and porters of the C# Wolfram-collections library
- Scope: Purpose, composition, semantics, and design decisions for `Tools.DataStructures.Wolfram`

## What This Library Is

`Tools.DataStructures.Wolfram` ships two immutable, persistent collections whose operation
surfaces and ordering semantics match Wolfram Language `List` and `Association`:

- `PersistentList<T>` - an ordered sequence facade over the FingerTree family's
  `FingerTreeDeque<T>`, exposing the Wolfram `List` operation vocabulary (`Append`, `Prepend`,
  `Insert`, `Delete`/`RemoveAt`, `ReplacePart`/`SetItem`, `Take`, `Drop`, `Join`, `Reverse`,
  `Map`, ...) with .NET zero-based indexing.
- `PersistentAssociation<TKey, TValue>` - an insertion-ordered persistent map giving first-class
  keyed *and* positional access, with the kernel-verified Wolfram `Association` ordering rules.

Both are persistent: every mutating-style operation returns a new version that shares structure
with its source, and all retained versions stay valid under branching histories. Both give a
reference-equality "nothing changed" signal for observably no-op writes, mirroring the
substrates' no-op identity contract.

The primary external client is the Tungsten engine (`C:\Smithereens\src\Tungsten`), a kernel-free
Wolfram Language automation workspace currently written in Python; the design imagines its
expression layer as a typed host application. The library itself is client-agnostic: nothing in
the API depends on Wolfram or Tungsten types, and `TValue` can carry engine-specific payloads
(for example a `delayed` flag distinguishing `->` from `:>` entries, which is deliberately an
engine concern rather than a library field).

## Design Provenance

The design instantiates the adversarially verified composition recorded in the repository's
[derived structure catalog](../../../../docs/reference/derived-structure-catalog.md) (the
`PersistentOrderedMap` pattern and its Tungsten consumer case study), and the target semantics
are the kernel-verified Wolfram behaviors recorded in the Tungsten design study
(`src/Tungsten/docs/reports/2026-07-03-list-association-persistent-backends.md` in the
Smithereens repository; verified there against Wolfram Engine 14.3 and re-verified against
Wolfram 15.0 on 2026-07-07). The load-bearing composition rules:

1. **Stable-stamp discipline.** The association never stores positions or ranks in the hash
   side. Entries carry gapped monotone stamp labels; the stamp is the rendezvous key between the
   hash index and the order sequence.
2. **Values in both structures.** Wolfram associations read `Keys`/`Values`/`Normal` and
   positional parts as hot paths, so values live in both the HAMT and the finger tree: keyed
   lookup is an allocation-free hash probe, ordered enumeration streams off the tree without any
   hashing, at the price of one extra reference per entry and updates touching both structures.
3. **Honest amortization.** Gap-exhaustion relabeling and end-append label consumption are
   documented per produced version; branching from a pre-relabel version can re-pay the relabel.

## Composition

```text
PersistentList<T>
└── FingerTreeDeque<T>                     (FingerTree family)

PersistentAssociation<TKey, TValue>
├── PersistentHashMap<TKey, (stamp, value)>        (HAMT family: keyed index)
└── FingerTreeDeque<(stamp, key, value)>           (FingerTree family: association order,
                                                    sorted by strictly ascending stamp)
```

Association stamps are gapped order-maintenance labels with gap `G = 2^20`: appends take
`last + G`, prepends `first - G`, positional inserts the midpoint of their neighbors' labels.
When a midpoint no longer exists (at least 20 consecutive same-point inserts after a fresh
labeling), the association relabels wholesale in `O(n (w + log n))`. Because the entry sequence
is stamp-sorted, any key's position is recovered from its stamp by the deque's sorted-search
signposts in `O(log n)` - this is how keyed removal, in-place update, and `IndexOfKey` avoid
linear scans.

## Wolfram Semantics Guaranteed

The kernel-verified ordering rules implemented and test-locked (zero-based indexes here,
one-based in Wolfram):

| # | Rule | API |
| --- | --- | --- |
| 1 | Duplicate keys at construction keep the first occurrence's position with the last value | `CreateRange`, `SetItems` |
| 2 | `Append`/`Prepend` on an existing key remove the old entry and re-add at the end/front | `Append`, `Prepend` |
| 3 | `AssociateTo`-style writes update in place, keeping the key's position; new keys append | `SetItem` |
| 4 | Positional operations act on association order | `GetAt`, `Take`, `Drop`, `GetRange`, `RemoveAt`, `RemoveFirst`, `RemoveLast`, `Reverse` |
| 5 | `Insert` of an existing key wins position and value; the position is interpreted before the old occurrence is removed | `Insert` |
| 6 | `Join` keeps the first operand's positions with the second operand's values; new keys append in second-operand order | `Join`, `SetItems` |
| 7 | `Sort` orders by values, `KeySort` by keys, both stable; sorted results are ordinary associations | `Sort`, `KeySort` |
| 8 | `KeyTake` follows the requested key order and skips absents; `KeyDrop` preserves association order | `KeyTake`, `RemoveRange` |

Absence semantics differ deliberately: Wolfram returns `Missing["KeyAbsent", k]` where this
library throws `KeyNotFoundException` from the indexer or returns `false` from `TryGetValue`.
Mapping absence to an engine's missing value is the client's job.

## Decisions And Trade-Offs

- **`Reverse` is O(n), not O(1).** The design study proposed a wrapper reversal bit. For the
  association it inverts the stamp-ascending invariant that keyed position lookup relies on; for
  the list it taxes every operation with direction dispatch. Both types instead rebuild, matching
  the reference Wolfram cost; a client that reverses on a hot path can keep its own direction
  flag.
- **No small-array or packed tiers.** The study's SmallList/PackedList tiers are engine-level
  representation switches behind the client's own abstract expression surface (they must stay
  unobservable through `SameQ`/`FullForm`, which only the engine can guarantee). The library
  ships the single persistent representation those tiers promote into.
- **Stored-key retention follows the HAMT.** `SetItem` on an existing key keeps the originally
  stored key instance; `Append`/`Prepend`/`Insert` re-add and therefore store the supplied
  instance. `TryGetKey` recovers the stored instance.
- **Key/value equality policy.** Key equality is the association's factory-supplied
  `IEqualityComparer<TKey>` (default-comparer instances collapse to the shared `Empty`). The
  no-op value check uses `EqualityComparer<TValue>.Default`, inheriting the HAMT family's
  documented value-comparer gap.

## Porting Notes

This C# implementation is the reference for ports to the repository's other language workspaces
(C, C++, Haskell, Kotlin, Rust), which all already ship the two substrate families. A port needs:
the substrate deque with sorted-search on a stamp comparer, the substrate hash map with
tuple-like slot values, the gapped-label algorithm (`TryPickStamp`/relabel), and the ordering
rules table above as its fidelity spec. The C# test suite's example tests encode the
kernel-verified cases and are designed to be transcribed.
