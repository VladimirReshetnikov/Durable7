# Haskell FingerTree

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell finger-tree family port
- Scope: `tools-data-structures-fingertree` package

This package ports the repository finger-tree family to Haskell. It includes a general measured
finger tree, a size-and-rightmost-leaf-measured deque, a reversible deque, sorted bag/set/map
facades, a stable meldable priority queue, interval tree helpers, positional ropes, measured ropes,
text-rope navigation helpers, and a persistent RRB vector.

`RrbVector a` uses immutable boxed arrays for 32-element leaves and 32-way branches. Packed branches
omit cumulative sizes and select children from five-bit radix digits; split/append-created relaxed
branches retain exact prefix sizes. The surface provides indexed replacement, endpoints,
boundary-spine append, split, range edits, `unsnoc`, structural validation/statistics, and an IO-only
root-sharing diagnostic for tests. `fromList` is the idiomatic pure bulk-construction path: unlike
the strict-language ports, Haskell does not expose a public mutable transient builder.

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

The local [test README](test/README.md) lists the deterministic coverage areas.
