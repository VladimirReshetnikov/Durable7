# Tools.Numerics

- Status: Implemented workspace
- Created (UTC): 2026-07-03T17:07:42Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Audience: Maintainers implementing and reviewing fixed-width and sparse integer numerics
- Scope: Project layout, API orientation, documentation, and validation entry points for `src/CSharp/src/Tools.Numerics`

`Tools.Numerics` is a fixed-width integer library for .NET that provides signed and unsigned **256-bit**, **512-bit**, and **1024-bit** numeric types with deterministic two's-complement behavior.

This package is intended for domains where integer width is part of the contract (binary protocols, deterministic serialization, reproducible overflow behavior, and cross-platform test fixtures).

## Layout

- `DataStructures.sln` is the solution entry point.
- `Directory.Build.props` applies the workspace's .NET 10, C# preview, nullable, unsafe-block, and XML-documentation warning policy.
- `src/Tools.Numerics/` contains the public library.
- [`tests/Tools.Numerics.Tests/`](../../tests/Tools.Numerics.Tests/README.md) contains the xUnit test project.
- [`docs/`](README.md) contains the API reference, maintainer guidance, validation guide, and historical/design notes.

## Status at a glance

- **Assembly**: `Tools.Numerics`
- **Target framework**: `net10.0`
- **Language configuration**: C# preview, nullable enabled, XML documentation generation enabled and enforced as warnings-as-errors
- **Primary namespace**: `Tools.Numerics`

See `Directory.Build.props` and `src/Tools.Numerics/Tools.Numerics.csproj` for authoritative build configuration details.

## Type inventory

The public wide-integer surface contains six value types:

- `UInt256` — unsigned 256-bit integer
- `Int256` — signed 256-bit integer (two's complement)
- `UInt512` — unsigned 512-bit integer
- `Int512` — signed 512-bit integer (two's complement)
- `UInt1024` — unsigned 1024-bit integer
- `Int1024` — signed 1024-bit integer (two's complement)
- `SparseInteger` — non-negative integer representation for very large sparse binary values

Supporting helpers in the same assembly:

- `BitConverterEx` — fixed-width byte conversion APIs (32/64/128-byte payloads)
- `BitHelpers` — internal allocation-conscious bit/endianness utility routines
- `NumericParseHelpers` — internal culture-aware sign-token helper for parsing flows
- `ArrayHelpers` — internal sorted-array helper used by `SparseInteger`

## Why this library exists

`BigInteger` is ideal for arbitrary-precision arithmetic, but it does not encode a fixed-size overflow contract by default. `Tools.Numerics` targets the opposite design center:

- numeric width is explicit and stable,
- overflows are deterministic and testable,
- signed values are represented in two's complement,
- and parse/format/operator APIs feel close to built-in numeric primitives.

In short: this library favors **predictable machine-like width semantics** over unbounded precision.

## Design principles

- **Fixed-width semantics are primary**: each type always occupies its declared width and computes modulo `2^N` in unchecked contexts.
- **BCL-shape familiarity**: operators, parse methods, formatting, and conversion patterns intentionally mirror built-in integer APIs.
- **Explicit checked behavior**: checked operators and checked narrowing conversions throw `OverflowException` when range contracts are violated.
- **Deterministic binary representation**: byte-oriented APIs always operate on exact-width payloads and state endianness behavior explicitly.
- **Allocation-conscious implementation**: hot paths favor split-half operations, `Span<T>`, and stack-allocated temporary buffers where practical.

## Internal representation model

The integer structs use compositional fixed halves:

- 256-bit values are composed from two `UInt128` halves
- 512-bit values are composed from two 256-bit halves
- 1024-bit values are composed from two 512-bit halves

This keeps carry/borrow and shift behavior explicit while avoiding arbitrary-precision storage as the core runtime representation. `BigInteger` is used as an interop boundary and validation bridge, not as fundamental storage.

## Behavioral contract summary

### Arithmetic and overflow

- Unchecked arithmetic wraps with modulo-`2^N` semantics except for the CLR/BCL-mandated signed
  `MinValue / -1` and `MinValue % -1` overflow cases, which throw in every context.
- Checked arithmetic throws on overflow.
- Signed types maintain two's-complement behavior for all bitwise and shift operations.

### Parsing and formatting

- `Parse` / `TryParse` overloads are available for UTF-16 (`string`, `ReadOnlySpan<char>`) and UTF-8 (`ReadOnlySpan<byte>`) inputs.
- Formatting supports `ToString`, `TryFormat`, and UTF-8 formatting paths, including `G`, `D`, `N`, and `X`
  standard formats. `D`, `N`, and `X` accept precision specifiers such as `D5`, `N0`, and `X8`; `G` takes no
  precision and rejects one (for example `G3`) with `FormatException`.
- Decimal parsing supports every `NumberStyles.Any` subset, including culture-aware signs, parenthesized negatives,
  currency symbols, group separators, exponents, and an all-zero fractional component; hexadecimal parsing preserves
  fixed-width two's-complement semantics. Invalid style combinations throw `ArgumentException` from both `Parse` and
  `TryParse`; `NumberStyles.AllowBinarySpecifier` is valid but unsupported and fails parsing.
- All six types implement `IParsable<T>`, `ISpanParsable<T>`, `IMinMaxValue<T>`, and
  `IUtf8SpanFormattable`. They intentionally do not yet claim `INumber<T>` or `IBinaryInteger<T>`: those interfaces
  require a substantially larger cross-type conversion and endian-operation surface that should be introduced from
  shared/generated code rather than six drifting handwritten implementations.

### Bit-centric helpers

Wide-integer types expose width-aware helpers including:

- `LeadingZeroCount`
- `TrailingZeroCount`
- `PopCount`
- `RotateLeft` / `RotateRight`
- `Log2` (on unsigned and applicable signed paths)

`Log2(0)` returns zero; signed `Log2` rejects negative inputs. `GetShortestBitLength()` follows the
`Int128`/`UInt128` convention (zero is zero bits), while `GetByteCount()` reports the fixed storage width
(32, 64, or 128 bytes).

### Conversion model

- Implicit and explicit conversion operators are provided between built-in primitives and corresponding wide types.
- Explicit conversions are available to/from `BigInteger`.
- Checked conversion paths enforce strict range compatibility.
- `float`/`double` conversions follow .NET 10 `Int128`/`UInt128` policy: unchecked source conversions truncate and
  clamp (with NaN mapped to zero), while checked source conversions reject non-finite and out-of-range values.
  Decimal source conversions truncate and reject negative values for unsigned destinations; wide-to-decimal
  conversion throws when the value exceeds the decimal range.
- Non-adjacent fixed-width conversions cover 512↔128 and 1024↔256/128 directly, with unchecked low-bit
  truncation and checked range enforcement.

## Binary conversion model (`BitConverterEx`)

`BitConverterEx` provides a deterministic, fixed-width binary bridge:

- `GetBytes(...)` materializes width-sized arrays.
- `TryWriteBytes(...)` supports allocation-free writes into caller-owned spans.
- `ToUInt*` / `ToInt*` overloads read from arrays/spans with explicit length validation.

Encoding widths are fixed by type:

- 256-bit types: **32 bytes**
- 512-bit types: **64 bytes**
- 1024-bit types: **128 bytes**

Like `System.BitConverter`, byte order follows architecture endianness; APIs normalize behavior consistently across read and write paths.

## Relationship to reference source

The implementation uses .NET integer behavior as a design anchor. In particular, `Int128` / `UInt128` parsing, formatting, and operator patterns are used as a behavior reference when extending to wider fixed-width types.

## Tests and quality posture

The companion test project [`tests/Tools.Numerics.Tests`](../../tests/Tools.Numerics.Tests/README.md) validates:

- arithmetic and checked/unchecked overflow semantics,
- parsing/formatting (decimal + hexadecimal paths),
- primitive and `BigInteger` conversions,
- binary conversion correctness and edge conditions,
- sparse integer conversion/arithmetic/power behavior,
- public API coverage and declaration parity expectations.

Use [`docs/validation.md`](validation.md) for local restore, build, and test commands.

## When to use `Tools.Numerics`

Prefer this library when you need:

- fixed-width integer contracts larger than built-in primitives,
- deterministic wraparound or checked-overflow behavior,
- strongly typed integer APIs instead of raw byte-array arithmetic,
- sparse representations for extremely large non-negative binary integers with moderate Hamming weight,
- and protocol-friendly, testable binary round-tripping.

If your scenario requires mathematically unbounded precision without fixed-width overflow semantics, prefer `BigInteger` directly.

## Extracted deep-dive documentation

During this README rewrite, the API/member-level contract matrix and failure-mode details were identified as too dense for the entry document and extracted into a dedicated technical reference:

- [Tools.Numerics API and Behavior Reference](api-and-behavior-reference.md) — normative deep dive covering per-type feature matrix, checked/unchecked semantics, parse/format contract details, byte-conversion guarantees, and maintenance invariants.
- [Tools.Numerics Wide-Integer Maintainer Guidance](wide-integer-maintainer-guidance.md) — defect-pattern-oriented playbook distilled from recent Int*/UInt* implementation and follow-up fix cycles, with pre-merge checklists designed to prevent recurrence.
- [Int256 Refactoring Analysis (`c0f7869247c630aa066f6520f690920b76b0d745`)](int256-c0f786-refactoring-analysis.md) — commit-scoped historical analysis covering behavior preservation, performance implications, and readability trade-offs for the Int256 simplification pass.
- [Integral Family Shared-Fragment Extraction Analysis](integral-shared-fragment-analysis.md) — scenario-driven analysis of extraction opportunities spanning all six integral types, signed/unsigned families, and per-bitness pairs.
- [Bitness Code Generation Readiness and Design](bitness-code-generation-readiness-and-design.md) — generator-readiness assessment for future wider integer families.

**Abstract**: use the API/behavior reference when changing contracts, and use the maintainer guidance when planning/refactoring wide-integer implementation work; use this README as the project-level orientation map.
