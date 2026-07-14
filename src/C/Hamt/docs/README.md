# src/C/Hamt Documentation

- Status: Informational
- Created (UTC): 2026-07-02T18:18:57Z
- Repository HEAD: 3444f5ee27357d86c43db484993f8f12dfd4887c
- Audience: Maintainers and reviewers of the pure C HAMT port
- Scope: Index of design references and local specifications for `src/C/Hamt`

## Current Documents

- [API Specification](api-specification.md) defines the C public API shape, semantic adaptation from
  the C# and C++ workspaces, persistence behavior, ownership rules, and complexity targets for
  `tds_hamt_map` and `tds_hamt_set`, including their one-way transient edit-session handles.
- [Usage guide](usage.md) shows policy setup, borrowed/owned lifetime rules, persistent update
  patterns, one-way edit sessions, iteration, and set algebra for `tds_hamt_map` and `tds_hamt_set`.
- [Validation](validation.md) records the local MSVC build script, Debug/Release commands, warning
  policy, generated outputs, and native model-test coverage.
- [Merkle search tree](merkle-search-tree.md) specifies the type-erased persistent ordered map,
  exact `mst-sha256-b16-v2` domain framing, canonical MST2 node blocks, verified persistence,
  bounded load/import, MSP2 proofs, synchronization, three-way merge, ownership, and diagnostics.
- [Tests README](../tests/README.md) maps the native executable, named test cases, direct executable path,
  and runner failure behavior.
