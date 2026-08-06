# OCaml Test Map

- Created (UTC): 2026-07-17T22:45:00Z
- Repository HEAD: 87dc70271d808e50b52c868cc4956a8da69b2504
- Audience: Maintainers extending the OCaml port
- Scope: Alcotest/QCheck executables and the contracts they own

| Suite | Cases | Primary coverage |
| --- | ---: | --- |
| `common` | 2 | Runtime hash/equality and comparison policy identity, reversal, and behavior |
| `hamt` | 32 | CHAMP collisions, representatives, factories, detached staging builders, transient lifecycle and six set relations, set/bag/bimap/derived families, Patricia, synchronized snapshots, exact `MST2`, persistence budgets, proofs, and merge |
| `finger_tree` | 98 | Measured/deque persistence, generated list model, sorted/priority/interval families, vector, rank/select bits, law-gated Range, generic/measured/text ropes and cursors, canonical ranks, meldable heap, priority search, DABA failure atomicity |
| `ordered` | 6 | Neutral ordered set/map/multimap representatives, movement, ranges, receiver-policy algebra, grouped order, and generated histories |

The research-derived collections contribute seven `finger_tree` groups and one `hamt` group. The
level-ancestor seam's group carries the load-bearing assertion: a maximum-hop bound over a
32,768-node chain, which is the only thing that can catch a coalescing-free arena, since removing the
jump links leaves every ancestor answer correct and only changes the work per query. The collection
groups assert exact query-count profiles rather than ceilings, pair every counting relation with a
positive control before asserting a zero-callback claim, and pin representative identity through a
boxed cell so the clean-slot invariant does not degenerate into value equality on immediate payloads.

Run all suites with `opam exec -- dune runtest -j 1 --force`, or use `test.ps1 -Workspace <name>`
for a focused lane. Tests remain deterministic except for QCheck's reported replay seed on failure.
