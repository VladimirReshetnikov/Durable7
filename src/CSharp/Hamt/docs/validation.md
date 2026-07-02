# C# HAMT Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T20:33:30Z
- Repository HEAD: 7c02f68ae23244d48871317ea90d26c0defd2394
- Audience: Maintainers validating the C# HAMT workspace
- Scope: Local restore, build, test, warning-policy, and test-coverage guidance for `src/CSharp/Hamt`

Use this guide when changing the C# HAMT library, tests, examples, or documentation that makes build,
test, API, or complexity claims. For semantic contracts and usage examples, pair it with the
[API specification](api-specification.md) and [usage guide](usage.md).

## Build Model

`Hamt.sln` contains:

- `src/Tools.DataStructures.Hamt/Tools.DataStructures.Hamt.csproj`, the public library.
- `tests/Tools.DataStructures.Hamt.Tests/Tools.DataStructures.Hamt.Tests.csproj`, the xUnit/CsCheck
  test project.

`Directory.Build.props` applies the workspace defaults:

- Target framework: `net10.0`.
- Language version: C# `preview`.
- Nullable annotations and implicit usings enabled.
- XML documentation generation enabled.
- Public XML documentation warnings `CS1591` and `CS1573` promoted to errors.

The test project references the library project and uses `xunit`, `xunit.runner.visualstudio`,
`Microsoft.NET.Test.Sdk`, and `CsCheck`.

## Commands

From `src/CSharp/Hamt`:

```powershell
dotnet restore
dotnet build .\Hamt.sln
dotnet test .\Hamt.sln
```

For ordinary behavior changes, `dotnet test .\Hamt.sln` is the main gate because it restores and builds
as needed before running the test project. Use the explicit restore/build steps when validating toolchain
or warning-policy changes, or when you want a clearer failure boundary.

## Test Coverage

`tests/Tools.DataStructures.Hamt.Tests/` covers the xUnit/CsCheck suite. See the
[tests README](../tests/Tools.DataStructures.Hamt.Tests/README.md) for source-file grouping and filter examples.

The suite covers:

- map construction, lookup, replacement, removal, no-op behavior, and enumeration;
- set construction, membership, add/remove, set algebra, and `IReadOnlySet<T>` behavior;
- comparer preservation, first equivalent key/item retention, and custom equality;
- equal-hash collision buckets, deep shared hash prefixes, and collision splitting;
- allocation-free copy-safe enumerators;
- structural sharing/root-shape invariants through internal test access;
- generated map histories checked against model dictionaries with retained snapshots;
- generated set behavior checked against model set semantics.

For a new public operation, add both direct examples and model/property coverage when there is a natural
BCL or simple in-memory oracle.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/CSharp/Hamt> dotnet test .\Hamt.sln
```

If a docs-only change only updates links or wording and does not alter commands, API claims, or XML
documentation behavior, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
