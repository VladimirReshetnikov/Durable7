# C Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Updated (UTC): 2026-07-27T17:20:00Z
- Repository HEAD: dc8c8f3fa715a02db94a5a9ef3347baeac0b70a0
- Audience: Maintainers and AI agents working in the C source root
- Scope: C data-structure workspaces under `src/C`

The C root contains type-erased, explicit-lifetime ports of repository-owned persistent data
structures. Where the managed ports lean on a garbage collector and generics, these workspaces make
every corresponding decision explicit: keys and values are `void *`, behavior arrives through
caller-supplied callback tables, allocation can fail and is checked, and each public value handle
has stated `clone` / `move` / `destroy` obligations. Reading a C port is often the fastest way to
see what a structure actually costs.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Hamt](Hamt/README.md) | C17 persistent HAMT collections, strict patches, directed graphs, indexed maps, Patricia, and Merkle | [public headers](Hamt/include/durable7/hamt), [API spec](Hamt/docs/api-specification.md) | `.\build.ps1 -Workspace Hamt -RunTests`; see [validation](Hamt/docs/validation.md) and [tests](Hamt/tests/README.md) |
| [FingerTree](FingerTree/README.md) | C11 measured-tree family, chunked bit set, RRB, DABA Lite, priority/interval, ropes, and cursors | [public headers](FingerTree/include/durable7/finger_tree), [API notes](FingerTree/docs/api-notes.md) | `.\build.ps1 -Workspace FingerTree -RunTests`; see [validation](FingerTree/docs/validation.md) and [tests](FingerTree/tests/README.md) |
| [Ordered](Ordered/README.md) | Neutral C17 persistent insertion-ordered set, map, and grouped multimap | [public headers](Ordered/include/durable7/ordered), [API spec](Ordered/docs/api-specification.md), [tests](Ordered/tests/README.md) | `.\build.ps1 -Workspace Ordered -RunTests`; also run Release for native parity work |

`Ordered` composes the public `Hamt` and `FingerTree` libraries and depends on nothing else;
`Hamt` and `FingerTree` are independent of each other.

## Toolchain

MSVC is the reference compiler. `Hamt` and `Ordered` are C17; `FingerTree` is C11. The HAMT build
additionally needs `/experimental:c11atomics`, because MSVC gates `<stdatomic.h>` behind that switch
in its C11/C17 modes.

Clang and GCC are used for portability validation rather than as the primary gate; the workspace
validation guides record the exact portable command lines. The C and C++ roots share the CMake and
Ninja that ship with Visual Studio, and `src/C/Hamt/build.ps1` imports the MSVC environment itself
through [`eng/Import-VisualCppEnvironment.ps1`](../../eng/Import-VisualCppEnvironment.ps1), so a
plain PowerShell prompt is enough — no developer command prompt required.

## Two build models

The root `build.ps1` is a dispatcher over two genuinely different local build systems, which is
worth knowing before reading a build failure:

- **Hamt** has no CMake project. Its `build.ps1` invokes `cl.exe` directly, once per test
  executable, with `/TC /std:c17 /W4 /WX /permissive-` and `-DD7_HAMT_TESTING`. The library is small
  and has no external dependency, so the direct compile stays legible and fast.
- **FingerTree** and **Ordered** are CMake projects with committed presets. `FingerTree` offers
  `msvc-debug`, `msvc-release`, `ninja-debug`, `ninja-release`, and `ninja-asan`; `Ordered` offers
  `msvc-debug` and `msvc-release`. Their tests are registered through
  [`eng/HeadlessTest.cmake`](../../eng/README.md#the-headless-test-contract) rather than bare
  `add_test`, so a crash on an unattended run fails by exit code instead of raising a dialog.

## Running the gate

From `src/C`:

```powershell
.\build.ps1 -Workspace Hamt -RunTests
.\build.ps1 -Workspace FingerTree -Configuration Release -RunTests
.\build.ps1
```

`-Workspace` accepts `All` (the default), `Hamt`, `FingerTree`, or `Ordered`, and takes a list.
`-Configuration` is `Debug` or `Release`. Without `-RunTests` the script builds only.

The root wrapper runs selected workspaces sequentially. Every CMake build preset, CTest preset, and
wrapper invocation fixes its job count at one; the direct HAMT compiler and test steps are already
serial. That is the repository-wide single-worker policy, not a per-workspace preference — parallel
runs make failures harder to attribute and can starve one another on a contended machine.

`-VisualStudioDevCmd`, `-CMake`, and `-CTest` are parameters rather than hard-coded paths, so a
machine with a different Visual Studio layout can be pointed at its own tools without editing the
script.

## Documentation layout

Each workspace keeps its own `docs/` directory: an API specification or API notes for the normative
contract, a usage guide where the ownership rules need worked examples, and a validation guide
recording the exact commands and what each proves. `tests/README.md` maps the executables.

Use the parent [source index](../README.md) for the full language list, the repository
[workspace map](../../docs/reference/workspace-map.md) for cross-language port lineage, the
[semantic contracts reference](../../docs/reference/semantic-contracts.md) when checking shared
persistence, ownership, policy, ordering, or failure-behavior obligations, and the
[build and validation guide](../../docs/guides/build-and-validation.md) for prerequisites and the
portable compiler commands.
