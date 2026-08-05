# C++ Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Updated (UTC): 2026-07-27T17:20:00Z
- Repository HEAD: dc8c8f3fa715a02db94a5a9ef3347baeac0b70a0
- Audience: Maintainers and AI agents working in the C++ source root
- Scope: C++ data-structure workspaces under `src/Cpp`

The C++ root contains value-semantics ports of repository-owned persistent data structures. A
persistent collection here is an ordinary copyable value: copying one is cheap because the copy
shares immutable nodes, and every mutating-looking operation returns a new value instead of editing
the receiver. Hash and comparison behavior arrives as template policy parameters rather than runtime
callbacks, so the compiler can inline it — the main representational difference from the C port,
which pays for type erasure at run time.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Hamt](Hamt/README.md) | C++20 CHAMP collections, strict patches, directed graphs, indexed maps, Patricia, exact-wire Merkle, and the ancestral connection forest | [aggregate header](Hamt/include/durable7/hamt/hamt.hpp), [API spec](Hamt/docs/api-specification.md) | `.\build.ps1 -Workspace Hamt -RunTests`; see [validation](Hamt/docs/validation.md) and [tests](Hamt/tests/README.md) |
| [FingerTree](FingerTree/README.md) | C++23 measured-tree family, chunked bit set, RRB, DABA Lite, priority/interval, ropes, cursors, the level-ancestor seam, and six of the seven research-derived collections | [aggregate header](FingerTree/include/durable7/finger_tree/finger_tree.hpp), [API notes](FingerTree/docs/api-notes.md) | `.\build.ps1 -Workspace FingerTree -RunTests`; see [validation](FingerTree/docs/validation.md) and [tests](FingerTree/tests/README.md) |
| [Ordered](Ordered/README.md) | Neutral C++23 insertion-ordered set, map, and grouped multimap | [aggregate header](Ordered/include/durable7/ordered/ordered.hpp), [API notes](Ordered/docs/api-notes.md) | `.\build.ps1 -Workspace Ordered -RunTests`; see [validation](Ordered/docs/validation.md) and [tests](Ordered/tests/README.md) |

The seven research-derived collections ship here too, six in FingerTree and the ancestral connection
forest in Hamt. C++ is the port where the policy regime pays off most visibly: where the managed
reference injects a level-ancestor backend, an action algebra, a comparer, and an event machine as
runtime objects, this workspace takes all four as compile-time template parameters constrained by
concepts. The consequence is not merely inlining — the reference's runtime policy-identity gates
(mismatched comparers, melded heaps with different policies, concatenated sequences with different
machines) stop being throwing checks and become compile errors. It is also the only port besides
Haskell whose measured substrate is a genuine Hinze–Paterson finger tree, so its contextual rank
sequence keeps the reference's endpoint and concatenation bounds where the Rust and Kotlin ports
must weaken them.

All three are header-only: there is no compiled library to link, only an include directory and an
aggregate header per family. `Ordered` includes the public `Hamt` and `FingerTree` headers and
nothing else from this repository, and it reaches into neither substrate's `detail` namespace.

## Toolchain

MSVC is the reference compiler. `Hamt` is C++20; `FingerTree` and `Ordered` require C++23 and are
compiled with `/std:c++latest`. Clang and GCC are used for portability validation, and both appear
in CI — see [continuous integration](../../docs/guides/build-and-validation.md#continuous-integration),
which covers `src/Cpp/FingerTree` and no other workspace in the repository.

`src/Cpp/Hamt/build.ps1` imports the MSVC environment itself through
[`eng/Import-VisualCppEnvironment.ps1`](../../eng/Import-VisualCppEnvironment.ps1), so a plain
PowerShell prompt suffices.

## Two build models

The root `build.ps1` dispatches to two different local build systems:

- **Hamt** has no CMake project. Its `build.ps1` drives `cl.exe` directly with
  `/std:c++20 /W4 /WX /permissive-`, once per test program, linking `bcrypt.lib` for the platform
  SHA-256 the Merkle family needs. Header-only and otherwise dependency-free, it does not need more.
- **FingerTree** and **Ordered** are CMake projects with committed presets. `FingerTree` offers
  `msvc-debug`, `msvc-release`, `ninja-debug`, `ninja-release`, `ninja-asan`, and `ninja-tsan`;
  `Ordered` offers the same minus `ninja-tsan`. Their tests are registered through
  [`eng/HeadlessTest.cmake`](../../eng/README.md#the-headless-test-contract) rather than bare
  `add_test`, so an unattended crash fails by exit code instead of raising a modal dialog — and so
  a Clang ASan build gets the compiler-matched sanitizer runtime copied beside it rather than
  binding whatever Visual Studio put on `PATH`.

`FingerTree` also carries `benchmarks/` and `samples/` targets. Benchmarks are outside the
correctness gate; see [benchmarks](../../docs/guides/build-and-validation.md#benchmarks).

## Running the gate

From `src/Cpp`:

```powershell
.\build.ps1 -Workspace Hamt -RunTests
.\build.ps1 -Workspace FingerTree -Configuration Release -RunTests
.\build.ps1
```

`-Workspace` accepts `All` (the default), `Hamt`, `FingerTree`, or `Ordered`, and takes a list.
`-Configuration` is `Debug` or `Release`. Without `-RunTests` the script builds only.

The root wrapper runs selected workspaces sequentially. Every CMake build preset, CTest preset, and
wrapper invocation fixes its job count at one; the direct HAMT compiler and test steps are already
serial. That is the repository-wide single-worker policy rather than a per-workspace preference.

`-VisualStudioDevCmd`, `-CMake`, and `-CTest` are parameters rather than hard-coded paths, so a
machine with a different Visual Studio layout can be pointed at its own tools without editing the
script.

## Documentation layout

Each workspace keeps its own `docs/` directory: an API specification or API notes for the normative
contract, a usage guide, and a validation guide recording the exact commands and what each proves.
`FingerTree/docs` additionally retains the port plan and the dated port-review reports, which are
historical records of how the port was carried over from C# rather than current-state contracts.

Use the parent [source index](../README.md) for the full language list, the repository
[workspace map](../../docs/reference/workspace-map.md) for cross-language port lineage, the
[semantic contracts reference](../../docs/reference/semantic-contracts.md) when checking shared
persistence, ownership, policy, ordering, or failure-behavior obligations, and the
[build and validation guide](../../docs/guides/build-and-validation.md) for prerequisites and the
portable compiler commands.
