# C++ Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents working in the C++ source root
- Scope: C++ data-structure workspaces under `src/Cpp`

The C++ root contains value-semantics ports of repository-owned persistent data structures. HAMT is a C++20
template library validated by the local build script; FingerTree is a header-first C++23/CMake workspace validated
through CTest.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Hamt](Hamt/README.md) | C++20 persistent HAMT map/set port | [map header](Hamt/include/Tools/DataStructures/Hamt/persistent_hash_map.hpp), [set header](Hamt/include/Tools/DataStructures/Hamt/persistent_hash_set.hpp), [usage](Hamt/docs/usage.md), [API spec](Hamt/docs/api-specification.md) | `Hamt/build.ps1 -RunTests`; see [validation](Hamt/docs/validation.md) and [tests](Hamt/tests/README.md) |
| [FingerTree](FingerTree/README.md) | C++23 measured-tree, deque, sorted/priority/interval, rope, and text-rope port | [aggregate header](FingerTree/include/tools/data_structures/finger_tree/finger_tree.hpp), [usage](FingerTree/docs/usage.md), [API notes](FingerTree/docs/api-notes.md) | CMake/CTest presets; see [validation](FingerTree/docs/validation.md) and [tests](FingerTree/tests/README.md) |

Use the parent [source index](../README.md) for the full language list and the repository
[workspace map](../../docs/reference/workspace-map.md) for cross-language port lineage.
