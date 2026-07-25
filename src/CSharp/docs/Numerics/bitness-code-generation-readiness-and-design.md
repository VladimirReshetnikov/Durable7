# Durable7.Numerics Bitness Code Generation: Readiness Assessment and Design

- Created (UTC): 2026-07-03T17:07:42Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Status: Informational with normative recommendations for future generator implementation
- Audience: Maintainers, contributors, reviewers planning `Int2048` / `UInt2048` and future `2^N` width additions
- Scope: `src/CSharp/src/Durable7.Numerics` wide-integer family (`Int256/512/1024`, `UInt256/512/1024`) and corresponding tests in `src/CSharp/tests/Durable7.Numerics.Tests`
- Related code:
  - `src/CSharp/src/Durable7.Numerics/UInt256.cs`
  - `src/CSharp/src/Durable7.Numerics/Int256.cs`
  - `src/CSharp/src/Durable7.Numerics/UInt512.cs`
  - `src/CSharp/src/Durable7.Numerics/Int512.cs`
  - `src/CSharp/src/Durable7.Numerics/UInt1024.cs`
  - `src/CSharp/src/Durable7.Numerics/Int1024.cs`
  - `src/CSharp/src/Durable7.Numerics/BitHelpers.cs`
  - `src/CSharp/src/Durable7.Numerics/BitConverterEx.cs`
  - `src/CSharp/tests/Durable7.Numerics.Tests/MixedScenarios/DeclarationParityTests.cs`
  - `src/CSharp/tests/Durable7.Numerics.Tests/MixedScenarios/PublicApiCoverageTests.cs`
- Related docs:
  - `src/CSharp/docs/Numerics/overview.md`
  - `src/CSharp/tests/Durable7.Numerics.Tests/README.md`
  - `src/CSharp/docs/Numerics/wide-integer-maintainer-guidance.md`

## Summary

Short answer: **yes, the repository is close to generator-ready**, and the most important prerequisite already exists: a test-defined declaration parity model that formalizes “same shape modulo bitness/sign.”

Long answer: we are ready to generate type files (`UIntN.cs`, `IntN.cs`) from canonical templates plus a width metadata model, but we are **not yet fully ready for one-command onboarding of new widths** unless generation scope also includes helper APIs and test assets currently hard-coded to 256/512/1024.

In other words:

- **Type-level generation readiness:** high.
- **Whole-subsystem width-onboarding readiness:** medium.

## What is already in place (strong readiness signals)

## 1) Structural parity is explicit and enforced by tests

`DeclarationParityTests` is already a machine-checkable specification of cross-width declaration shape and signed/unsigned alignment. It token-normalizes width-dependent symbols (type names, bit constants, masks, byte counts, half-width types), then asserts equality across siblings.

This is exactly the kind of formalization a generator needs:

- a canonical declaration grammar target,
- a “modulo width” equivalence definition,
- and automated drift detection.

The same test suite includes family-level and signed-vs-unsigned parity checks, not only adjacent width pairs, which reduces the chance of template skew.

## 2) Existing implementation pattern is intentionally compositional

The current family consistently follows “value = two halves of previous limb width”:

- `UInt256` uses `UInt128` halves,
- `UInt512` uses `UInt256` halves,
- `UInt1024` uses `UInt512` halves,
- signed variants mirror this with unsigned halves and two's-complement semantics.

That recursive structure is generator-friendly because the generated code needs only a small width metadata tuple to resolve:

- current width,
- half width,
- half type names,
- sign-bit/magnitude constants,
- shift mask,
- byte count.

## 3) Project configuration does not block generated source files

`Durable7.Numerics.csproj` uses SDK defaults and does not explicitly enumerate `Compile` items. Generated `.cs` files placed under the project directory are automatically included.

This removes one common generator friction point (manual project-file edits per new type).

## 4) Documentation and maintainer guidance already identify repeated manual failure modes

Existing maintainer guidance captures recurring defects from manual copy/scale edits (helper promotion misses, stale byte constants, conversion asymmetry, parse edge drift). Those lessons can be encoded into generator rules and post-generation checks.

That historical knowledge materially lowers generator design risk.

## What is not fully ready yet (gaps to address in generator scope)

## 1) Helper APIs are still manually width-enumerated

`BitConverterEx` and `BitHelpers` currently expose width-specific overload sets up to 1024. Introducing 2048-bit types by generating only `Int2048/UInt2048` would still leave missing helper surfaces and likely break expected ergonomics/parity.

**Implication:** generation plan should include helper expansion artifacts (or a second generator stage for helper families).

## 2) Test assets are width-specific and partially hard-coded

Although declaration parity is generic in spirit, multiple tests are explicitly authored for 256/512/1024 files/types, and test directory structure is fixed by width (`BitWidth256`, `BitWidth512`, `BitWidth1024`).

**Implication:** full automation requires either:

- generated test files for each new width, or
- refactoring selected tests into parameterized width matrices.

## 3) Public API coverage is manually curated by concrete type

`PublicApiCoverageTests` intentionally invokes APIs by concrete type and width. This is useful today but means new widths require broad, repetitive edits unless those tests are also generation-backed.

## 4) Current parity definition is source-text/declaration oriented, not behavior-template oriented

`DeclarationParityTests` validates declaration shape and attributes, which is necessary, but not sufficient to prove behavior template integrity (for example parse/format edge semantics and checked conversion guard logic).

**Implication:** generator rollout should keep (and expand) adversarial behavior regression tests, not rely solely on declaration parity green status.

## Readiness verdict

For your specific goal (“add `Int2048`/`UInt2048` and then automate all required `2^N` bitness siblings”), readiness is:

- **Ready now for**: type source generation architecture/design and likely first implementation iteration.
- **Needs coordinated follow-up for complete automation**: helper-layer generation, test generation/parameterization, and onboarding workflow integration.

The codebase is at the right maturity stage to start generator work, as long as we treat it as a subsystem initiative rather than “emit two new `.cs` files only.”

## Proposed generator design

## Design goals

1. Generate `Int{N}`/`UInt{N}` for widths `N = 2^k` (at least from 256 upward).
2. Preserve declaration parity and declaration ordering guarantees currently enforced by tests.
3. Keep generated output deterministic and stable (idempotent runs, minimized diff noise).
4. Make intentional asymmetries explicit in metadata, not ad-hoc edits in generated files.
5. Allow staged adoption (types first, then helpers/tests) without dead-end architecture.

## Non-goals

- Replacing all handwritten numeric code in one shot.
- Runtime source generation in consumer builds.
- Inserting reflection-based dynamic type systems into `Durable7.Numerics`.

## Generator architecture

### 1) Canonical metadata model (single source of truth)

Define a width specification record, conceptually:

- `WidthBits` (e.g., 256, 512, 1024, 2048)
- `ByteCount = WidthBits / 8`
- `HalfBits = WidthBits / 2`
- `UnsignedTypeName = UInt{WidthBits}`
- `SignedTypeName = Int{WidthBits}`
- `UnsignedHalfTypeName` (`UInt128` for 256, else `UInt{HalfBits}`)
- `SignedHalfTypeName` (`Int128` for 256, else `Int{HalfBits}`)
- `ShiftMaskLiteral` (e.g., `0xFF`, `0x1FF`, `0x3FF`, `0x7FF`)
- `MagnitudeBits = WidthBits - 1`
- `UpperHalfSignBit = HalfBits - 1`

This metadata should drive all emitted literals and type tokens.

### 2) Role-based templates

Use role-oriented templates rather than copy-paste concrete files:

- `UnsignedType.template.cs` (`UIntN`)
- `SignedType.template.cs` (`IntN`)
- optional later:
  - `BitConverterEx.template.fragment.cs`
  - `BitHelpers.template.fragment.cs`
  - `PublicApiCoverageTests.template.cs`
  - `BitWidthN test templates`

Template tokens should model semantic roles (e.g., `{{TYPE}}`, `{{HALF_TYPE}}`, `{{BITS}}`, `{{BYTE_COUNT}}`) matching the parity test normalization vocabulary where practical.

### 3) Deterministic emission pipeline

For each requested width `N`:

1. Validate width eligibility (`N` power-of-two and supported minimum).
2. Build width metadata graph (including reference to `N/2` types).
3. Render unsigned and signed templates.
4. Normalize newline/whitespace and sort generated regions deterministically.
5. Write files atomically.

Idempotency requirement: running generator twice with unchanged metadata yields byte-identical output.

### 4) Guardrails integrated with existing tests

Post-generation validation should at minimum run:

- declaration parity tests,
- public API coverage tests,
- width-specific behavior suites,
- mixed scenario edge tests.

Generator success should be defined by validation-clean state, not merely successful file write.

## Suggested implementation phases

### Phase 1: Type generation only (minimum useful milestone)

- Introduce generator that emits `UIntN.cs` and `IntN.cs`.
- Use it to emit `UInt2048.cs` and `Int2048.cs`.
- Keep helper and test file edits manual for this phase.

### Phase 2: Helper-generation support

- Generate/patch `BitConverterEx` width blocks.
- Generate/patch required `BitHelpers` overload families.
- Add helper-specific parity/coverage checks if missing.

### Phase 3: Test-surface automation

- Parameterize or generate width-specific tests (especially API coverage and bit-width folder suites).
- Treat declaration parity plus behavior matrix as acceptance gates for new width onboarding.

### Phase 4: Onboarding UX

- Add a single command, e.g. “add-width 2048”, that performs generation + suggested validation command list.
- Optionally support batch generation for a width ladder.

## Design choices and trade-offs

## Template-based source generation vs Roslyn syntax-tree emission

- **Template-first advantage:** easiest to preserve current human-readable style and declaration order that parity tests enforce.
- **AST-first advantage:** stronger structural correctness guarantees, better for deep refactors.

Given current parity constraints and aligned file style, template-first is the practical starting point. AST-based generation can be introduced later if needed.

## In-repo generated files vs generated-at-build-time

- **In-repo generated files (recommended):** reviewable diffs, no hidden build-time behavior, easier debugging.
- **Build-time generation:** cleaner repository but higher tooling complexity and build coupling.

Current repository patterns strongly favor committed generated source.

## One monolithic generator vs staged generators

A staged approach (types first, helpers/tests second) gives fast value while avoiding “all-or-nothing” blocker risk. The key is to keep one metadata model shared by all stages.

## Risks and mitigations

- **Risk: generated code passes declaration parity but fails behavior edges.**
  - Mitigation: maintain adversarial edge suites and checked-conversion boundary tests as required gates.

- **Risk: helper APIs lag behind newly generated widths.**
  - Mitigation: include helper generation in phase plan and add width-completeness checks.

- **Risk: template drift from intended hand-authored style.**
  - Mitigation: baseline templates from current best files and enforce idempotent formatter pass.

- **Risk: regeneration causes noisy churn.**
  - Mitigation: deterministic emission ordering and exact whitespace rules.

## Acceptance criteria for “generator-ready for new width”

A width `N` should be considered fully onboarded only when all are true:

1. `IntN` and `UIntN` files are generated from metadata/templates.
2. Required helper APIs (`BitHelpers`, `BitConverterEx`) support `N` symmetrically with existing widths.
3. Declaration parity tests pass with `N` included in family comparisons.
4. Public API coverage and mixed scenario tests include `N`.
5. Bit-width-specific behavior suites exist (generated or parameterized) for signed/unsigned/shared scenarios.

## Practical recommendation

Proceed with generator work in the next PR, but define scope explicitly as:

1. metadata model,
2. type templates,
3. deterministic generator command,
4. first generated target (`Int2048` + `UInt2048`),
5. helper/test follow-up phases either included or immediately queued.

This balances speed with correctness and avoids repeating the historical “new width first, parity/helper/test repairs later” cycle.
