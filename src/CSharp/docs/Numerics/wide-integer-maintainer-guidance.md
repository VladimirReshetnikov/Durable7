# Tools.Numerics Wide-Integer Maintenance Guidance (Int*/UInt* Family)

- Created (UTC): 2026-07-03T17:07:42Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Status: Normative guidance informed by recent repository history
- Audience: Maintainers and contributors modifying `Int256`/`UInt256`, `Int512`/`UInt512`, `Int1024`/`UInt1024`, related helpers, and tests
- Scope: Lessons learned from recent feature-and-fix cycles in `Tools.Numerics`
- Related code:
  - `src/CSharp/src/Tools.Numerics/Int256.cs`
  - `src/CSharp/src/Tools.Numerics/UInt256.cs`
  - `src/CSharp/src/Tools.Numerics/Int512.cs`
  - `src/CSharp/src/Tools.Numerics/UInt512.cs`
  - `src/CSharp/src/Tools.Numerics/Int1024.cs`
  - `src/CSharp/src/Tools.Numerics/UInt1024.cs`
  - `src/CSharp/src/Tools.Numerics/BitHelpers.cs`
  - `src/CSharp/src/Tools.Numerics/BitConverterEx.cs`
- Related tests:
  - `src/CSharp/tests/Tools.Numerics.Tests/`

## Summary

Recent `Tools.Numerics` history shows a repeating lifecycle:

1. a major width expansion or parity refactor lands,
2. several follow-up commits repair correctness and API-shape drift,
3. additional test infrastructure is introduced or hardened to prevent recurrence.

The same classes of mistakes appeared more than once across 256-bit, 512-bit, and 1024-bit work. This document captures those recurring pitfalls and converts them into concrete maintenance rules.

## High-level timeline patterns observed

### Pattern A: New width introduced, then helper gaps are discovered

- 512-bit introduction was followed by a fix adding missing `UInt256` helper overloads used by 512-bit logic.
- 1024-bit introduction was followed by a fix adding missing `UInt512` helper overloads and correcting 1024-bit endian slicing widths.

### Pattern B: Parse/format behavior looked complete but edge semantics diverged

- Empty-span parse behavior required explicit correction for parity with expected primitive-like contracts.
- Over-wide hexadecimal parsing in signed types required explicit overflow classification fixes.
- Additional 1024-bit parse/format fixes landed after initial implementation due to remaining edge failures.

### Pattern C: Signed/unsigned parity drifted across widths

- Multiple commits focused on declaration/member-shape parity and checked conversion guard alignment.
- Follow-up test refactors strengthened extraction/parity checks because earlier test enforcement missed some forms.

### Pattern D: Test infrastructure itself had defects

- Duplicate API coverage checks and regex/parsing fragility in declaration parity tests required repairs.
- Some test expectations were themselves incorrect and needed investigation-driven correction.

## Recurring pitfalls and how to avoid them

## 1) Incomplete helper-layer promotion when adding a larger width

### Recurring mistake

When adding `N*2` width types, contributors updated the new struct files but did not fully promote helper coverage in `BitHelpers`/`BitConverterEx` for the newly required lower-level operations.

### Why this kept happening

The implementation pattern is compositional (`1024` builds on `512`, `512` on `256`, etc.). It is easy to focus on operators and constructors in the new type and forget that shared helper APIs must be width-closed too.

### Preventive rule

For every new width, complete helper promotion before finalizing type logic:

- bit counts (`LeadingZeroCount`, `TrailingZeroCount`, `PopCount`) for the new base unsigned limb type,
- endian read/write helpers for the new limb size,
- full-width serializer/deserializer slice boundaries verified against byte constants,
- converter APIs (`BitConverterEx`) updated and covered by both positive and negative-length tests.

### Maintainer checklist

- [ ] Can every new struct operation be implemented without ad-hoc local helper duplication?
- [ ] Do helper APIs now exist for all transitive limb widths used by the new type?
- [ ] Are span slices exactly half-width/whole-width (no stale 32/64-byte constants after upscaling)?

## 2) Byte-width constant drift during copy/scale-up edits

### Recurring mistake

After cloning patterns from smaller widths, some slice boundaries and byte-count assumptions remained at old widths (for example, 1024-bit read/write paths using 32-byte partitions where 64-byte partitions were required).

### Why this kept happening

The code shape intentionally mirrors across widths; this encourages copy/paste. Without mechanically validating width constants, stale literals pass review easily.

### Preventive rule

Treat every literal length as suspect during width expansion.

- Derive from named constants where possible.
- Verify endian helpers with explicit round-trip tests that assert both lower and upper halves survive serialization.
- Include tests that exercise non-zero data in both halves to detect accidental truncation.

## 3) Checked conversion guard asymmetry between equivalent types

### Recurring mistake

Equivalent checked conversion guards diverged between widths/sign pairs, then later had to be “aligned with X pattern.”

### Why this kept happening

The family has many explicit and checked operators. Local reasoning for one type can look correct while still violating established family conventions.

### Preventive rule

When modifying one checked conversion guard:

- diff the corresponding operator in every sibling width,
- enforce identical expression strategy where mathematically equivalent,
- add/adjust pairwise boundary tests (`MaxValue`, `MinValue`, `±1` around boundaries).

## 4) Parse contract mismatches in edge inputs (especially empty spans and over-wide hex)

### Recurring mistake

Parsing behavior occasionally diverged from expected primitive-style semantics in edge cases:

- empty spans treated inconsistently across overloads,
- over-wide hex strings reported as format failure instead of overflow (or vice versa) in signed flows.

### Why this kept happening

Parse code has multiple paths (char span, string, UTF-8; decimal vs hex; signed vs unsigned), and overflow-vs-format classification is subtle.

### Preventive rule

Maintain parse behavior as a matrix, not isolated methods.

Minimum regression matrix for every integer family member:

- empty `string`, empty `ReadOnlySpan<char>`, empty `ReadOnlySpan<byte>`,
- max-width valid hex, max-width+1 hex,
- decimal sign token permutations via culture provider,
- invalid character failure modes separated from overflow failure modes.

## 5) Declaration/API parity drift across Int/UInt and across widths

### Recurring mistake

Public/member declaration shapes drifted repeatedly and required many parity-alignment commits.

### Why this kept happening

The family is intentionally symmetric, but manual evolution across six large structs is error-prone.

### Preventive rule

Parity is a first-class correctness property.

- Keep declaration parity tests mandatory and precise.
- Harden extraction logic for realistic signatures (multiline declarations, modifiers, partial type contexts).
- Require parity-test updates in the same change whenever a public member is intentionally asymmetric.

## 6) Over-reliance on “mirrored tests” without adversarial edge vectors

### Recurring mistake

Initial mirrored test suites were substantial but still missed edge defects, causing follow-up fixes.

### Why this kept happening

Mirroring validates broad shape, but not necessarily adversarial boundaries unique to signed arithmetic, two's complement reinterpretation, and checked conversion contracts.

### Preventive rule

For each new width, include both mirrored and adversarial tests:

- mirrored parity suites for baseline consistency,
- edge-case suites deliberately targeting signed extremes, sign extension, half-boundary carries/borrows, and checked overflow transitions,
- focused regression tests for every post-merge defect.

## 7) Test infrastructure fragility (false confidence risk)

### Recurring mistake

Even guardrail tests needed fixes (duplicate API checks, brittle regex assumptions).

### Why this matters

When guardrail tests are flawed, maintainers may get false green builds while parity regressions slip in.

### Preventive rule

Treat test infrastructure as production code:

- refactor test parsers/extractors for robust syntax handling,
- remove duplication that can hide missing checks,
- add clear assertion diagnostics to speed root-cause analysis.

## Recommended engineering workflow for future width/features

1. **Design pass**: define representation, limb boundaries, and helper requirements.
2. **Helper pass**: extend `BitHelpers`/`BitConverterEx` first, with unit tests.
3. **Type pass**: implement Int/UInt pair together to reduce parity drift.
4. **Parity pass**: run/update declaration and API-shape parity tests before behavioral polishing.
5. **Behavior pass**: execute edge matrix for parse, checked conversions, shifts/rotates, and endian round-trips.
6. **Regression pass**: add targeted tests for every discovered defect before merge.

## Required pre-merge checklist (copy into PR description)

- [ ] Helper layer includes full support for all limb widths used by this change.
- [ ] Endian read/write paths verified with non-trivial upper/lower-half data.
- [ ] Checked conversion guards reviewed for cross-width and Int/UInt symmetry.
- [ ] Parse matrix executed (empty, invalid, overflow, signed/hex combinations).
- [ ] Declaration/API parity tests updated and passing.
- [ ] No duplicate or brittle test logic introduced in guardrail test suites.
- [ ] Each bug found during development has a regression test in the same change.

## Red-flag signals during review

Stop and re-verify if you see any of the following:

- new width added with minimal `BitHelpers` changes,
- manual byte-slice literals introduced without constant-backed reasoning,
- conversion guard expression that differs from sibling type for no documented reason,
- parse behavior changes not accompanied by matrix-style tests,
- parity test updates that only weaken assertions instead of improving extraction accuracy.

## Notes for maintainers

The core lesson from recent history is that **family consistency is itself a correctness constraint** in `Tools.Numerics`. For these types, “compiles + mostly passes” is not sufficient. Correctness depends on synchronized behavior across:

- signed/unsigned pairs,
- all supported widths,
- all parse/format overload families,
- helper and test infrastructure layers.

Design and review changes as cross-family operations, not single-file edits.
