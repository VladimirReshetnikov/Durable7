# C# Numerics Validation

- Status: Current validation guide
- Created (UTC): 2026-07-03T17:07:42Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Audience: Maintainers validating the C# Numerics workspace
- Scope: Local restore, build, test, warning-policy, and coverage guidance for `src/CSharp/src/Tools.Numerics`

Use this guide when changing `Tools.Numerics` source, tests, documentation, or project configuration. Pair it with
the [API and behavior reference](api-and-behavior-reference.md) for public semantics and the
[wide-integer maintainer guidance](wide-integer-maintainer-guidance.md) for cross-width implementation checks.

## Build Model

`DataStructures.sln` contains:

- `src/Tools.Numerics/Tools.Numerics.csproj`, the public library.
- `tests/Tools.Numerics.Tests/Tools.Numerics.Tests.csproj`, the xUnit test project.

`Directory.Build.props` applies the workspace defaults:

- Target framework: `net10.0`.
- Language version: C# `preview`.
- Nullable annotations and implicit usings enabled.
- Unsafe blocks enabled for allocation-conscious numeric implementation paths.
- XML documentation generation enabled.
- Public XML documentation warnings `CS1591` and `CS1573` promoted to errors.

The test project references the library project and uses `xunit`, `xunit.runner.visualstudio`, and
`Microsoft.NET.Test.Sdk`.

## Commands

From `src/CSharp`:

```powershell
dotnet restore
dotnet build .\DataStructures.sln
dotnet test .\DataStructures.sln
```

For ordinary behavior changes, `dotnet test .\DataStructures.sln` is the main gate because it restores and builds as
needed before running the test project. Use the explicit restore/build steps when validating toolchain or warning
policy changes, or when you want a clearer failure boundary.

## Test Coverage

`tests/Tools.Numerics.Tests/` covers the xUnit suite. See the
[tests README](../../tests/Tools.Numerics.Tests/README.md) for source-file grouping and structure.

The suite covers:

- 256-bit, 512-bit, and 1024-bit signed/unsigned arithmetic;
- checked and unchecked overflow behavior;
- decimal and hexadecimal parsing/formatting across UTF-16 and UTF-8 paths, including `NumberStyles.Number`,
  standard-format precision, and generic parsing interfaces;
- primitive and `BigInteger` conversions;
- fixed-width binary conversion through `BitConverterEx`;
- sparse non-negative integer conversion, arithmetic, and power/logarithm behavior;
- dynamic binding, comparison, hashing, public API coverage, and declaration parity guardrails.

When adding or changing a public wide-integer operation, update the corresponding bit-width and signedness quadrant,
then check sibling-width parity through the mixed-scenario tests.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/CSharp> dotnet test .\DataStructures.sln
```

If a docs-only change only updates links or wording and does not alter commands, API claims, or XML documentation
behavior, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
