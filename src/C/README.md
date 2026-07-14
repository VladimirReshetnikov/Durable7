# C Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents working in the C source root
- Scope: C data-structure workspaces under `src/C`

The C root contains type-erased, explicit-lifetime ports of repository-owned persistent data structures.
The root `build.ps1` delegates to the family-local build systems: the HAMT C17 script and the FingerTree
and Tungsten CMake/CTest presets.

The root wrapper runs selected workspaces sequentially. Every CMake build preset, CTest preset, and
wrapper invocation fixes its job count at one; the direct HAMT compiler/test steps are already serial.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Hamt](Hamt/README.md) | C17 persistent HAMT map/set port | [public header](Hamt/include/Tools/DataStructures/Hamt/hamt.h), [usage](Hamt/docs/usage.md), [API spec](Hamt/docs/api-specification.md) | `.\build.ps1 -Workspace Hamt -RunTests`; see [validation](Hamt/docs/validation.md) and [tests](Hamt/tests/README.md) |
| [FingerTree](FingerTree/README.md) | C11 measured-tree, deque, RRB, DABA Lite, sorted/priority/interval, rope/text, and explicit-lifetime positional cursor port | [core header](FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [DABA header](FingerTree/include/tools/data_structures/finger_tree/daba_lite.h), [usage](FingerTree/docs/usage.md), [API notes](FingerTree/docs/api-notes.md) | `.\build.ps1 -Workspace FingerTree -RunTests`; see [validation](FingerTree/docs/validation.md), [tests](FingerTree/tests/README.md), [samples](FingerTree/samples/README.md), and [benchmarks](FingerTree/benchmarks/README.md) |
| [Tungsten](Tungsten/README.md) | C17 Tungsten `List` and `Association` collection port | [public header](Tungsten/include/tools/data_structures/tungsten/tungsten.h), [tests](Tungsten/tests/tungsten_c_tests.c) | `.\build.ps1 -Workspace Tungsten -RunTests`; also run Release for native parity work |

Use the parent [source index](../README.md) for the full language list, the repository
[workspace map](../../docs/reference/workspace-map.md) for cross-language port lineage, and the
[semantic contracts reference](../../docs/reference/semantic-contracts.md) when checking shared
persistence, ownership, policy, ordering, or failure-behavior obligations.
