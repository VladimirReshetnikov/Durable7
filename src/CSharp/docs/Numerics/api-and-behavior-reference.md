# Tools.Numerics API and Behavior Reference

- Created (UTC): 2026-07-03T17:07:42Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Status: Normative (current contract and maintenance invariants)
- Audience: Maintainers, reviewers, advanced consumers
- Scope: Public behavior and compatibility contract for `src/CSharp/src/Tools.Numerics`
- Related code:
  - `src/CSharp/src/Tools.Numerics/UInt256.cs`
  - `src/CSharp/src/Tools.Numerics/Int256.cs`
  - `src/CSharp/src/Tools.Numerics/UInt512.cs`
  - `src/CSharp/src/Tools.Numerics/Int512.cs`
  - `src/CSharp/src/Tools.Numerics/UInt1024.cs`
  - `src/CSharp/src/Tools.Numerics/Int1024.cs`
  - `src/CSharp/src/Tools.Numerics/SparseInteger.cs`
  - `src/CSharp/src/Tools.Numerics/SparseInteger.Conversions.cs`
  - `src/CSharp/src/Tools.Numerics/BitConverterEx.cs`
  - `src/CSharp/src/Tools.Numerics/BitHelpers.cs`
  - `src/CSharp/src/Tools.Numerics/NumericParseHelpers.cs`
  - `src/CSharp/src/Tools.Numerics/ArrayHelpers.cs`
- Related tests:
  - `src/CSharp/tests/Tools.Numerics.Tests/BitWidth256/*`
  - `src/CSharp/tests/Tools.Numerics.Tests/BitWidth512/*`
  - `src/CSharp/tests/Tools.Numerics.Tests/BitWidth1024/*`
  - `src/CSharp/tests/Tools.Numerics.Tests/MixedScenarios/*`
- Related docs:
  - [Workspace overview](overview.md)
  - [Wide-integer maintainer guidance](wide-integer-maintainer-guidance.md)

## Summary

`Tools.Numerics` provides fixed-width, two's-complement integer types where width itself is part of the behavior contract. This document defines the expected API shape and the semantic invariants that must remain stable across implementation changes.

## Responsibilities and non-goals

### Responsibilities

- Provide deterministic integer behavior at 256, 512, and 1024 bits.
- Preserve familiar operator and parse/format ergonomics aligned with BCL integer types.
- Expose explicit checked/unchecked overflow semantics.
- Provide deterministic fixed-size byte conversion APIs.

### Non-goals

- Arbitrary-precision arithmetic (this is `BigInteger` territory).
- Pluggable limb sizes or configurable widths at runtime.
- Encoding policy abstraction beyond architecture-endian `BitConverter`-style behavior in `BitConverterEx`.

## Public surface map

## 1) Core numeric types

- Unsigned: `UInt256`, `UInt512`, `UInt1024`
- Signed: `Int256`, `Int512`, `Int1024`

All six types are readonly structs with value semantics and operator-rich APIs.

## 2) Common capability matrix

All width pairs (signed + unsigned) are expected to support:

- Equality, ordering, and hash-code behavior based on full-width bit/value identity.
- Arithmetic operators (`+`, `-`, `*`, `/`, `%`) with checked variants where meaningful.
- Bitwise operators (`&`, `|`, `^`, `~`) and width-constrained shifts/rotations.
- Parse and format APIs for UTF-16 and UTF-8 pathways.
- Conversion operators to/from primitive integer types and `BigInteger`.
- `IParsable<T>`, `ISpanParsable<T>`, `IMinMaxValue<T>`, and `IUtf8SpanFormattable` integration.

`INumber<T>` and `IBinaryInteger<T>` are deliberately outside the current contract. Their static-abstract member
graphs include generic cross-type creation/conversion and endian read/write operations that are not safely supplied
by the existing handwritten width family. Add them only together with a shared/generated implementation and a
complete BCL differential suite.

## 2a) Sparse non-negative integer type

`SparseInteger` represents non-negative integers that either fit in `ulong` or have a moderate number of set bits,
where each set-bit position is recursively represented as another `SparseInteger`. It supports value comparison,
addition, multiplication, base-2 exponentiation, exact base-2 logarithm, decimal formatting/parsing, and
`BigInteger` conversion when the most significant set-bit position fits in `int`.

Addition merges the two ordered set-bit streams in one forward pass and propagates carry within that merge. It must
not repeatedly insert into or remove from the middle of an array, which turns dense sparse operands into quadratic
array-copy workloads.

## 3) Signed-specific expectations

Signed types (`Int*`) also provide:

- Sign-aware comparison and arithmetic.
- Unary negation with checked overflow protection at `MinValue`.
- Sign-preserving right shift behavior.
- `Abs`/`Sign` semantics (where implemented in each type).

## 4) Unsigned-specific expectations

Unsigned types (`UInt*`) provide:

- Logical right-shift behavior.
- Monotonic value ordering over the full bit space.
- Narrowing conversion checks against unsigned bounds.

## Invariants and guarantees

### Width invariants

- `UInt256`/`Int256` always represent exactly 256 bits.
- `UInt512`/`Int512` always represent exactly 512 bits.
- `UInt1024`/`Int1024` always represent exactly 1024 bits.

No API should introduce width drift or variable-length storage semantics.

### Checked/unchecked invariants

- Unchecked arithmetic must wrap modulo `2^N`, except that signed `MinValue / -1` and `MinValue % -1` throw
  `OverflowException` in checked and unchecked contexts, matching `Int128`.
- Checked arithmetic must throw `OverflowException` when result does not fit the target type.
- Checked narrowing conversions must enforce destination range limits.

### Two's-complement invariants

For signed types:

- Bitwise operations operate on two's-complement representation.
- Sign bit location is stable (`N - 1`).
- Reinterpreting signed/unsigned counterparts preserves bit patterns unless a checked conversion forbids the value.

### Parse/format invariants

- `Parse` and `TryParse` families must remain behaviorally aligned for input-driven success/failure classification
  (`TryParse == false` for any `FormatException`/`OverflowException` path of `Parse`). Invalid `NumberStyles`
  combinations are argument errors, not input errors: both `Parse` and `TryParse` throw `ArgumentException` for
  undefined style flags or for `AllowHexSpecifier`/`AllowBinarySpecifier` combined with flags outside the
  corresponding `HexNumber`/`BinaryNumber` combination, matching the BCL numeric types.
- UTF-8 parse/format pathways must match UTF-16 semantics for equivalent textual data. Fixed-width integer
  UTF-8 parsers transcode through temporary spans rather than allocating intermediate strings.
- `NumberStyles` validation is centralized through shared helper logic. Decimal parsing delegates to `BigInteger`
  and accepts every subset of `NumberStyles.Any`: leading/trailing whitespace and signs, trailing signs,
  parenthesized negatives, culture-specific currency symbols, group separators, exponents, and decimal points whose
  fractional digits are all zero. Hexadecimal parsing requires `AllowHexSpecifier` and supports leading/trailing
  whitespace when the corresponding flags are set. `AllowBinarySpecifier` is a valid style but is not supported:
  binary input fails parsing (`FormatException` from `Parse`, `false` from `TryParse`).
- Formatting supports `G`/`D`/`N`/`X` (and lowercase forms). `D`, `N`, and `X` accept minimum-digit or fractional
  precision components such as `D5`, `N0`, and `X8`. The span overloads use the shared limb formatter for decimal,
  grouped-number, and hexadecimal output without first creating a formatted string; oversized UTF-8 scratch space
  may be rented from `ArrayPool<T>`. String-returning overloads allocate only their required result on the default
  decimal path.
  `G`/`g` accepts no precision component: `BigInteger` formatting ignores `G` precision instead of applying
  `Int128`-style rounding/scientific notation, so a `G` format with a precision specifier (for example `G3`) is
  rejected with `FormatException` rather than silently diverging from the built-in integer types.
- `GetShortestBitLength()` and `GetByteCount()` follow integral-interface conventions: the former reports zero for
  zero and otherwise the BCL signed/unsigned shortest bit length; the latter reports the fixed 32/64/128-byte width.

### Byte conversion invariants (`BitConverterEx`)

- Byte payload sizes are fixed at 32/64/128 bytes by width.
- Methods must fail predictably when source/destination buffers are undersized.
- Signed values are encoded in two's complement.
- Endianness behavior mirrors architecture rules (`BitConverter`-style).

## Parse and format contract notes

### Parse failure classes

- Invalid syntax should surface as format failures.
- Out-of-range numeric input should surface as overflow failures.
- `TryParse` should never throw for ordinary parse failures and should report failure via return value.

The `Int256` hexadecimal overwide case was historically tracked in the source Tools repository and should remain
covered by parse overflow regression tests in this workspace.

### Compatibility target

Parse and format behavior should remain as close as practical to .NET built-in integer semantics (`Int128`/`UInt128`) for analogous scenarios, with width-specific extensions only where required by larger bit sizes.

## Conversion contract notes

### Primitive conversions

- Widening conversions should be implicit where safe and unambiguous.
- Narrowing conversions should be explicit; checked variants must throw on range violations.
- Direct non-adjacent conversions are part of the matrix: 512-bit values interoperate with 128-bit primitives,
  and 1024-bit values interoperate with 256- and 128-bit values. Unchecked narrowing returns the low bits;
  checked narrowing validates the full source value.
- Binary floating-point source conversions match .NET 10 `Int128`/`UInt128`: fractions truncate toward zero;
  unchecked NaN becomes zero and unchecked out-of-range/infinite values clamp to the destination bound; checked
  non-finite or out-of-range values throw. Decimal conversions truncate fractions and unsigned destinations reject
  negative values. Wide-to-float/double uses normal IEEE rounding; wide-to-decimal throws outside decimal range.

### `BigInteger` conversions

- Unchecked explicit conversion from `BigInteger` uses modulo-`2^N` truncation semantics.
- Checked explicit conversion enforces strict representable range and throws `OverflowException` on violations.

These dual modes are part of the public compatibility contract and are used by tests to validate both machine-like and strict behaviors.

## Binary conversion contract (`BitConverterEx`)

### API roles

- `GetBytes(...)`: allocate and return fixed-width payloads.
- `TryWriteBytes(...)`: write fixed-width payloads into caller-provided spans.
- `ToUInt*` / `ToInt*`: read fixed-width values from arrays or spans with robust argument validation.

### Buffer and indexing expectations

- Array overloads with `startIndex` must validate null, range, and remaining length in a deterministic order.
- Span overloads must throw for undersized input where contract states required byte count.
- Try-pattern methods must return `false` instead of throwing for size insufficiency where designed as non-throwing APIs.

## Maintenance rules for behavior changes

When changing behavior in any public member of `Tools.Numerics`:

1. Update or add tests in the corresponding width/signedness quadrant.
2. Confirm parity implications for sibling types (`256/512/1024`, signed/unsigned).
3. Re-check parse/format behavior for both UTF-16 and UTF-8 entry points where relevant.
4. Re-check `BitConverterEx` invariants if representation or conversion logic changes.
5. Update this document and the project README if the contract surface changes.

## Known risk areas and review hotspots

- Overflow classification in parse flows (format-vs-overflow distinction).
- Checked operator correctness around min/max boundary values.
- Cross-width conversion parity drift.
- Endianness bugs in binary conversion paths.
- Signed/unsigned reinterpretation paths that accidentally alter bit patterns.

## Practical review checklist

Use this checklist for contract-sensitive pull requests:

- [ ] Are checked and unchecked arithmetic semantics preserved?
- [ ] Do signed and unsigned counterparts remain behaviorally symmetric where intended?
- [ ] Are parse failures still classified correctly as format vs overflow?
- [ ] Do UTF-8 and UTF-16 parse/format paths still agree?
- [ ] Are `BitConverterEx` byte widths and error conditions unchanged unless intentionally revised?
- [ ] Were corresponding tests updated across relevant bit-width quadrants?

## Related reading

- Project overview: [workspace overview](overview.md)
- Maintainer lessons learned and prevention checklist: [wide-integer maintainer guidance](wide-integer-maintainer-guidance.md)
- Test structure map: [tests README](../../tests/Tools.Numerics.Tests/README.md)
