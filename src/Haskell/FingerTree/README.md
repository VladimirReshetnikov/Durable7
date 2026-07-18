# Haskell FingerTree

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell finger-tree family port
- Scope: `tools-data-structures-fingertree` package

This package ports the repository finger-tree family to Haskell. It includes a general measured
finger tree, a size-and-rightmost-leaf-measured deque, a reversible deque, sorted bag/set/map
facades, a stable meldable priority queue, a worst-case-optimal Brodal-Okasaki heap, a keyed
priority-search queue, interval tree and payload-bearing interval-map helpers, positional ropes, measured ropes, text-rope navigation
helpers, immutable positional/measured/text rope cursors, a persistent RRB vector, a policy-canonical
zip-zip-tree sorted set, a persistent lazy range-update sequence, and
`PersistentChunkedBitSet`, a measured sparse sequence of nonzero 64-bit words with logarithmic
membership, inclusive rank/select, and sparse linear algebra.

`IntervalMap a v` composes the augmented low-sorted `IntervalTree` with an ordered exact-key map.
It provides unique closed-interval keys, strict addition, payload replacement, exact and indexed
lookup, pruned first/all/count overlap queries, snapshot-preserving removal, and cross-index
validation.

`RangeUpdateSequence` is a deterministic implicit-key AVL tree with cached count, height, and
ordered logical measure. A retained `RangeUpdateAlgebra` record defines element measurement,
ordered measure combination, identity recognition, directional `compose newer older`, and tag
actions on elements and cached subtree measures. Whole-sequence nonidentity updates replace one
root in O(1); indexed edits, splits, joins, proper range updates, and proper range queries copy or
visit O(log n) boundary nodes. Reads carry pending tags without publishing pushed trees, while
structural descent pushes tags immutably before rotations. Empty ranges and semantically identity
tags retain the source root. As with `MeasuredRope`, operands passed to `append` must retain
extensionally identical policy functions because Haskell functions have no decidable equality.
The port uses `Maybe` for invalid indices and ranges and the package's checked-`Int` pure exception
for count overflow.

[`CanonicalSortedSet`](docs/canonical-sorted-set.md) derives exact HMAC-SHA-256 ranks from a caller's
equivalence-class hash and a retained seeded, keyed, or fresh-random policy. Policy creation is an
explicit `IO` boundary that allocates honest algebra-family identity; all persistent set operations
are pure. Stable bulk construction and incremental histories converge on one topology, first
representatives are retained, cross-policy equality uses the receiver's comparison semantics, and
same-family algebra preserves canonicality. Iterative construction, updates, equality, enumeration,
digesting, and validation remain stack-safe even when a constant rank hash forces a linear tree.

`BrodalOkasakiHeap a` directly implements the bootstrapped skew-binomial representation: the
rank-zero global root stores the minimum while its child list fuses primitive skew-tree children
with the embedded heap forest. Insert, meld, and minimum are O(1) worst-case and minimum deletion is
O(log n). Its validator decodes every fused boundary and checks skew ranks, heap order, count, and
depth without depending on the operation code.

`PrioritySearchQueue k p v` is a direct persistent winner-cached AVL core. Each key has one
priority/payload entry, each node caches its subtree winner, and equal priorities break by key.
Keyed updates and minimum deletion are O(log n), minimum lookup is O(1), and inclusive key-range
plus priority-threshold queries prune subtrees whose cached winner already exceeds the threshold.
`validateStructure` independently checks order, AVL balance, cached count/height, and every winner.

`RrbVector a` uses immutable boxed arrays for 32-element leaves and 32-way branches. Packed branches
omit cumulative sizes and select children from five-bit radix digits; split/append-created relaxed
branches retain exact prefix sizes. The surface provides indexed replacement, endpoints,
boundary-spine append, split, range edits, `unsnoc`, structural validation/statistics, and an IO-only
root-sharing diagnostic for tests. `fromList` is the idiomatic pure bulk-construction path: unlike
the strict-language ports, Haskell does not expose a public mutable transient builder. Append
redistributes only the boundary seam, so the test suite's density ceilings are regression gates and
not a global minimum-occupancy invariant. The validator's height cap is derived from the `Int` bit
width plus one legal boundary-only slack level and is thirteen on the repository's 64-bit targets.

`ReversibleDeque a` uses its own orientation-aware strict finger-tree core. `reverse` is an O(1)
mirror of the root, and `append` glues logical digits through mirrored middle views, so
mixed-orientation appends such as `append (reverse left) right` remain tree-based instead of
reifying either operand through lists.

The core measured tree follows the Hinze-Paterson shape directly. Some derived Haskell facades use
idiomatic `containers` storage where it preserves the same observable contract more naturally than
copying a stricter managed implementation detail.

The deque's rightmost-leaf signpost is comparator-agnostic storage used by the runtime-comparator
sorted helpers. Lower bound, upper bound, and binary search follow one measured root-to-leaf path in
O(log n), rather than binary-searching through O(log n) positional probes.

`SortedBag` uses the measured core rather than `Data.Map`: each distinct-key bucket is a
`Data.Sequence`, and the spine caches total multiplicity, distinct count, and last key. Key counts,
rank lookup, and rank-boundary slicing therefore descend in O(log n); slicing rebuilds at most its
two boundary buckets and shares untouched measured subtrees. This remains logarithmic when all
instances belong to one comparer-equal bucket.

`Rope` and `MeasuredRope` are chunked facades over that measured core, with chunks capped at 64
elements. Their spines are measured by element count (and, for `MeasuredRope`, by the caller's
monoidal element measure), so indexing, splitting, prefix measurement, and measure-guided location
navigate cached measures in O(log n), plus at most one bounded chunk scan. Endpoint and positional
edits replace only a boundary chunk and retain untouched tree/chunk storage; `append` joins the two
spines without flattening. Because Haskell functions do not have decidable equality, two
`MeasuredRope` operands passed to `append` must have extensionally identical element-measure
functions.

`RopeCursor a` is an opaque immutable snapshot-plus-gap cursor over `Rope a`. `cursor` and
`cursorAt` create gaps in `0 .. count`; the query, peek, movement, seek, insertion, deletion,
replacement, and `snapshot` functions preserve retained ancestors and let edits branch freely.
`Maybe` represents an absent neighbor or invalid operation in the usual Haskell way: when the
element type is itself `Maybe a`, `Just Nothing` is a stored `Nothing` and outer `Nothing` is a
boundary. Same-position seek and empty range insertion preserve the exact retained version state,
and unconditional replacement has no `Eq` constraint and stores the supplied representative.
Construction, movement, and snapshot are O(1); peeks and point edits are O(log n) plus bounded
64-element chunk work, and inserting `m` values is O(m + log n). This is deliberately a persistent
path-copy checkpoint, not the C# focused cursor representation, so it makes no focus-local or benchmark-parity claim.

`MeasuredRopeCursor v a` applies the same opaque immutable gap model to the exact retained
`MeasuredRope`. It adds ordered `measureBefore` and `measureAfter` partitions plus absolute
measure-guided factory and seek operations. `MeasuredRopeCursorSearch` always carries a usable
cursor: a lawful monotone-prefix hit selects the gap before the first satisfying element, while an
empty rope or miss returns the end gap with `searchFound == False`. Measure partitions preserve
left-to-right monoid order without assuming an inverse, commutativity, or element equality. Peeks
retain Haskell's nested-`Maybe` distinction, replacement has no `Eq` constraint, same-gap seek and
empty insertion preserve the exact source, and edits branch through the existing measured rope.
Creation, movement, positional seek, and snapshot are O(1); peeks, ordered measure reads, point
edits, and absolute measure search are O(log n) plus one bounded 64-element chunk scan, and range
insertion is O(m + log n). Element-measure and monoid work may occur during measure reads and edits.

`TextRopeCursor` and `TextRopeCursorSearch` are zero-wrapper type aliases over that measured cursor,
with the full gap/edit/search surface and `lineColumnOfCursor` re-exported by the text module. A
snapshot is therefore immediately usable by every existing text helper. Positions and columns count
Haskell `Char` elements, not UTF-16 code units or grapheme clusters: a supplementary-plane character
normally occupies one `Char`, a combining mark occupies another, and a surrogate-valued `Char` also
occupies one. Because `TextRope` itself is a type alias, callers can construct
one through the generic measured API with a non-newline measure; the text cursor and helper contract
requires values created by `fromString`/`fromText` or an extensionally identical newline measure.

Positional- and measured-rope growth checks the cached `Int` count before constructing a result.
`append`, point/range insertion, and their cursor forms raise the family-specific pure length-
overflow exception if the result would exceed `maxBound`; the receiver and every retained source
remain reusable. Measured range insertion validates the gap, consumes only enough list spine to
prove the count fits, then constructs its measured middle once, so overflow wins before the new
elements or their measure/monoid callbacks are forced. As usual for pure Haskell, forcing an
unevaluated operand or input spine is outside that callback-order guarantee. The text facade also
checks the derived `newlineCount + 1` line count instead of wrapping at `maxBound`.

The text helpers use the measured rope's cached newline counts for offset/line navigation rather
than rescanning the entire text. The interval tree is a low-sorted finger tree annotated with both
maximum-high and last-low values: lower-bound insertion and the first overlap query are O(log n),
and overlap enumeration repeatedly prunes prefixes whose maximum high cannot reach the probe.
`compact`, full enumeration, and text/string conversion remain intentionally linear in the number
of returned elements.

```powershell
cd src\Haskell
.\test.ps1 -Workspace FingerTree
```

The wrapper forces one Cabal build job. The local [test README](test/README.md) lists the
deterministic coverage areas.
