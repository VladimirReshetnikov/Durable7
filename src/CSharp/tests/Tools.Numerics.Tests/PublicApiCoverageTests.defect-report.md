# Defect Report: `PublicApiCoverageTests` does not currently exercise `UInt512`/`Int512` public APIs

## Scope

- File: `src/CSharp/tests/Tools.Numerics.Tests/PublicApiCoverageTests.cs`

## Observed defect

The test-class summary states that it exercises public API members for:

- `UInt256`
- `Int256`
- `UInt512`
- `Int512`
- `BitConverterEx`

However, the class currently includes only three test methods:

- `UInt256_PublicSurface_IsExercised`
- `Int256_PublicSurface_IsExercised`
- `BitConverterEx_PublicSurface_IsExercised`

There are no `UInt512`/`Int512` public-surface coverage methods.

## Impact

- Public API additions/removals in `UInt512` and `Int512` can escape this intended sentinel coverage.
- The class-level documentation can mislead maintainers into assuming those surfaces are exercised here.

## Proposed fix

Add dedicated methods that mirror the existing style:

- `UInt512_PublicSurface_IsExercised`
- `Int512_PublicSurface_IsExercised`

Each method should invoke all public members at least once, including:

- constructors and static fields,
- parse/try-parse/format overloads (UTF-16 and UTF-8 paths),
- arithmetic/bitwise/shift/rotate operators,
- checked conversion operators,
- helper APIs (`LeadingZeroCount`, `TrailingZeroCount`, `PopCount`, etc.),
- `IComparable` object path.

This aligns implementation with the stated intent of the coverage test suite.
