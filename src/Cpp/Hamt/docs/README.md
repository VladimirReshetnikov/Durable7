# src/Cpp/Hamt Documentation

- Status: Informational
- Created (UTC): 2026-07-02T17:58:46Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers and reviewers of the native C++ HAMT port
- Scope: Index of design references and local specifications for `src/Cpp/Hamt`

## Current Documents

- [API Specification](api-specification.md) defines the C++ public API shape, semantic adaptation
  from the C# workspace, persistence behavior, and complexity targets for `persistent_hash_map` and
  `persistent_hash_set`.
- [Usage guide](usage.md) shows include paths, value-semantics patterns, map/set operations,
  policy objects, iteration, and set algebra for the C++ HAMT templates.
- [Validation](validation.md) records the local MSVC build script, Debug/Release commands, warning
  policy, generated outputs, and native model-test coverage.
