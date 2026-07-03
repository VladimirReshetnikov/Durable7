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
boundaries, use the [test suite map](../reference/test-suite-map.md).

## Validation Matrix

| Workspace | Primary command | Local validation guide | Test map | Coverage |
| --- | --- | --- | --- | --- |
| [`src/CSharp/Hamt`](../../src/CSharp/Hamt/README.md) | `dotnet test .\Hamt.sln` | [Validation](../../src/CSharp/Hamt/docs/validation.md) | [Tests](../../src/CSharp/Hamt/tests/Tools.DataStructures.Hamt.Tests/README.md) | .NET library build, XML-doc warning gate, xUnit tests, CsCheck model tests |
| [`src/CSharp/FingerTree`](../../src/CSharp/FingerTree/README.md) | `dotnet test .\FingerTree.sln` | [Validation](../../src/CSharp/FingerTree/docs/validation.md) | [Tests](../../src/CSharp/FingerTree/tests/Tools.DataStructures.FingerTree.Tests/README.md) | .NET library, samples, benchmark project build, stress controls, xUnit/CsCheck suites |
| [`src/C/Hamt`](../../src/C/Hamt/README.md) | `.\build.ps1 -RunTests` | [Validation](../../src/C/Hamt/docs/validation.md) | [Tests](../../src/C/Hamt/tests/README.md) | C17 build, warning policy, deterministic HAMT tests |
| [`src/Cpp/Hamt`](../../src/Cpp/Hamt/README.md) | `.\build.ps1 -RunTests` | [Validation](../../src/Cpp/Hamt/docs/validation.md) | [Tests](../../src/Cpp/Hamt/tests/README.md) | C++20 build, warning policy, deterministic HAMT tests |
| [`src/Rust/Hamt`](../../src/Rust/Hamt/README.md) | `cargo test -p tools-data-structures-hamt` | [Validation](../../src/Rust/Hamt/docs/validation.md) | [Tests](../../src/Rust/Hamt/tests/README.md) | Safe Rust crate, structural HAMT tests, collision and set-algebra coverage |
| [`src/C/FingerTree`](../../src/C/FingerTree/README.md) | Visual Studio `cmd.exe` CMake/CTest chain below | [Validation](../../src/C/FingerTree/docs/validation.md) | [Tests](../../src/C/FingerTree/tests/README.md) | C11 static library, tests, samples, benchmark harness entry points |
| [`src/Cpp/FingerTree`](../../src/Cpp/FingerTree/README.md) | Visual Studio `cmd.exe` CMake/CTest chain below | [Validation](../../src/Cpp/FingerTree/docs/validation.md) | [Tests](../../src/Cpp/FingerTree/tests/README.md) | C++23 header-first library, CTest suite, stress controls, benchmark-harness status |
| [`src/Haskell`](../../src/Haskell/README.md) | `cabal test all` | [Haskell README](../../src/Haskell/README.md) | [HAMT tests](../../src/Haskell/Hamt/test/README.md), [FingerTree tests](../../src/Haskell/FingerTree/test/README.md) | GHC/cabal build, dependency-light HAMT and FingerTree executable tests |
| [`src/Rust/FingerTree`](../../src/Rust/FingerTree/README.md) | `cargo test -p tools-data-structures-fingertree` | [Validation](../../src/Rust/FingerTree/docs/validation.md) | [Tests](../../src/Rust/FingerTree/tests/README.md) | Safe Rust checkpoint crate, persistent facade tests across deque, measured sequence, sorted, priority, interval, rope, and text helpers |

For broad repository edits, run every row that could be affected. For documentation-only edits, run the
Markdown link check below and any build/test commands whose documented paths changed.

## C# Workspaces

```powershell
cd C:\DataStructures\src\CSharp\Hamt
dotnet test .\Hamt.sln

cd C:\DataStructures\src\CSharp\FingerTree
dotnet test .\FingerTree.sln
```

The C# solutions target `net10.0` and use C# preview features. Treat public XML documentation warnings
as build-relevant: `CS1591` and `CS1573` are intentionally escalated in the C# workspaces. The local
validation guides define test coverage and optional stress/benchmark boundaries:

- [C# HAMT validation](../../src/CSharp/Hamt/docs/validation.md)
- [C# HAMT tests](../../src/CSharp/Hamt/tests/Tools.DataStructures.Hamt.Tests/README.md)
- [C# FingerTree validation](../../src/CSharp/FingerTree/docs/validation.md)
- [C# FingerTree tests](../../src/CSharp/FingerTree/tests/Tools.DataStructures.FingerTree.Tests/README.md)

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

## HAMT Native Ports

```powershell
cd C:\DataStructures\src\C\Hamt
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests

cd C:\DataStructures\src\Cpp\Hamt
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests
```

The HAMT native build scripts import the MSVC environment through Scriptorium. Build outputs are written
under `build/<Configuration>/` and are ignored by the repository. The local guides define compiler flags,
warning policy, generated outputs, and native model-test coverage:

- [C HAMT validation](../../src/C/Hamt/docs/validation.md)
- [C HAMT tests](../../src/C/Hamt/tests/README.md)
- [C++ HAMT validation](../../src/Cpp/Hamt/docs/validation.md)
- [C++ HAMT tests](../../src/Cpp/Hamt/tests/README.md)

## FingerTree Native Ports

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

## Haskell Workspaces

```powershell
cd C:\DataStructures\src\Haskell
cabal test all
```

The cabal project builds both Haskell packages and runs the dependency-light test executables:

- [Haskell HAMT tests](../../src/Haskell/Hamt/test/README.md)
- [Haskell FingerTree tests](../../src/Haskell/FingerTree/test/README.md)

## Benchmarks

Benchmarks are not part of routine validation. Run them when changing complexity-sensitive code, public
performance claims, or benchmark documentation.

```powershell
cd C:\DataStructures\src\CSharp\FingerTree\benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release -- --filter * --job short
```

Release configuration is required for meaningful benchmark numbers.

## Documentation Checks

Use `rg` for stale path and accidental-rewrite scans. This current-state scan excludes migration
provenance, where old extraction paths are intentional historical evidence:

```powershell
rg -n "C:\\DataStructures\\(Hamt|HamtC|HamtCpp|FingerTree|C\\FingerTree|Cpp\\FingerTree)|sr[s]rc|src[/\\]src|iladimi[r]|T[i]alue|MS[i]C|[i]ersion|docs/agent-workflows\\.md" README.md docs src --glob "!docs/migration/**" --glob "!src/CSharp/FingerTree/docs/external/**" --glob "!*.pdf"
```

For repository-owned Markdown links:

```powershell
$root = (Resolve-Path .).Path
$files = rg --files -g '*.md' --glob '!src/CSharp/FingerTree/docs/external/**'
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
