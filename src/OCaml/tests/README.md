# OCaml Test Map

- Created (UTC): 2026-07-17T22:45:00Z
- Repository HEAD: 87dc70271d808e50b52c868cc4956a8da69b2504
- Audience: Maintainers extending the OCaml port
- Scope: Alcotest/QCheck executables and the contracts they own

| Suite | Cases | Primary coverage |
| --- | ---: | --- |
| `common` | 2 | Runtime hash/equality and comparison policy identity, reversal, and behavior |
| `numerics` | 7 | Fixed-width wrapping/checked arithmetic, signed division, exact two's-complement bytes, bit diagnostics, Zarith models, sparse-integer operations |
| `hamt` | 16 | CHAMP collisions, representatives, factories, detached staging builders, transient lifecycle and six set relations, set/bag/bimap/derived families, Patricia, synchronized snapshots, exact `MST2`, persistence budgets, proofs, and merge |
| `finger_tree` | 16 | Measured/deque persistence, generated list model, sorted/priority/interval families, vector, rank/select bits, law-gated Range, generic/measured/text ropes and cursors, canonical ranks, meldable heap, priority search, DABA failure atomicity |
| `ordered` | 4 | Neutral ordered set/map/multimap representatives, movement, ranges, receiver-policy algebra, grouped order, generated histories, Tungsten-free behavior |
| `tungsten` | 4 | List vocabulary/model histories and Association replacement/movement/insertion/join/sort rules |

Run all suites with `opam exec -- dune runtest -j 1 --force`, or use `test.ps1 -Workspace <name>`
for a focused lane. Tests remain deterministic except for QCheck's reported replay seed on failure.
