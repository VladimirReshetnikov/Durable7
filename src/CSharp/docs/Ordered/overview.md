# C# Persistent Ordered Collections Overview

- Status: Current overview
- Created (UTC): 2026-07-15T01:28:46Z
- Repository HEAD: 5fd1a85c5ec58886f0dbabe805552bd37ec40871
- Audience: Users choosing the collection and maintainers reviewing its architecture
- Scope: `PersistentOrderedMap<TKey, TValue>`, `PersistentOrderedSet<T>`, and `PersistentOrderedMultimap<TKey, TValue>`

## Ordered Map

`PersistentOrderedMap<TKey, TValue>` combines comparer-defined keyed lookup with insertion and
explicit-position order. Each logical entry is one immutable object referenced by a CHAMP key index
and a stamp-ordered finger-tree deque. This keeps keyed reads on the hash path and lets positional
reads and enumeration return keys and values without hashing or duplicating the raw payload.

The key and value equality policies are independent. An existing-key `SetItem` retains the stored
key representative and position; a value equal under `ValueComparer` returns the receiver. `Add`,
`AddFirst`, and `Insert` are strict about duplicate keys, while `MoveToFirst`, `MoveToLast`, and
`MoveTo` are the only ordering mutations. Ranges, reversal, removal by key or position, and retained
branching versions follow the same sparse-label and failure-atomic publication discipline as the set.

## Ordered Multimap

`PersistentOrderedMultimap<TKey, TValue>` composes an ordered map of ordered sets. It retains the
first-insertion order of nonempty key groups and a separate first-insertion order for distinct
values within each group. Pair enumeration is deliberately key-grouped, not one globally
interleaved arrival history. Empty groups are contracted, duplicate pairs preserve receiver
identity, and independent key/value comparers and representatives are retained. The complete
contract is [specified separately](persistent-ordered-multimap.md).

## What The Type Provides

`PersistentOrderedSet<T>` combines set membership with a durable, explicitly controllable order:

- equality and hashing decide whether a class is present;
- the first representative of each equality class is retained;
- construction order fixes the initial sequence;
- ordinary additions append, or prepend/insert only when explicitly requested;
- adding an existing equality class is a reference-identity no-op and never moves it;
- movement has separate, unmistakable operations;
- positional lookup, removal, ranges, reversal, and stable one-shot sorting operate on the sequence;
- algebra preserves deterministic receiver/argument order under the receiver's equality policy; and
- every result is an immutable snapshot safe for concurrent readers.

“Ordered” means insertion and explicit-position order. It does not mean comparison-sorted order.
There is no `Min`, `Max`, value lower-bound, or persistent sortedness promise. `Sort` reorders once;
subsequent additions append normally.

## Independent Ownership

The project depends only on public general-purpose foundations:

```text
Tools.DataStructures.Ordered
├── Tools.DataStructures.Hamt
└── Tools.DataStructures.FingerTree
```

It does not reference `Tools.DataStructures.Tungsten`, link a Tungsten source file, consume Tungsten
internals, or use `PersistentAssociation` as a live oracle. Sparse-label order maintenance is an
independent Ordered implementation with its own set-specific contract, invariants, tests, and
evolution policy. The exact label stride and relabel cadence are private implementation details.

## Representation

Each ordered-set version owns two persistent indexes:

```csharp
FingerTreeDeque<Entry> _order;       // Entry = (strictly ascending stamp, representative)
PersistentHashMap<T, long> _stamps;  // representative equivalence class -> stamp
```

The CHAMP map answers membership, stored-representative, and stamp queries. The finger-tree deque
owns enumeration order and the actual ordered representative sequence. A private stamp comparer
ignores the representative and orders entries only by their unique `long` labels, which makes a
deque lower-bound locate a map-provided stamp without hashing or comparing elements again.

Insertion chooses a stamp at an end or strictly between neighboring stamps. When no interior integer
label remains, one unpublished rebuild assigns fresh sparse labels to the complete result and
bulk-builds both indexes. The public behavior does not expose how many repeated same-position edits
fit between relabels.

## Owned Invariants

Every published version maintains:

1. `_order.Count == _stamps.Count`.
2. Exactly one representative exists for every equality-comparer equivalence class.
3. Stamps strictly ascend in `_order`.
4. Every ordered entry has exactly one map entry carrying the same stamp.
5. Every map entry has exactly one ordered entry.
6. The map key and ordered item retain the same representative object for reference types.
7. Every derived version retains the receiver's equality-comparer object.
8. Earlier versions remain immutable and safe for concurrent reads.

Ordered-owned diagnostic code recomputes both directions of this invariant using only public HAMT
and FingerTree operations. The foundations do not grant Ordered friend access.

## Representative And Null Semantics

The supplied `IEqualityComparer<T>` defines hashing, equality classes, and therefore duplicate
collapse. The first encountered representative wins and is never implicitly replaced. This rule
applies to `CreateRange`, ordinary additions, comparer-normalized algebra arguments, movement,
ranges, reversal, and sorting.

There is no `where T : notnull` constraint. A comparer may define null as an ordinary equivalence
class. `TryGetValue(lookup, out actual)` returns the stored representative on a hit and echoes the
lookup value on a miss, following the HAMT set convention.

## Identity And Failure

Logical no-ops return the receiver instance: duplicate adds/inserts, movement to the current
position, absent removal, empty clear, full-range selection, unchanged reversal/sort cases, and
algebra that leaves the ordered representative sequence unchanged.

All structures are immutable. Equality-comparer, ordering-comparer, enumerable, argument-validation,
or allocation failures can abandon unpublished intermediate arrays or substrate versions, but cannot
change any published input. Positional arguments are validated before membership callbacks when the
operation's contract requires eager validation.

## Complexity Summary

Let `n` be the receiver/result size, `m` the number of argument inputs, `w` the bounded CHAMP depth,
and `c` an equal-full-hash collision scan.

| Operation | Bound |
| --- | --- |
| `CreateRange` | O(m (w + c) + n), then one final bulk build per index |
| `Contains` / `TryGetValue` | O(w + c) |
| `GetAt` / positional indexer | O(log n) worst; O(1 + log min(index + 1, n - index)) amortized; endpoints O(1) |
| `IndexOf` | O(w + c + log n) |
| End insertion | O(w + c) amortized on a linear history; O(w + c + log n) ordinary worst case |
| Positional insertion or movement with a label gap | O(w + c + log n) |
| Insertion or movement requiring relabel | O(n (w + c)) |
| Successful value removal | O(w + c + log n); absent removal O(w + c) |
| `GetRange` / `Take` / `Drop` | O(log n) sequence work plus O(min(kept, removed) (w + c)) index reconciliation |
| `Reverse` | O(n (w + c)) rebuild |
| stable one-shot `Sort` | O(n log n) ordering-comparer calls plus O(n (w + c)) rebuild when changed |
| set-producing algebra | O((n + m) (w + c + log(n + m + 1))) conservative worst case |
| set relations | O((n + m) (w + c)) after eager receiver-policy normalization |
| enumeration / `ToArray` | O(n) |

The relabel cost belongs to each produced version. No amortization claim spans sibling branches from
the same pre-relabel version. No benchmark or claim of superiority over another ordered-set
representation is part of the shipment contract.
