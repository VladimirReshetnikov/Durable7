# Build And Validation Guide

- Created (UTC): 2026-07-02T19:44:02Z
- Repository HEAD: 9bf68f498405e2dce44cb08fad08ea2bbe97d97c
- Audience: Maintainers and AI agents validating repository changes
- Scope: Repository-wide build, test, and generated-cache guidance

Use this guide when a change crosses workspace boundaries, moves files, edits public docs, or touches
shared repository guidance. Workspace-local README files remain the shortest entry point for a single
library; this document is the cross-repository checklist. The workspace validation guides linked below
own the local warning policy, generated-output locations, test coverage map, and evidence wording.
For a cross-workspace view of test runners, local test READMEs, stress knobs, sample smoke tests, and benchmark
boundaries, use the [test suite map](../reference/test-suite-map.md). If you are still deciding which
workspace or evidence boundary applies, start with the
[repository onboarding guide](repository-onboarding.md).

## Validation Matrix

| Workspace | Primary command | Local validation guide | Test map | Coverage |
| --- | --- | --- | --- | --- |
| [C# Numerics](../../src/CSharp/docs/Numerics/overview.md) | `dotnet test .\DataStructures.sln` from `src/CSharp` | [Validation](../../src/CSharp/docs/Numerics/validation.md) | [Tests](../../src/CSharp/tests/Tools.Numerics.Tests/README.md) | .NET library build, XML-doc warning gate, xUnit wide/sparse-integer behavior tests, declaration parity guardrails |
| [C# HAMT](../../src/CSharp/docs/Hamt/overview.md) | `dotnet test .\DataStructures.sln` from `src/CSharp` | [Validation](../../src/CSharp/docs/Hamt/validation.md) | [Tests](../../src/CSharp/tests/Tools.DataStructures.Hamt.Tests/README.md) | .NET library build, XML-doc warning gate, xUnit tests, CsCheck model tests |
| [C# FingerTree](../../src/CSharp/docs/FingerTree/overview.md) | `dotnet test .\DataStructures.sln` from `src/CSharp` | [Validation](../../src/CSharp/docs/FingerTree/validation.md) | [Tests](../../src/CSharp/tests/Tools.DataStructures.FingerTree.Tests/README.md) | .NET library, samples, benchmark project build, stress controls, xUnit/CsCheck suites |
| [C# Wolfram collections](../../src/CSharp/docs/Wolfram/overview.md) | `dotnet test .\DataStructures.sln` from `src/CSharp` | [Validation](../../src/CSharp/docs/Wolfram/validation.md) | [Tests](../../src/CSharp/tests/Tools.DataStructures.Wolfram.Tests/README.md) | .NET library build, XML-doc warning gate, kernel-verified ordering examples, CsCheck ordered-model histories |
| [`src/C/Wolfram`](../../src/C/Wolfram/README.md) | `.\build.ps1 -Workspace Wolfram -RunTests` from `src/C` | [README](../../src/C/Wolfram/README.md) | [Tests](../../src/C/Wolfram/tests/wolfram_c_tests.c) | C17 MSVC Debug/Release CTest executable, list examples, Association ordering rules, custom policies, relabel stress, generated histories |
| [`src/Cpp/Wolfram`](../../src/Cpp/Wolfram/README.md) | `.\build.ps1 -Workspace Wolfram -RunTests` from `src/Cpp` | [README](../../src/Cpp/Wolfram/README.md) | [Tests](../../src/Cpp/Wolfram/tests/wolfram_tests.cpp) | C++23 CTest executable, examples, policy tests, relabel stress, generated histories |
| [`src/C/Hamt`](../../src/C/Hamt/README.md) | `.\build.ps1 -Workspace Hamt -RunTests` from `src/C` | [Validation](../../src/C/Hamt/docs/validation.md) | [Tests](../../src/C/Hamt/tests/README.md) | C17 MSVC, GCC, and Clang builds; warning policy; deterministic HAMT tests |
| [`src/Cpp/Hamt`](../../src/Cpp/Hamt/README.md) | `.\build.ps1 -Workspace Hamt -RunTests` from `src/Cpp` | [Validation](../../src/Cpp/Hamt/docs/validation.md) | [Tests](../../src/Cpp/Hamt/tests/README.md) | C++20 MSVC, GCC, and Clang builds; warning policy; deterministic HAMT tests |
| [`src/Kotlin/Hamt`](../../src/Kotlin/Hamt/README.md) | `.\build.ps1 -Workspace Hamt` from `src/Kotlin` | [Validation](../../src/Kotlin/Hamt/docs/validation.md) | [Tests](../../src/Kotlin/Hamt/tests/README.md) | Kotlin/JVM HAMT build, tool bootstrap, deterministic trie and set-algebra tests |
| [`src/Rust/Hamt`](../../src/Rust/Hamt/README.md) | `cargo test -p tools-data-structures-hamt` | [Validation](../../src/Rust/Hamt/docs/validation.md) | [Tests](../../src/Rust/Hamt/tests/README.md) | Safe Rust crate, structural HAMT tests, collision and set-algebra coverage |
| [`src/C/FingerTree`](../../src/C/FingerTree/README.md) | `.\build.ps1 -Workspace FingerTree -RunTests` from `src/C` | [Validation](../../src/C/FingerTree/docs/validation.md) | [Tests](../../src/C/FingerTree/tests/README.md) | C11 MSVC, GCC, and Clang builds; tests, samples, benchmark harness entry points |
| [`src/Cpp/FingerTree`](../../src/Cpp/FingerTree/README.md) | `.\build.ps1 -Workspace FingerTree -RunTests` from `src/Cpp` | [Validation](../../src/Cpp/FingerTree/docs/validation.md) | [Tests](../../src/Cpp/FingerTree/tests/README.md) | C++23 MSVC, GCC, and Clang CTest lanes; stress controls; benchmark-harness status |
| [`src/Haskell`](../../src/Haskell/README.md) | `cabal test all` | [Haskell README](../../src/Haskell/README.md) | [HAMT tests](../../src/Haskell/Hamt/test/README.md), [FingerTree tests](../../src/Haskell/FingerTree/test/README.md) | GHC/cabal build, dependency-light HAMT and FingerTree executable tests |
| [`src/Kotlin/FingerTree`](../../src/Kotlin/FingerTree/README.md) | `.\build.ps1 -Workspace FingerTree` from `src/Kotlin` | [Validation](../../src/Kotlin/FingerTree/docs/validation.md) | [Tests](../../src/Kotlin/FingerTree/tests/README.md) | Kotlin/JVM semantic-checkpoint tests across deque, reversible deque, measured tree, sorted, priority, interval, rope, and text helpers |
| [`src/Rust/FingerTree`](../../src/Rust/FingerTree/README.md) | `cargo test -p tools-data-structures-fingertree` | [Validation](../../src/Rust/FingerTree/docs/validation.md) | [Tests](../../src/Rust/FingerTree/tests/README.md) | Safe Rust checkpoint crate, structurally shared storage and cached-measure tests across deque, reversible deque, sorted, priority, interval, rope, measured tree, measured rope, and text helpers |
| [`src/Kotlin/Wolfram`](../../src/Kotlin/Wolfram/README.md) | `.\build.ps1 -Workspace Wolfram` from `src/Kotlin` | [README](../../src/Kotlin/Wolfram/README.md) | [Tests](../../src/Kotlin/Wolfram/test/tools/datastructures/wolfram/WolframTests.kt) | Kotlin/JVM executable tests for Wolfram list and association semantics |
| [`src/Rust/Wolfram`](../../src/Rust/Wolfram/README.md) | `cargo test -p tools-data-structures-wolfram` | [README](../../src/Rust/Wolfram/README.md) | [Source tests](../../src/Rust/Wolfram/src/lib.rs) | Safe Rust crate tests for Wolfram list and association semantics |

For broad repository edits, run every row that could be affected. For documentation-only edits, run the
Markdown link check below and any build/test commands whose documented paths changed.

## Native Compiler Policy

For native C and C++ source, header, test, sample, benchmark, or validation-documentation changes, compile and
run tests with every supported compiler lane documented for the affected workspace. Do not treat a successful
compile as enough: each lane must run the executable or CTest suite produced by that same compiler and output
directory.

The current Windows compiler set is:

- MSVC through the Visual Studio developer environment and the language-root `build.ps1` wrappers.
- LLVM/Clang through `C:\Program Files\LLVM\bin\clang.exe` or `clang++.exe`, with the Visual Studio developer
  environment when targeting the MSVC ABI.
- GCC/MinGW through WinLibs, normally under
  `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin`.

Workspace-local validation guides define which of those lanes are mandatory today and provide exact commands.
When a compiler is installed but a workspace guide marks that lane as provisional or blocked, record the attempted
command and failure mode instead of silently omitting it.

## C# Workspace

```powershell
cd C:\DataStructures\src\CSharp
dotnet test .\DataStructures.sln
```

The C# solution targets `net10.0` and uses C# preview features. Treat public XML documentation warnings
as build-relevant: `CS1591` and `CS1573` are intentionally escalated in the C# workspace. The local
validation guides define family-specific coverage and optional stress/benchmark boundaries:

- [C# Numerics validation](../../src/CSharp/docs/Numerics/validation.md)
- [C# Numerics tests](../../src/CSharp/tests/Tools.Numerics.Tests/README.md)
- [C# HAMT validation](../../src/CSharp/docs/Hamt/validation.md)
- [C# HAMT tests](../../src/CSharp/tests/Tools.DataStructures.Hamt.Tests/README.md)
- [C# FingerTree validation](../../src/CSharp/docs/FingerTree/validation.md)
- [C# FingerTree tests](../../src/CSharp/tests/Tools.DataStructures.FingerTree.Tests/README.md)
- [C# Wolfram collections validation](../../src/CSharp/docs/Wolfram/validation.md)
- [C# Wolfram collections tests](../../src/CSharp/tests/Tools.DataStructures.Wolfram.Tests/README.md)

## Rust Workspaces

```powershell
cd C:\DataStructures\src\Rust
cargo test --workspace
```

The Rust crates are safe Rust only (`#![forbid(unsafe_code)]`) and are validated through Cargo unit tests. If
Cargo is installed under the default rustup profile but is not on `PATH`, use the explicit local tool:

```powershell
cd C:\DataStructures\src\Rust
& $env:USERPROFILE\.cargo\bin\cargo.exe test --workspace
```

Local guides:

- [Rust HAMT validation](../../src/Rust/Hamt/docs/validation.md)
- [Rust HAMT tests](../../src/Rust/Hamt/tests/README.md)
- [Rust FingerTree validation](../../src/Rust/FingerTree/docs/validation.md)
- [Rust FingerTree tests](../../src/Rust/FingerTree/tests/README.md)
- [Rust Wolfram README](../../src/Rust/Wolfram/README.md)

## HAMT Native Ports

```powershell
cd C:\DataStructures\src\C
.\build.ps1 -Workspace Hamt -RunTests
.\build.ps1 -Workspace Hamt -Configuration Release -RunTests

cd C:\DataStructures\src\Cpp
.\build.ps1 -Workspace Hamt -RunTests
.\build.ps1 -Workspace Hamt -Configuration Release -RunTests
```

The language-root scripts delegate to the HAMT native build scripts, which import the MSVC environment through
Scriptorium. Build outputs are written
under `build/<Configuration>/` and are ignored by the repository. The local guides define compiler flags,
warning policy, generated outputs, and native model-test coverage:

- [C HAMT validation](../../src/C/Hamt/docs/validation.md)
- [C HAMT tests](../../src/C/Hamt/tests/README.md)
- [C++ HAMT validation](../../src/Cpp/Hamt/docs/validation.md)
- [C++ HAMT tests](../../src/Cpp/Hamt/tests/README.md)

For C or C++ HAMT source, header, test, or behavior-documentation changes, also run strict warning builds with
GCC and Clang and execute each compiler's produced test binary. Use the workspace validation guides for exact
commands; a typical Windows direct lane uses the installed compiler paths explicitly when the current shell has
not reloaded `PATH`.

## Wolfram Native Ports

```powershell
cd C:\DataStructures\src\C
.\build.ps1 -Workspace Wolfram -RunTests
.\build.ps1 -Workspace Wolfram -Configuration Release -RunTests

cd C:\DataStructures\src\Cpp
.\build.ps1 -Workspace Wolfram -RunTests
```

The Wolfram native workspaces use CMake presets and CTest. The C port links the existing C HAMT and
C FingerTree implementations and validates both Debug and Release MSVC lanes for parity-sensitive changes.
The C++ port is header-first and shares the C++ HAMT/FingerTree substrates.

## FingerTree Native Ports

The C and C++ language-root scripts can build the FingerTree workspaces through their CMake presets:

```powershell
cd C:\DataStructures\src\C
.\build.ps1 -Workspace FingerTree -RunTests

cd C:\DataStructures\src\Cpp
.\build.ps1 -Workspace FingerTree -RunTests
```

The FingerTree C and C++ workspaces use CMake presets with Visual Studio's bundled Ninja. The C++ workspace
models the target as `CXX_STANDARD 23` and adds MSVC `/std:c++latest` explicitly. A plain PowerShell invocation
of `VsDevCmd.bat` does not persist environment changes in the current PowerShell process, so use a single
`cmd.exe` chain when starting from an uninitialized shell.

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

cd C:\DataStructures\src\C\FingerTree
cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmake"" --preset msvc-debug && ""$cmake"" --build --preset msvc-debug && ""$ctest"" --preset msvc-debug --output-on-failure"

cd C:\DataStructures\src\Cpp\FingerTree
cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmake"" --preset msvc-debug && ""$cmake"" --build --preset msvc-debug && ""$ctest"" --preset msvc-debug --output-on-failure"
```

Use `msvc-release` for optimized validation:

```powershell
cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmake"" --preset msvc-release && ""$cmake"" --build --preset msvc-release && ""$ctest"" --preset msvc-release --output-on-failure"
```

If a workspace was moved after CMake had already configured it, remove only the stale generated preset
directory before reconfiguring. Verify the resolved target is inside the workspace first.

```powershell
Remove-Item -LiteralPath .\out\build\msvc-debug -Recurse -Force
Remove-Item -LiteralPath .\out\build\msvc-release -Recurse -Force
```

The native FingerTree validation guides describe local CMake presets, stress controls, sample smoke
tests, and benchmark harness status:

- [C FingerTree validation](../../src/C/FingerTree/docs/validation.md)
- [C FingerTree tests](../../src/C/FingerTree/tests/README.md)
- [C FingerTree samples](../../src/C/FingerTree/samples/README.md)
- [C FingerTree benchmarks](../../src/C/FingerTree/benchmarks/README.md)
- [C++ FingerTree validation](../../src/Cpp/FingerTree/docs/validation.md)
- [C++ FingerTree tests](../../src/Cpp/FingerTree/tests/README.md)

For C or C++ FingerTree source, header, test, sample, benchmark, or behavior-documentation changes, run the MSVC
Debug/Release commands above and run separate GCC and Clang CMake build directories, followed by CTest in each
directory. Do not reuse the MSVC `out/build/msvc-*` binaries as evidence for GCC or Clang.

Both FingerTree native workspaces also provide host-agnostic `ninja-debug`, `ninja-release`, and `ninja-asan`
presets. On hosts with `cmake`, `ninja`, and a GCC/Clang-style sanitizer-capable compiler on `PATH`, use
`ninja-asan` to catch lifetime and undefined-behavior issues:

```powershell
cd C:\DataStructures\src\C\FingerTree
cmake --preset ninja-asan
cmake --build --preset ninja-asan
ctest --preset ninja-asan --output-on-failure

cd C:\DataStructures\src\Cpp\FingerTree
cmake --preset ninja-asan
cmake --build --preset ninja-asan
ctest --preset ninja-asan --output-on-failure
```

For C++ FingerTree, the workspace validation guide owns the exact MSVC, GCC, and Clang commands. On Windows, run
Clang's MSVC-targeting `clang++.exe` from a Visual Studio developer environment and keep every compiler/configuration
pair in its own `out/build/<compiler>-<configuration>` directory.

## Haskell Workspaces

```powershell
cd C:\DataStructures\src\Haskell
cabal test all
```

The cabal project builds both Haskell packages and runs the dependency-light test executables:

- [Haskell HAMT tests](../../src/Haskell/Hamt/test/README.md)
- [Haskell FingerTree tests](../../src/Haskell/FingerTree/test/README.md)
- [Haskell Wolfram README](../../src/Haskell/Wolfram/README.md)

## Kotlin Workspaces

```powershell
cd C:\DataStructures\src\Kotlin
.\build.ps1
.\build.ps1 -Workspace Hamt
.\build.ps1 -Workspace FingerTree
.\build.ps1 -Workspace Wolfram
```

The Kotlin build script compiles each workspace with the Kotlin command-line compiler and runs
dependency-free executable tests. If no Java 21+ runtime is on `PATH` on Windows, it bootstraps a local Temurin
JDK 21 under `src/Kotlin/build/tools`; on non-Windows hosts, provide Java 21+ through `PATH` or `JAVA_HOME`.
It also downloads and verifies the Kotlin compiler archive.
Local guides:

- [Kotlin HAMT validation](../../src/Kotlin/Hamt/docs/validation.md)
- [Kotlin HAMT tests](../../src/Kotlin/Hamt/tests/README.md)
- [Kotlin FingerTree validation](../../src/Kotlin/FingerTree/docs/validation.md)
- [Kotlin FingerTree tests](../../src/Kotlin/FingerTree/tests/README.md)
- [Kotlin Wolfram README](../../src/Kotlin/Wolfram/README.md)

## Benchmarks

Benchmarks are not part of routine validation. Run them when changing complexity-sensitive code, public
performance claims, or benchmark documentation.

```powershell
cd C:\DataStructures\src\CSharp\benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release -- --filter * --job short
```

Release configuration is required for meaningful benchmark numbers.

## Documentation Checks

Use `rg` for stale path and accidental-rewrite scans. This current-state scan excludes migration
provenance, where old extraction paths are intentional historical evidence:

```powershell
rg -n "C:\\DataStructures\\(Hamt|HamtC|HamtCpp|FingerTree|C\\FingerTree|Cpp\\FingerTree)|sr[s]rc|iladimi[r]|T[i]alue|MS[i]C|[i]ersion|docs/agent-workflows\\.md" README.md docs src --glob "!docs/migration/**" --glob "!src/CSharp/docs/FingerTree/external/**" --glob "!*.pdf"
```

For repository-owned Markdown links:

```powershell
$root = (Resolve-Path .).Path
$files = rg --files -g '*.md' --glob '!src/CSharp/docs/FingerTree/external/**'
$missing = New-Object System.Collections.Generic.List[string]
$linkPattern = '!{0,1}\[[^\]]*\]\((?<target>[^)]+)\)'
foreach ($file in $files) {
    $full = Join-Path $root $file
    $text = [System.IO.File]::ReadAllText($full)
    foreach ($m in [regex]::Matches($text, $linkPattern)) {
        $target = $m.Groups['target'].Value.Trim()
        if ($target.StartsWith('<') -and $target.EndsWith('>')) { $target = $target.Substring(1, $target.Length - 2) }
        if ($target -match '^(https?|mailto|app|file)://' -or $target -match '^(https?|mailto|app|file):' -or $target.StartsWith('//') -or $target.StartsWith('#') -or [string]::IsNullOrWhiteSpace($target)) { continue }
        $target = ($target -split '#',2)[0]
        if ($target -match '^(?<path>.+\.(md|cs|hpp|h|c|cpp|ps1|txt|tex|pdf|sln|csproj|tsv)):\d+(-\d+)?$') { $target = $Matches['path'] }
        if ([string]::IsNullOrWhiteSpace($target)) { continue }
        $target = [System.Uri]::UnescapeDataString($target)
        $candidate = if ([System.IO.Path]::IsPathRooted($target)) { $target } else { Join-Path (Split-Path $full -Parent) $target }
        if (-not (Test-Path -LiteralPath $candidate)) {
            $line = ($text.Substring(0, $m.Index) -split "`n").Count
            $missing.Add("${file}:$line -> $target")
        }
    }
}
if ($missing.Count -gt 0) { $missing | Sort-Object; exit 1 }
'All repository-owned Markdown links resolve.'
```

Finish with:

```powershell
git diff --check
```
