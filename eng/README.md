# Engineering Tooling

- Created (UTC): 2026-07-27T16:55:51Z
- Repository HEAD: de0e88f1b62e093a9168f0c70f929ed7b00691c3
- Audience: Maintainers and AI agents changing build, test, or document-generation tooling
- Scope: The shared scripts and CMake module under `eng/`

`eng/` holds the tooling that is shared across language roots rather than owned by one of them.
Nothing here is part of a shipped library; nothing here is language-specific. A file belongs in
`eng/` when at least two workspaces need it, or when it exists to make an unattended run behave.

| File | Role |
| --- | --- |
| [`Enable-HeadlessTestMode.ps1`](Enable-HeadlessTestMode.ps1) | Dot-sourced helper defining `Enable-HeadlessTestMode`, which suppresses Windows crash dialogs for the current process and its children |
| [`Invoke-HeadlessTest.ps1`](Invoke-HeadlessTest.ps1) | Runs one test executable under that error mode and propagates its exit code |
| [`Invoke-HeadlessCommandTest.ps1`](Invoke-HeadlessCommandTest.ps1) | Same contract, but reads the executable and arguments from a file so CMake can hand over a command too long or too quoted for a command line |
| [`HeadlessTest.cmake`](HeadlessTest.cmake) | `d7_add_headless_test` / `d7_add_headless_command_test`, the CTest registration functions every native workspace uses instead of bare `add_test` |
| [`Import-VisualCppEnvironment.ps1`](Import-VisualCppEnvironment.ps1) | Imports an MSVC `vcvars` environment into the current PowerShell process so `cl.exe` works without a developer command prompt |
| [`Build-LatexDoc.ps1`](Build-LatexDoc.ps1) | Generic N-pass LaTeX builder used by the workspace-local document build scripts |

## The headless-test contract

An unattended test run must fail by exit code. On Windows the default is worse than that: a crash,
a failed DLL load, a CRT assertion, or an invalid-parameter trap raises a modal dialog and the
process waits for a click that never comes. A CI job or an agent-driven gate then hangs until it
times out, and the log says nothing useful.

Three layers cooperate to prevent that, and a native test needs all three because each covers a
window the others cannot reach.

**1. The process error mode, set before the child starts.** `Enable-HeadlessTestMode` clears
`SEM_FAILCRITICALERRORS`, `SEM_NOGPFAULTERRORBOX`, and `SEM_NOOPENFILEERRORBOX` via `SetErrorMode`,
returning the previous mode so a caller can restore it. These flags are inherited, which is the
whole point: they are already in effect when the loader runs, so a missing DLL fails as a console
diagnostic *before* the test binary reaches `main` and could configure anything itself. Every
language-root entry point dot-sources this file first — `src/CSharp/test.ps1`, `src/Haskell/test.ps1`,
`src/Kotlin/build.ps1`, `src/OCaml/test.ps1`, `src/Python/test.ps1`, `src/Rust/test.ps1`,
`src/TypeScript/test.ps1`, and both native `build.ps1` wrappers.

On a non-Windows host the function reports `IsWindows = $false` and does nothing, so the same
scripts run unchanged on Linux and macOS.

**2. The CTest wrapper.** `ctest` starts test executables itself, so the error mode has to be
re-established between CTest and the binary. On Windows, `d7_add_headless_test` registers the test
as `pwsh -File Invoke-HeadlessTest.ps1 <target-exe> <args>` rather than as the executable directly;
on other platforms it registers the executable with no wrapper. `d7_add_headless_command_test` is
the variant for a command that is not a CMake target — it writes the argument vector to a generated
file, one element per line, and passes the file path, which sidesteps every quoting rule between
CMake, CTest, and PowerShell. Arguments containing newlines are rejected at configure time because
the file format could not represent them.

`d7_add_headless_test` also puts [`src/test_support/include`](../src/test_support/include) on the
target's private include path, so a test can include the header described below without repeating
the path, and it copies the compiler-matched AddressSanitizer runtime next to a Clang ASan binary.
That last step is not incidental: Visual Studio ships an independently versioned DLL with the same
basename, and without the copy the loader can bind the wrong one before `main`.

**3. The in-process header.**
[`durable7/test_support/headless_test_process.h`](../src/test_support/include/durable7/test_support/headless_test_process.h)
is a header-only C shim that each native test's `main` calls first. `d7_enter_headless_test_process`
re-asserts the error mode and additionally redirects the MSVC CRT's `_CRT_WARN`, `_CRT_ERROR`, and
`_CRT_ASSERT` reports to stderr, forces `_set_error_mode(_OUT_TO_STDERR)`, clears the abort
behavior that would summon Windows Error Reporting, and installs an invalid-parameter handler that
prints and aborts instead of trapping. It then *verifies* the configuration took effect and returns
0 if it did not, so a test cannot silently run in dialog-raising mode. The redundancy with layer 1
is intentional: the header also protects a binary run directly from a debugger or an IDE, where no
PowerShell wrapper is involved.

Twenty CTest registrations across the C and C++ workspaces go through these functions. Use them for
any new native test rather than `add_test`.

## Importing the MSVC toolchain

`src/C/Hamt/build.ps1` and `src/Cpp/Hamt/build.ps1` compile with `cl.exe` directly instead of going
through CMake, so they need the Visual C++ environment variables that a developer command prompt
would have set. `Import-VisualCppEnvironment.ps1` locates the newest Visual Studio installation
carrying the requested toolset component through `vswhere.exe`, runs the matching `vcvars` batch
file in a `cmd.exe` child, and copies the resulting variables into the current PowerShell process.

It works both as a one-shot import and as a dot-sourced function:

```powershell
& eng\Import-VisualCppEnvironment.ps1 -Architecture x64
```

```powershell
. eng\Import-VisualCppEnvironment.ps1
Import-VisualCppEnvironment -Architecture x64_arm64 -IncludePrerelease -Force
```

The import is idempotent — it returns immediately when `INCLUDE` and `LIB` are already set — so
`-Force` is what you want when switching target architecture inside one session. Pass
`-IncludePrerelease` when the only installed toolset lives in a Preview or Insiders installation.
Supported architectures are `x64` (default), `x86`, `arm64`, `x64_x86`, and `x64_arm64`.

## Building a LaTeX document

`Build-LatexDoc.ps1` takes a `.tex` path and runs an engine over it a fixed number of times,
because a table of contents and cross-references do not settle in one pass. It warns when the log
still reports undefined references, deletes the byproducts (`.aux`, `.log`, `.out`, `.toc`) unless
`-KeepAux` is given, and reports the size of the resulting PDF. If the chosen engine is not on
`PATH` it falls back to the default MiKTeX install location before failing with an installation
hint.

```powershell
pwsh eng\Build-LatexDoc.ps1 path\to\doc.tex
pwsh eng\Build-LatexDoc.ps1 path\to\doc.tex -Engine pdflatex -Passes 3 -KeepAux
```

Defaults are `lualatex` and two passes. Keep document-specific choices in a thin wrapper next to the
document rather than in this script;
[`src/CSharp/docs/FingerTree/build-design-notes.ps1`](../src/CSharp/docs/FingerTree/build-design-notes.ps1)
is the example to copy.

This builder does not run `makeindex`, so it cannot produce the
[field guide](../docs/book/README.md), whose index requires that pass. Build that document with the
explicit sequence recorded in its own README.

## Conventions

- Scripts set `Set-StrictMode -Version Latest` and `$ErrorActionPreference = 'Stop'`, and check
  `$LASTEXITCODE` explicitly where a native tool's exit code matters.
- A test process that ends without reporting an exit code is treated as a failure, never as a pass.
- Every file carries an `SPDX-License-Identifier: MIT-0` marker and comment-based help.
- Anything workspace-specific belongs in that workspace's `build.ps1` or `test.ps1`, not here.

## Related documentation

- [Build and validation guide](../docs/guides/build-and-validation.md) — the repository-wide
  validation matrix, prerequisites, single-worker policy, and the non-interactive failure policy
  these scripts implement.
- [Test suite map](../docs/reference/test-suite-map.md) — which suite each workspace runs and what
  each command proves.
- [Continuous integration](../docs/guides/build-and-validation.md#continuous-integration) — the one
  GitHub Actions workflow, which reuses `Enable-HeadlessTestMode.ps1` directly.
