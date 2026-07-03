# Numerics Documentation

- Status: Informational
- Created (UTC): 2026-07-03T17:07:42Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Audience: Maintainers and implementers working on `Tools.Numerics`
- Scope: Index of local specifications, guidance, validation, and design notes for `src/CSharp/Numerics`

## Current Documents

- [API and Behavior Reference](api-and-behavior-reference.md) defines the public behavior contract for the fixed-width integer family.
- [Wide-Integer Maintainer Guidance](wide-integer-maintainer-guidance.md) captures recurring implementation and review pitfalls for the `Int*` / `UInt*` family.
- [Validation](validation.md) records local .NET restore, build, and test commands plus the warning policy and test coverage map.
- [Bitness Code Generation Readiness and Design](bitness-code-generation-readiness-and-design.md) assesses generator readiness for future wider fixed-width integer families.
- [Integral Shared-Fragment Analysis](integral-shared-fragment-analysis.md) analyzes common declaration and implementation fragments across the integer family.
- [Int256 Refactoring Analysis](int256-c0f786-refactoring-analysis.md) preserves a commit-scoped historical review of the `Int256` simplification pass.
- [Tests README](../tests/Tools.Numerics.Tests/README.md) maps the xUnit test project and bit-width directory layout.
