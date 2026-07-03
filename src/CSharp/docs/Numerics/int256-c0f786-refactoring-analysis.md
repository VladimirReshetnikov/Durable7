# Int256 Refactoring Analysis (`c0f7869247c630aa066f6520f690920b76b0d745`)

- Created (UTC): 2026-07-03T17:07:42Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Status: Historical analysis
- Audience: `Tools.Numerics` maintainers and reviewers
- Scope: behavioral and maintainability analysis of the `Int256.cs` refactoring commit `c0f7869247c630aa066f6520f690920b76b0d745`
- Related code:
  - `src/CSharp/src/Tools.Numerics/Int256.cs`
- Related commits:
  - `c0f7869247c630aa066f6520f690920b76b0d745` (“Simplify `Int256` implementation by inlining helper methods and other refactorings”)

## Summary

Commit `c0f7869247c630aa066f6520f690920b76b0d745` reduced `Int256.cs` by removing helper indirection and tightening several implementations in place. Net file-level effect from the commit itself: **35 insertions and 67 deletions** (`-32` LOC in Git’s accounting).

The refactoring is dominated by three themes:

1. **Inlining small private helpers into call sites** (`WriteFullLittleEndian`, `GetLowerIfUpperIsZero`, `GetLowerSignedIfSignExtended`).
2. **Equivalent-control-flow simplification** (positive-first branch in formatting, compact loop body layout in multiplication).
3. **Local expression streamlining** (passing `NumberFormatInfo.GetInstance(provider)` directly to sign-stripping helper).

Based on source-level equivalence analysis, the commit is behavior-preserving for all touched paths. Performance impact is expected to be neutral to slightly positive in typical JIT configurations; readability impact is mixed but overall favorable for reducing “jump to helper for one-line rule” friction.

## Change inventory and analysis

### 1) Multiplication nested-loop formatting simplification

#### What changed

In the multiply implementation, the nested `for` loops were flattened stylistically:

- `for (int i = 0; i < 4; i++)`
- `for (int j = 0; j < 4; j++)`
- body statements moved out of an extra block level while preserving order.

No arithmetic operation, carry propagation, index increment, or loop bound changed.

#### Behavior preservation assessment

- Loop bounds and iteration order are unchanged (`i` then `j`, each `0..3`).
- `multiplication`, `sum`, `carry`, and `index` computations are identical.
- Carry-propagation `while (carry != 0)` loop is unchanged.

**Conclusion**: behavior-preserving.

#### Performance notes

- No algorithmic or allocation changes.
- Generated machine code should be equivalent or trivially equivalent after JIT normalization.

**Expected cost**: none.

#### Readability notes

- Slightly denser visual layout.
- Slight reduction in indentation noise.

**Net readability**: marginally improved for experienced readers; neutral for most.

---

### 2) `ToString(Int256, IFormatProvider?)` branch inversion

#### What changed

The method moved from “negative branch first + positive fallthrough” to “positive early return + negative path second”:

- Before: `if (value.IsNegative) { ... } return unsigned.ToString(...);`
- After: `if (!value.IsNegative) return unsigned.ToString(...); ...negative handling...`

Negative magnitude logic for `MinValue` remains the same.

#### Behavior preservation assessment

Both versions implement:

- Non-negative values: format by treating the bits as `UInt256`.
- Negative values: emit culture-aware negative sign + magnitude, with `MinValue` special-case to avoid overflow in negation.

There is no semantic shift in `provider` usage or sign token handling.

**Conclusion**: behavior-preserving.

#### Performance notes

- Equivalent operation count on both paths.
- If non-negative values dominate workloads, early return may marginally improve branch predictability.

**Expected cost**: neutral to slightly positive.

#### Readability notes

- Positive-fast-path style aligns with common formatting-path coding conventions.
- Negative logic is grouped together and easier to scan as a single block.

**Net readability**: improved.

---

### 3) `TryParseNative` `NumberFormatInfo` inlining

#### What changed

A local variable:

```csharp
NumberFormatInfo numberFormat = NumberFormatInfo.GetInstance(provider);
```

was removed and replaced with direct argument expression in `TryStripLeadingSign`.

#### Behavior preservation assessment

- `NumberFormatInfo.GetInstance(provider)` is still called once on this path.
- Returned value is still passed to `TryStripLeadingSign` before unsigned parse.
- Trimming/sign handling order is unchanged.

**Conclusion**: behavior-preserving.

#### Performance notes

- Same method invocation count.
- Potentially one fewer local in IL/debug metadata; runtime impact negligible.

**Expected cost**: none.

#### Readability notes

- Fewer local names reduces clutter.
- Slightly less explicit “named concept” for readers stepping through parse flow.

**Net readability**: neutral.

---

### 4) Removal of `WriteFullLittleEndian` helper and direct use in `ToBigInteger`

#### What changed

`ToBigInteger` replaced:

- `WriteFullLittleEndian(bytes);`

with two explicit writes:

- `WriteUInt128LittleEndian(_lower, bytes[..16]);`
- `WriteUInt128LittleEndian(_upper, bytes.Slice(16, 16));`

and the private helper `WriteFullLittleEndian` was removed.

#### Behavior preservation assessment

- The removed helper was exactly those two statements in the same order.
- Destination span (`32` bytes) setup and `new BigInteger(bytes, isUnsigned: false, isBigEndian: false)` call are unchanged.

**Conclusion**: behavior-preserving.

#### Performance notes

- Removes one private method call site. The removed helper was marked `AggressiveInlining`, so JIT likely already flattened it.
- Worst case: equal cost; best case: tiny code simplification.

**Expected cost**: neutral to very slightly positive.

#### Readability notes

- `ToBigInteger` now exposes its byte-layout intent directly.
- Loses a reusable named abstraction, but there was only one call site.

**Net readability**: improved for local comprehension.

---

### 5) Checked conversion helper inlining (`UInt128`/`Int128` paths)

#### What changed

Removed private helpers:

- `GetLowerIfUpperIsZero(Int256 value)`
- `GetLowerSignedIfSignExtended(Int256 value)`

and inlined their logic into checked conversion operators.

Notable call-site rewrites:

- `checked((ulong)GetLowerIfUpperIsZero(value))` → `checked((ulong)(UInt128)value)`
- `checked((long)GetLowerSignedIfSignExtended(value))` → `checked((long)(Int128)value)`
- checked `UInt128` and `Int128` operators now host explicit overflow checks directly.

#### Behavior preservation assessment

- The checked `UInt128` conversion still requires `_upper == 0`, otherwise throws `OverflowException`.
- The checked `Int128` conversion still requires `_upper` be exact sign-extension of low-half sign bit, otherwise throws `OverflowException`.
- `checked ulong`/`checked long` now delegate through checked 128-bit conversions; this composes equivalent overflow conditions.

Potential concern was exception-type/ordering drift; analysis indicates the same `OverflowException` contract is preserved for out-of-range values.

**Conclusion**: behavior-preserving.

#### Performance notes

- Removes helper call boundaries and centralizes checks in conversion operators.
- With JIT inlining, prior performance was likely already similar.
- Chained checked casts (`Int256` → `Int128`/`UInt128` → primitive) may introduce equivalent additional IR nodes, but no meaningful overhead is expected after optimization.

**Expected cost**: neutral.

#### Readability notes

- Conversion rules become visible at operator declarations, reducing indirection.
- Shared helper reuse is lost, but the logic remains short and now colocated with contract-bearing members.

**Net readability**: improved for API contract auditing.

## Behavior-preservation verdict

### Overall verdict: **Yes, the commit is behavior-preserving**

No touched change alters:

- mathematical algorithms,
- numeric bounds,
- sign-extension rules,
- endianness layout,
- parsing style gatekeeping (`NumberStyles.Integer`), or
- documented exception contract surface for checked narrowing conversions.

The refactoring changes structure and locality, not semantics.

## Performance impact verdict

### Overall verdict: **No meaningful runtime regression expected**

- Most edits are helper inlining / branch shape changes.
- The removed helpers were tiny and at least one was already `AggressiveInlining`.
- Potential gains are minor (reduced indirection, positive-fast-path branch shape), and likely below measurement noise for end-to-end workloads.

If micro-performance certainty is required, benchmark only:

- `ToString` for mixed sign distributions,
- checked narrowing conversions in tight loops,
- `ToBigInteger` conversion throughput.

## Readability impact verdict

### Overall verdict: **Generally improved, with one neutral trade-off**

Improvements:

- fewer one-off private helpers,
- conversion invariants visible directly at operators,
- explicit byte-write intent in `ToBigInteger`.

Trade-off:

- inlining `NumberFormatInfo.GetInstance(provider)` removes a semantically named local.

Net effect favors maintainability during local code review and defect triage, especially in conversion-heavy regions where correctness constraints are subtle.

## Maintainer guidance

When applying similar refactorings in other wide-integer types (`Int512`, `Int1024`, and unsigned counterparts):

1. Preserve checked-conversion exception contracts exactly.
2. Keep sign-extension predicates explicit and colocated with checked signed conversions.
3. Prefer direct, width-explicit byte writes when helper abstraction has only one call site.
4. Re-run conversion and parse/format test suites after any helper-removal pass to guard against latent drift introduced by “obvious” rewrites.
