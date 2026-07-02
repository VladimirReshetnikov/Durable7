# src/C/Hamt Documentation

- Status: Informational
- Created (UTC): 2026-07-02T18:18:57Z
- Repository HEAD: 3444f5ee27357d86c43db484993f8f12dfd4887c
- Audience: Maintainers and reviewers of the pure C HAMT port
- Scope: Index of design references and local specifications for `src/C/Hamt`

## Current Documents

- [API Specification](api-specification.md) defines the C public API shape, semantic adaptation from
  the C# and C++ workspaces, persistence behavior, ownership rules, and complexity targets for
  `tds_hamt_map` and `tds_hamt_set`.
- [Usage guide](usage.md) shows policy setup, borrowed/owned lifetime rules, persistent update
  patterns, iteration, and set algebra for `tds_hamt_map` and `tds_hamt_set`.
- [Validation](validation.md) records the local MSVC build script, Debug/Release commands, warning
  policy, generated outputs, and native model-test coverage.
