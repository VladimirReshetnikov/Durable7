# C# Numerics Tests

- Created (UTC): 2026-07-03T17:07:42Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Audience: Maintainers validating the C# Numerics workspace
- Scope: xUnit test project under `src/CSharp/tests/Tools.Numerics.Tests`

`Tools.Numerics.Tests` targets the workspace defaults from `Directory.Build.props`, references the public
`Tools.Numerics` project, and uses xUnit, `Microsoft.NET.Test.Sdk`, and `xunit.runner.visualstudio`.

From `src/CSharp`, run:

```powershell
.\test.ps1
```

The launcher suppresses modal Windows loader/crash reporting for the complete `dotnet` child-process tree. The
test assembly repeats the headless process configuration during module initialization, so direct test-runner and
Test Explorer execution is non-interactive after the assembly loads as well.

This test project is organized by **bit width** and **signedness quadrant** so counterpart coverage is easy to compare.

## Directory layout

- `BitWidth256/`
  - `Unsigned/` — `UInt256` primary behavior suites.
  - `Signed/` — `Int256` primary behavior suites.
  - `Shared/` — 256-bit width-wide helpers and APIs shared by signed/unsigned types (for example `BitConverterEx`).
- `BitWidth512/`
  - `Unsigned/` — `UInt512` primary behavior suites.
  - `Signed/` — `Int512` primary behavior suites.
  - `Shared/` — 512-bit width-wide helpers and APIs shared by signed/unsigned types.
- `BitWidth1024/`
  - `Unsigned/` — `UInt1024` primary behavior suites.
  - `Signed/` — `Int1024` primary behavior suites.
  - `Shared/` — 1024-bit width-wide helpers and APIs shared by signed/unsigned types.
- `MixedScenarios/`
  - Tests that intentionally span quadrants (dynamic runtime binding, cross-type comparison contracts, edge behaviors exercised across multiple types, and broad API-surface coverage).
  - `SparseIntegerTests.cs` covers the sparse non-negative integer representation, including conversion, comparison, sparse powers, arithmetic, and invalid inputs.
  - `WideIntegerNumberStylesTests.cs` covers style combinations, precision/grouping formats, redundant hexadecimal
    zeroes, and generic parsing/min-max interface conformance across all six fixed-width types.
  - `WideIntegerConversionParityTests.cs` covers binary/decimal floating-point conversions and the direct
    512↔128 and 1024↔256/128 conversion matrix.
  - `WideIntegerAlgorithmTests.cs` differentially checks normalized limb division at every width, verifies warmed
    UTF-16/UTF-8 span formatting is allocation-free for representative decimal, grouped, and hexadecimal formats,
    and stresses dense `SparseInteger` carry propagation.
  - Mixed tests are explicitly labeled in class/method names and XML docs so asymmetry is intentional and discoverable.
- `Infrastructure/`
  - Shared deterministic test helpers (random generators, normalization helpers, and reference-model utilities).

## Ordering convention

Within each quadrant-specific file, tests should follow roughly the same conceptual order:

1. constants and constructors,
2. arithmetic and checked/unchecked overflow behavior,
3. bitwise/shift/rotate behavior,
4. parsing and formatting,
5. conversions and interoperability,
6. width-specific edge contracts.

This mirrored order makes it straightforward to diff `Int256` vs `Int512` vs `Int1024` (or `UInt256` vs `UInt512` vs `UInt1024`) and see where asymmetry is expected (for example unary negation not being applicable to unsigned types).
