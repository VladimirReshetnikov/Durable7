# C++ Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents working in the C++ source root
- Scope: C++ data-structure workspaces under `src/Cpp`

The C++ root contains value-semantics ports of repository-owned persistent data structures. The root
`build.ps1` delegates to the family-local build systems: the HAMT C++20 script and the FingerTree
and Tungsten CMake/CTest presets.

The root wrapper runs selected workspaces sequentially. Every CMake build preset, CTest preset, and
wrapper invocation fixes its job count at one; the direct HAMT compiler/test steps are already serial.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Hamt](Hamt/README.md) | C++20 CHAMP/Patricia port and exact-wire Merkle core | [aggregate header](Hamt/include/Tools/DataStructures/Hamt/hamt.hpp), [Merkle guide](Hamt/docs/merkle-search-tree.md), [usage](Hamt/docs/usage.md), [API spec](Hamt/docs/api-specification.md) | `.\build.ps1 -Workspace Hamt -RunTests`; see [validation](Hamt/docs/validation.md) and [tests](Hamt/tests/README.md) |
| [FingerTree](FingerTree/README.md) | C++23 measured-tree, deque, RRB vector, mutable DABA Lite window aggregation, sorted/priority/interval, rope/text port, and positional/measured/text cursor checkpoints | [aggregate header](FingerTree/include/tools/data_structures/finger_tree/finger_tree.hpp), [DABA Lite](FingerTree/include/tools/data_structures/finger_tree/daba_lite.hpp), [RRB vector](FingerTree/include/tools/data_structures/finger_tree/rrb_vector.hpp), [usage](FingerTree/docs/usage.md), [samples](FingerTree/samples/README.md), [benchmarks](FingerTree/benchmarks/README.md) | `.\build.ps1 -Workspace FingerTree -RunTests`; see [validation](FingerTree/docs/validation.md) and [tests](FingerTree/tests/README.md) |
| [Tungsten](Tungsten/README.md) | C++23 Tungsten `List` and `Association` collection port | [aggregate header](Tungsten/include/tools/data_structures/tungsten/tungsten.hpp), [list header](Tungsten/include/tools/data_structures/tungsten/persistent_list.hpp), [association header](Tungsten/include/tools/data_structures/tungsten/persistent_association.hpp) | `.\build.ps1 -Workspace Tungsten -RunTests` |

Use the parent [source index](../README.md) for the full language list, the repository
[workspace map](../../docs/reference/workspace-map.md) for cross-language port lineage, and the
[semantic contracts reference](../../docs/reference/semantic-contracts.md) when checking shared
persistence, ownership, policy, ordering, or failure-behavior obligations.
