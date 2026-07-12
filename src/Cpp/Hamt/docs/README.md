# src/Cpp/Hamt Documentation

- Status: Informational
- Created (UTC): 2026-07-02T17:58:46Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers and reviewers of the native C++ HAMT port
- Scope: Index of design references and local specifications for `src/Cpp/Hamt`

## Current Documents

- [API Specification](api-specification.md) defines the C++ public API shape, semantic adaptation
  from the C# workspace, immutable-version behavior, and complexity targets for the CHAMP,
  Patricia, and Merkle search-tree families.
- [Merkle Search Tree](merkle-search-tree.md) specifies the exact `mst-sha256-b16-v2` policy and
  `MST2` block contract, canonical B=16 topology, codec rules, structural sharing, and validation
  surface. Its scope is the in-memory core and wire format, not stores, proofs, sync, or merge.
- [Usage guide](usage.md) shows include paths, value-semantics patterns, map/set operations,
  policy objects, Merkle construction and diagnostics, iteration, and set algebra for the C++
  templates.
- [Validation](validation.md) records the local MSVC build script, Debug/Release commands, warning
  policy, portable compiler lanes, generated outputs, and native model/wire-test coverage.
- [Tests README](../tests/README.md) maps the native executables, named coverage groups, direct
  executable paths, and runner failure behavior.
