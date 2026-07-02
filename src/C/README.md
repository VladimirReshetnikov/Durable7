# C Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents working in the C source root
- Scope: C data-structure workspaces under `src/C`

The C root contains type-erased, explicit-lifetime ports of repository-owned persistent data structures. The
workspaces share a native MSVC validation environment, but their build systems differ by port history.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Hamt](Hamt/README.md) | C17 persistent HAMT map/set port | [public header](Hamt/include/Tools/DataStructures/Hamt/hamt.h), [usage](Hamt/docs/usage.md), [API spec](Hamt/docs/api-specification.md) | `Hamt/build.ps1 -RunTests`; see [validation](Hamt/docs/validation.md) and [tests](Hamt/tests/README.md) |
| [FingerTree](FingerTree/README.md) | C11 measured-tree, deque, sorted/priority/interval, rope, and text-rope port | [public header](FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [usage](FingerTree/docs/usage.md), [API notes](FingerTree/docs/api-notes.md) | CMake/CTest presets; see [validation](FingerTree/docs/validation.md), [tests](FingerTree/tests/README.md), [samples](FingerTree/samples/README.md), and [benchmarks](FingerTree/benchmarks/README.md) |

Use the parent [source index](../README.md) for the full language list and the repository
[workspace map](../../docs/reference/workspace-map.md) for cross-language port lineage.
