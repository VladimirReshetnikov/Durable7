# C++ Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents working in the C++ source root
- Scope: C++ data-structure workspaces under `src/Cpp`

The C++ root contains value-semantics ports of repository-owned persistent data structures. The root
`build.ps1` delegates to the family-local build systems: the HAMT C++20 script and the FingerTree,
Ordered, and Tungsten CMake/CTest presets.

The root wrapper runs selected workspaces sequentially. Every CMake build preset, CTest preset, and
wrapper invocation fixes its job count at one; the direct HAMT compiler/test steps are already serial.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Hamt](Hamt/README.md) | C++20 CHAMP collections, strict patches, directed graphs, indexed maps, Patricia, and exact-wire Merkle | [aggregate header](Hamt/include/Tools/DataStructures/Hamt/hamt.hpp), [API spec](Hamt/docs/api-specification.md) | `.\build.ps1 -Workspace Hamt -RunTests`; see [validation](Hamt/docs/validation.md) and [tests](Hamt/tests/README.md) |
| [FingerTree](FingerTree/README.md) | C++23 measured-tree family, chunked bit set, RRB, DABA Lite, priority/interval, ropes, and cursors | [aggregate header](FingerTree/include/tools/data_structures/finger_tree/finger_tree.hpp), [API notes](FingerTree/docs/api-notes.md) | `.\build.ps1 -Workspace FingerTree -RunTests`; see [validation](FingerTree/docs/validation.md) and [tests](FingerTree/tests/README.md) |
| [Ordered](Ordered/README.md) | Neutral C++23 insertion-ordered set, map, and grouped multimap | [aggregate header](Ordered/include/tools/data_structures/ordered/ordered.hpp), [API notes](Ordered/docs/api-notes.md) | `.\build.ps1 -Workspace Ordered -RunTests`; see [validation](Ordered/docs/validation.md) and [tests](Ordered/tests/README.md) |
| [Tungsten](Tungsten/README.md) | Application-specific C++23 leaf port of Tungsten `List` and `Association` | [aggregate header](Tungsten/include/tools/data_structures/tungsten/tungsten.hpp), [list header](Tungsten/include/tools/data_structures/tungsten/persistent_list.hpp), [association header](Tungsten/include/tools/data_structures/tungsten/persistent_association.hpp) | `.\build.ps1 -Workspace Tungsten -RunTests` |

Use the parent [source index](../README.md) for the full language list, the repository
[workspace map](../../docs/reference/workspace-map.md) for cross-language port lineage, and the
[semantic contracts reference](../../docs/reference/semantic-contracts.md) when checking shared
persistence, ownership, policy, ordering, or failure-behavior obligations.
