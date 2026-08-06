# OCaml Collection Port — 2026-08-05

> **Current-state note (2026-08-05, later the same day).** A Python port of the same seven
> collections shipped after this one, so coverage is now eight languages and only TypeScript
> remains. This review's scope and findings are unchanged and still describe the OCaml shipment; the
> Python port carries its own verification, recorded in
> [the Python port review](python-collection-port-2026-08-05.md).

- Created (UTC): 2026-08-05
- Repository HEAD (reviewed): `experimental` branch, post C++ port
- Audience: Maintainers reviewing the seven-language shipment of the seven research-derived collections
- Scope: The OCaml port of all seven collections plus the level-ancestor seam they share, the
  intentional divergences it makes, and the verification evidence

## Decision Recorded

The seven research-derived collections are now ported to OCaml, making coverage **C#, Rust, C,
Haskell, Kotlin, C++, and OCaml**. Only Python and TypeScript remain unported under the same
parity-economics rule. The five earlier port reviews carry current-state notes pointing here.

## What Shipped

| Collection | OCaml module |
| --- | --- |
| `AncestralSliceQueue<T>` | `Durable7.Finger_tree.Ancestral_slice_queue` |
| `BilateralAncestralDeque<T>` | `Durable7.Finger_tree.Bilateral_ancestral_deque` |
| `ContextualRankSequence<TElement, TMachine>` | `Durable7.Finger_tree.Contextual_rank_sequence` |
| `PersistentDeltaMap<TKey, TValue>` | `Durable7.Finger_tree.Persistent_delta_map` |
| `PersistentRunDeltaVector<T>` | `Durable7.Finger_tree.Persistent_run_delta_vector` |
| `PersistentMonotoneActionHeap<TElement, TPriority, TAction>` | `Durable7.Finger_tree.Persistent_monotone_action_heap` |
| `PersistentAncestralConnectionForest` | `Durable7.Hamt.Persistent_ancestral_connection_forest` |

`Durable7.Finger_tree.Incremental_ancestor` carries the level-ancestor seam. Every module is an
`.ml`/`.mli` pair whose interface holds the odoc contract.

## The Substrates Are Weaker Here, and the Bounds Say So

This is the port where honest bounds mattered most, because three of the substrates deliver less
than the managed baseline's. Each was established by reading — and in one case by measuring — rather
than inherited:

- **`Measured_tree` is a weight-balanced join tree, not a Hinze–Paterson finger tree.** There are no
  digits and no lazy middle; `cons`/`snoc` are literally a join against a singleton. So
  `Contextual_rank_sequence` states Θ(s log n) endpoint updates rather than O(s) amortized, and a
  concatenation bound that depends on the operands' height difference rather than on the smaller
  operand — the baseline's O(s log(min(n, m))) form does not hold in either direction. This was
  confirmed by extracting the join/split kernel into standalone probes with a counting monoid and
  measuring composition counts at n = 256 through 262144: endpoint cost is exactly log₂ n
  compositions, balanced concatenation is one, and maximally skewed concatenation is log₂ n. OCaml
  therefore sits with Rust and Kotlin, not with Haskell and C++.
- **`Sorted_map` is an immutable entry array, not a path-copied tree.** `Persistent_delta_map`'s
  writes are Θ(N + k) rather than O(log N), and `key_range` is Θ(log N + output) because the window
  is copied rather than shared. In the other direction its extremes and rank selection are O(1),
  *better* than the tree-backed ports. Both directions are documented.
- **`Rrb_vector` is a facade over the same measured tree, not a relaxed radix trie**, so the
  run-delta vector's splice bounds are worst case rather than amortized.

None of that touches the load-bearing properties: run splices remain independent of run length and
comparison-free, and range-restricted change enumeration is a genuine boundary seek through
`Sorted_map.key_range`, not the filter the C port once shipped.

## Two OCaml-Specific Hazards, Handled Rather Than Documented Away

**`ocamlopt` lifts fully constant records into shared static data.** A history root allocated as a
plain constant record returns the *same physical block* on every call, in both native and bytecode —
so two independently created connection forests would have shared one history root, and version
identity is physical here. The port patches the root's self-reference through a mutable field that
is never reassigned and never observable untied, and confirmed the test catches the regression by
swapping a shared root back in.

**OCaml's `( = )` is not an equivalence relation on any float-bearing payload.** Measured on this
toolchain: `nan = nan` is false while `compare nan nan` is zero, and the disagreement survives
nesting. Since a non-reflexive relation silently corrupts run accounting, the run-delta vector's
default relation is `compare x y = 0` rather than `( = )`, `reflexive_ieee` is `Float.equal`, and its
audit *rejects* a version whose relation is observably non-reflexive at a clean position — a
condition the Rust port documents as undetectable.

A third, smaller one: OCaml's `==` on immediate payloads (`int`, `bool`, `char`) is value equality,
which would have made the "a clean position reuses its exact checkpoint slot" invariant vacuous for
exactly the payload type the tests use. The port boxes values in a one-field cell so the invariant
has teeth for every payload type — notably *not* because the substrate needed it, since
`Rrb_vector.set` applies no equality short-circuit at all, unlike Kotlin's and C++'s.

## Adversarial Parity Audit

Each of the eight units was audited member-by-member against its C# baseline by an independent
reader, and every finding was passed to a second reader instructed to refute it by default.
`Bilateral_ancestral_deque`, `Persistent_run_delta_vector`, and
`Persistent_ancestral_connection_forest` were found faithful with nothing to report. Six findings
survived; all are fixed.

| Severity | Unit | Finding | Fix |
| --- | --- | --- | --- |
| Major | `Contextual_rank_sequence` | **`split_at` and `sub` were the only constructors that never called `publish`**, so a summary fault created while the split re-joins subtrees was silently discarded and a sequence with a corrupt cached event table was returned as `Ok` — one its own `validate` rejects. This broke the module's stated guarantee that a rejected successor publishes nothing | Both routed through `publish`; either half faulting fails the whole call, so nothing partial escapes. The `.mli` now states the new failure mode |
| Minor | `Contextual_rank_sequence` | No test put a half produced by `split_at` through the model or the audit — the split test only checked that halves rejoin and that lengths sum, neither of which reads the cached summary. That is precisely why the defect above went unnoticed | Each half is now audited and content-checked at every boundary |
| Medium | `Incremental_ancestor` | The baseline's public backend seam has **no OCaml counterpart** — no module type, no functor — and the removal was documented nowhere, though Rust, C++, C, and Kotlin all preserve it and Haskell documents why it drops it | Documented explicitly as a deliberate omission, with the reason and the consequence for the parameterized bounds |
| Low | `Ancestral_slice_queue` | The anchored-empty note claimed a queue drained from the back "walks back to the arena's bottom node" — true only when the window began at depth zero | Corrected to the node one level above the window |
| Low | `Persistent_delta_map` | `key_range` silently kept the baseline's O(log N) bound although the array substrate copies the selected slice | Restated as Θ(log N + output), with the structural-sharing contrast to C# and Rust |
| Low | `Persistent_monotone_action_heap` | The test claiming to pin "transforming an empty heap reshares it" was unfalsifiable, since the root-sharing predicate is unconditionally true for two empty heaps | Replaced with physical identity of the whole value, which a rebuilding `transform_all` would fail |

The recurring self-comparison defect appeared once more, in a new disguise: a sharing predicate that
is *unconditionally* true for the empty case rather than one comparing a value with itself. Worth
adding to the pattern list for future ports.

## Verification

- `dune build @check`: clean, under the workspace's warnings-as-errors set (`@a-4-42-44-48-70`,
  which makes even `name-out-of-scope` an error).
- `dune runtest`: **138 tests pass, up from 48 at baseline** — 6 for the arena seam, 70 across the
  six finger-tree collections, and 14 for the connection forest.
- The arena's own group carries the assertion the earlier Kotlin and C++ audits had to add
  retroactively: a maximum-hop bound over a 32,768-node chain, which is the only thing that can catch
  a coalescing-free arena, since removing the jump links leaves every ancestor *answer* correct.
  This port shipped it from the start rather than after an audit.
- Every collection group was mutation-tested during development — reversing a tag composition
  direction, dropping an expose, flipping a minimum tie-break, replacing a range seek with a filter,
  removing run merging or canonicalization, and substituting a fresh-but-equal cell on cancellation
  were each confirmed to fail a named case.

**Two gates could not be exercised.** Neither `ocamlformat` nor `odoc` is installed in this
environment, so `@fmt` and `@doc` were not run. A green `dune build @doc` in that state is vacuous —
the alias has nothing to run — and is not reported here as a passing documentation gate. Formatting
was matched by hand against the surrounding files (`profile=conventional, margin=100`), and every
`.mli` carries odoc comments on every exported type and value, but both remain unverified until a
machine with those tools runs them.

## Follow-Up Filed

While looking for a skew-binomial kernel to mirror, the heap port found that this workspace's
`Brodal_okasaki_heap` is list-backed — which the workspace docs already disclaim for *bounds* — but
that its `validate`-equivalent synthesizes shape statistics arithmetically from the element count
rather than measuring anything, and that its implementation header describes a forest-merging
algorithm the code does not implement. Both are tracked separately, as questions about already-
shipped code rather than about this port.
