# Tools.Numerics Integral Declaration Shared-Fragment Analysis

- Created (UTC): 2026-07-03T17:07:42Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Status: Informational extraction analysis
- Audience: Maintainers considering helper extraction or generator work in `Tools.Numerics`
- Scope: Shared declaration and implementation fragments across `Int256`/`UInt256`, `Int512`/`UInt512`, and `Int1024`/`UInt1024`

## Scope and intent

This document analyzes the six fixed-width integral types in `Tools.Numerics`:

- `UInt256`, `Int256`
- `UInt512`, `Int512`
- `UInt1024`, `Int1024`

The objective is to identify **non-trivial expressions or statement sequences** that are realistically extractable into helper methods/classes. The analysis covers three extraction scenarios:

1. Fragments shared by **all 6** types.
2. Fragments shared by **all 3 signed** types and by **all 3 unsigned** types (separate signed/unsigned helpers).
3. Fragments shared by each **bitness pair** (`Int256`/`UInt256`, `Int512`/`UInt512`, `Int1024`/`UInt1024`).

The focus is maintainability-oriented refactoring opportunities, not textual/codegen-only duplication.

---

## Observed structural baseline

The six declaration files are intentionally parity-shaped and contain repeated wrappers around formatting, parsing, comparison, and bit operations. Existing parity tests in `Tools.Numerics.Tests` already enforce declaration similarity, which is an indicator that helper extraction opportunities are likely to be broad rather than isolated.

---

## Scenario 1 — Shared fragments across **all six** integral types

## A. Formatting wrapper flow (char + UTF-8)

A repeated two-stage flow appears in every type:

1. Build a `string` via `FormatValue(this, format, provider)`.
2. Copy into destination (`Span<char>` or UTF-8 bytes), returning `false` on insufficient buffer.

### Extractable helper concept

- `WideIntegerFormatting.TryFormatChars(...)`
- `WideIntegerFormatting.TryFormatUtf8(...)`

Both can accept a formatter delegate such as `Func<string?>`/`Func<ReadOnlySpan<char>, IFormatProvider?, string>` or a preformatted `string` producer.

### Why this is a meaningful extraction

- Eliminates duplicated buffer-check and copy logic.
- Keeps each type’s format-selection logic local (`FormatValue`), while centralizing the identical output-shaping logic.

## B. Parse/TryParse overload funnels

All six types expose the same overload fan-out pattern:

- `Parse(string)` / `Parse(string, IFormatProvider?)`
- `Parse(string, NumberStyles, IFormatProvider?)`
- `Parse(ReadOnlySpan<char>...)`
- `Parse(ReadOnlySpan<byte>...)`
- `TryParse(...)` variants for string/char span/utf8 span

The wrappers consistently forward to `ParseCore` / `TryParseCore` after common pre-steps (`null` handling, UTF-8 conversion, default style/provider routing).

### Extractable helper concept

- `WideIntegerParseOverloads.ForStringInput(...)`
- `WideIntegerParseOverloads.ForUtf8Input(...)`
- `WideIntegerParseOverloads.TryParseNullableString(...)`

Use delegates for final typed parse core (`Func<ReadOnlySpan<char>, NumberStyles, IFormatProvider?, T>` and `Try` counterpart).

### Why this is meaningful

The repetitive overload glue is contract-critical and high-volume; consolidating it reduces parity drift risk while preserving each type’s custom core parsing implementation.

## C. Boxed `IComparable.CompareTo(object?)` switch shape

Each type has the same boxed-compare pattern:

- `null => 1`
- exact-type cast => strongly typed `CompareTo`
- otherwise throw `ArgumentException` with type-specific message

### Extractable helper concept

- `WideIntegerObjectComparison.CompareToBoxed<T>(object?, Func<T, int> compare, string expectedTypeName)`

### Caveat

Because this sits on frequently used primitive-like types, ensure helper remains aggressively inlinable and does not degrade exception message quality.

## D. Equality/Object equality/hash wrappers

Common wrappers are repeated in all six files:

- `Equals(object?)` type-test + strongly typed `Equals`
- `operator ==` / `!=` forwarding to typed equality
- `GetHashCode()` via `HashCode.Combine(_upper, _lower)`

### Extractable helper concept

- `WideIntegerEquality.EqualsObject<T>(object?, Func<T, bool>)`
- `WideIntegerEquality.CombineHash<TUpper, TLower>(TUpper upper, TLower lower)`

### Note

This extraction is lower-value than parse/format extraction because methods are small, but it can still reduce repeated boilerplate.

---

## Scenario 2 — Shared fragments by **signed family** and **unsigned family**

## A. Unsigned-family common fragments (`UInt256`, `UInt512`, `UInt1024`)

## 1) Unsigned parse-core hex/decimal accumulation skeleton

Unsigned parse cores share the same conceptual machinery:

- trim/validate number styles
- detect hex mode
- run digit accumulation over full fixed width
- overflow classification and fail/throw mapping

### Extractable helper concept

- `WideUnsignedParseCore.TryParseFixedWidth(...)`

with parameterization by:

- width constants (`Bits`, `Bytes`)
- limb operations/delegates (shift/add/multiply checks)
- type construction delegate from high/low limbs or from internal intermediate value

## 2) Unsigned bit utility shape

`LeadingZeroCount`, `TrailingZeroCount`, `PopCount`, `RotateLeft`, `RotateRight`, and `Log2` follow near-identical strategy in the three unsigned types, differing mostly by limb width/type.

### Extractable helper concept

- `WideUnsignedBitOps` with bitness-parameterized helper methods.

### Caveat

If extraction requires boxing/interface dispatch, avoid it; this area is likely hot-path sensitive.

## B. Signed-family common fragments (`Int256`, `Int512`, `Int1024`)

## 1) Signed parse flow as sign-token preprocessor + unsigned parse backend

Signed parse logic in all three types follows a repeated pattern:

- normalize and inspect sign token (`+` / `-`) and style constraints
- parse magnitude (often through unsigned counterpart parsing path)
- perform two’s-complement boundary checks
- negate or convert accordingly, with checked overflow behavior for min-boundary edge cases

### Extractable helper concept

- `WideSignedParseCore.TryParseViaUnsigned(...)`

with delegates for:

- unsigned parser
- signed construction
- min/max boundary validation

## 2) Signed bit/utility wrappers over unsigned counterparts

Signed `RotateLeft` and `RotateRight` implementations in all three files are shape-identical wrappers over unsigned rotate operations with cast-through conversion.

### Extractable helper concept

- `WideSignedBitOps.RotateLeftViaUnsigned<TSigned, TUnsigned>(...)`
- `WideSignedBitOps.RotateRightViaUnsigned<TSigned, TUnsigned>(...)`

This is high-confidence extractable because the repeated logic is exactly adapter-style.

---

## Scenario 3 — Shared fragments by **bitness pairs**

## A. Pair-specific shared fragments (`IntN` + `UIntN` for same N)

For each bitness, signed/unsigned types share the same underlying limb geometry and many identical operator implementation skeletons with sign semantics layered on top.

Common pair-level patterns include:

- same field topology (`_upper` + `_lower`) with same limb type
- same byte width constants and endian read/write staging sizes
- same split-half carry/borrow mechanics in arithmetic kernels
- same popcount/leading/trailing zero decomposition over the two halves

### Extractable helper concept

Create one helper per bitness:

- `Wide256Core`
- `Wide512Core`
- `Wide1024Core`

Each helper can centralize bitness-specific half operations and buffer slicing math used by both signed and unsigned type pairs.

### Why pair-scoped extraction may be best

- Avoids over-generalized generic abstractions that are hard for JIT to optimize.
- Captures high-value reuse while retaining straightforward generated machine code.
- Reduces signed/unsigned drift without forcing a single mega-helper for all widths.

---

## Recommended extraction strategy (priority order)

1. **High confidence, low risk:**
   - parse/try-parse overload funnel wrappers
   - format-to-buffer wrappers (`TryFormat` char/utf8)
2. **Moderate risk, high value:**
   - signed parse preprocessing via unsigned backend
   - unsigned parse-core skeleton extraction
3. **Selective, perf-sensitive:**
   - bit-ops helper extraction (especially rotate wrappers)
   - pair-specific core helpers for endianness and limb decomposition

If performance concerns dominate, prefer `internal static` helpers with `[MethodImpl(MethodImplOptions.AggressiveInlining)]` and avoid interface-based generic polymorphism.

---

## Practical verdict by requested scenarios

- **Scenario 1 (all 6 share one fragment):**
  - **Confirmed.** Multiple non-trivial fragments are common across all six types (especially parse/format overload funnels and object-comparison wrappers).

- **Scenario 2 (signed-family and unsigned-family fragments):**
  - **Confirmed.** Signed and unsigned families each have substantial shared parse/bit-operation structures that are extractable into separate helpers.

- **Scenario 3 (per-bitness signed+unsigned fragments):**
  - **Confirmed.** Each bitness pair has common half-geometry/byte-staging logic that can be extracted into width-specific core helpers.

All three scenarios are real and actionable; they are complementary, not mutually exclusive.

---

## Suggested next step for implementation planning

Stage refactoring in this order:

1. Introduce helper(s) for overload funnels and formatting wrappers (minimal behavioral risk).
2. Add/expand parity + behavior tests specifically around parse classification and buffer boundary behavior.
3. Extract signed/unsigned parse cores with strict boundary-test coverage.
4. Only then consider deeper arithmetic/bit helper extraction, guided by benchmarks.

This sequence minimizes regressions while still removing the largest repeated logic blocks.
