# TypeScript persistent ordered set

- Created (UTC): 2026-07-15T02:15:19Z
- Repository HEAD: 6dbabd71db65ea2771a0b6581c119a367d96d106
- Scope: `PersistentOrderedSet<T>` API, contracts, runtime mapping, and validation

`PersistentOrderedSet<T>` is the strict TypeScript port of the independently owned C# insertion-
ordered set. It is exported from `@vladimir-reshetnikov/data-structures/ordered` and the package root.
“Ordered” means insertion and explicitly requested positional order, not comparison-sorted order.
`sort` is a stable one-shot reorder; subsequent additions append normally.

## Dependency boundary and representation

The implementation is a neutral general-purpose collection:

```text
ordered/PersistentOrderedSet
├── hamt/PersistentHashMap
└── finger-tree/FingerTree
```

It neither imports nor delegates to Tungsten. The CHAMP map associates each equality-policy class
with a private `bigint` order-maintenance stamp. A right-biased last-stamp measure over the persistent
FingerTree sequence supplies logarithmic stamp location and positional edits. Sparse labels use the
C# implementation's `2^20` initial spacing and signed-64-bit relabel boundary, but labels are private
and do not appear in the public API.

Every version maintains one ordered representative and one matching stamp-map entry per policy
class. Published values are immutable. Ordinary point operations path-copy affected HAMT/FingerTree
spines; relabel, reversal, changed sorting, and algebra rebuild unpublished candidates where their
contracts require it. Old versions and sibling branches remain usable. The TypeScript port makes no
C# deque-enumerator-state, allocation, or managed-thread performance claim.

## TypeScript API mapping

| Contract area | TypeScript members |
| --- | --- |
| Construction | `empty`, `create`, `from`, `createRange` |
| State | `size`, `count`, `isEmpty`, `policy`, `first`, `last` |
| Lookup | `contains`, `tryGetValue`, `getAt`, `indexOf` |
| Addition/movement | `add`, `addFirst`, `insert`, `moveToFirst`, `moveToLast`, `moveTo` |
| Removal | `remove`, `tryRemove`, `removeAt`, `removeFirst`, `removeLast`, `clear` |
| Range/order | `getRange`, `take`, `drop`, `reverse`, stable one-shot `sort` |
| Algebra | `union`, `intersect`, `except`, `symmetricExcept` |
| Relations | `isSubsetOf`, `isProperSubsetOf`, `isSupersetOf`, `isProperSupersetOf`, `overlaps`, `setEquals` |
| Enumeration | iteration, `toArray` |

`empty`/`from`, `size`, and `policy` are the package's idiomatic counterparts to C# `Create`/
`CreateRange`, `Count`, and `Comparer`; `create`, `createRange`, and `count` are also supplied as
discoverable parity spellings. `HashPolicy<T>` combines the hash and equivalence callbacks retained
as one exact runtime object.

`tryGetValue` returns `{ found, value }`, distinguishing a stored `undefined` representative from a
miss; a miss echoes the lookup value. `tryRemove` returns `{ removed, set }`, and a miss carries the
exact receiver. Invalid positions/counts throw `RangeError`, empty endpoint operations throw `Error`,
and absent explicit movement throws `OrderedSetMissingValueError`.

JavaScript iterators are ordinary immutable-snapshot iterators. Independently obtained iterators and
an iterator retained across successor construction remain independent; the C# value-enumerator copy
fail-fast rule has no JavaScript analogue.

`validateStructure()` is the TypeScript package's established public-diagnostics adaptation: it
recomputes the Ordered-owned dual-index invariants and returns the validated count. The analogous C#
hook is test-internal, where an `InternalsVisibleTo` seam is available.

## Equality, representatives, and order

The exact retained `HashPolicy<T>` defines class membership and argument normalization. The first
representative installed for a class remains until removal:

- construction keeps its first occurrence and position;
- `add`, `addFirst`, and `insert` are exact identity no-ops for an existing class and never move it;
- explicit movement uses the stored representative and interprets `moveTo`'s index in the final
  result;
- ranges, reversal, sorting, and algebra retain surviving receiver representatives; and
- argument-only algebra classes use the first representative observed during receiver-policy
  normalization.

Default policy semantics are JavaScript SameValueZero for primitives and identity for objects. This
includes ordinary stored `undefined`, `null`, and `NaN` classes; `-0` and `0` collapse while retaining
the first exact representative.

## Algebra, relations, identity, and failure

All algebra and all six relations eagerly consume and normalize the complete argument under the
receiver policy before applying an operation-specific shortcut. Same-type arguments with a different
policy are enumerated; their policy's membership callbacks are not delegated to. Duplicate argument
values count once, first argument representatives win collapsed argument classes, and a late iterator
or policy failure is therefore observable even when an earlier value appears decisive.

Result ordering is receiver order followed, where applicable, by first normalized argument-only
order. Receiver representatives win shared classes. Duplicate additions, current-position movement,
absent removal, empty clear, full range, trivial reverse/sort, unchanged stable sort, and logical
union/intersection/difference no-ops return the receiver exactly. Empty results retain the receiver
policy and use the shared empty singleton only for the shared default policy.

All callbacks run while building unpublished candidates. A hash, equivalence, ordering, or iterator
failure cannot mutate a source or partially publish a successor. Positional validation precedes hash
and equivalence callbacks for `insert`, `moveTo`, and range operations.

The TypeScript FingerTree facade is backed by the workspace's persistent measured AVL sequence.
Membership remains bounded-CHAMP-depth plus any equal-hash collision scan; positional lookup,
movement, and edits are logarithmic in the sequence size, and enumeration is linear. This port does
not inherit the tuned C# deque's endpoint amortization or struct-enumerator allocation claims.

## Validation

The Ordered suites port the C# examples, boundaries, representative rules, exhaustive small movement
and relation tables, repeated relabel histories, sibling branches, eager/failing normalization,
stable sorting, retained iterators/readers, public surface, package export, and dependency-boundary
checks. A fast-check branching command model compares every retained version against an independent
policy-aware ordered-list model. Run:

```powershell
cd src/TypeScript
npx vitest run test/ordered
npm run validate
npm pack --dry-run
```
