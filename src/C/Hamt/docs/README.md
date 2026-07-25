# src/C/Hamt Documentation

- Status: Informational
- Created (UTC): 2026-07-02T18:18:57Z
- Repository HEAD: 3444f5ee27357d86c43db484993f8f12dfd4887c
- Audience: Maintainers and reviewers of the pure C HAMT port
- Scope: Index of design references and local specifications for `src/C/Hamt`

## Current Documents

- [API Specification](api-specification.md) defines the C public API shape, semantic adaptation from
  the C# and C++ workspaces, persistence behavior, ownership rules, and complexity targets for
  `d7_hamt_map`, `d7_hamt_set`, and `d7_hamt_bag`, including map/set one-way transient
  edit-session handles and the bag's checked multiplicity/algebra contract.
- [Usage guide](usage.md) shows policy setup, borrowed/owned lifetime rules, persistent update
  patterns, one-way edit sessions, iteration, set algebra, and persistent hash-bag operations.
- [Validation](validation.md) records the local MSVC build script, Debug/Release commands, warning
  policy, generated outputs, and native model-test coverage.
- [Merkle search tree](merkle-search-tree.md) specifies the type-erased persistent ordered map,
  exact `mst-sha256-b16-v2` domain framing, canonical MST2 node blocks, verified persistence,
  bounded load/import, MSP2 proofs, synchronization, three-way merge, ownership, and diagnostics.
- [Tests README](../tests/README.md) maps the native executables, named test cases, direct executable
  paths, and runner failure behavior.
