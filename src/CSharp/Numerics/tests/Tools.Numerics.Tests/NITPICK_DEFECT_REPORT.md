# Defect Report: Additional issues found during `Tools.Numerics` doc-comment review

## Scope

While reviewing XML documentation comments in `Tools.Numerics` and `Tools.Numerics.Tests`, I also reviewed nearby test logic for consistency with the documented behavior and found defects unrelated to XML comments.

## Defect 1: 512-bit short-input tests accidentally use a 32-byte threshold

### Location

- `src/CSharp/Numerics/tests/Tools.Numerics.Tests/BitConverter512Tests.cs`
  - `TryWriteBytes_WritesAtBeginningAndReturnsLengthContract`
  - `To512_SpanOverloads_ThrowForShortInput`

### Problem

Both tests target 512-bit APIs that require **64 bytes**, but they use `new byte[31]` as the only short-input size.

`31` is valid for demonstrating "too short", but it is a copied 256-bit boundary test value and misses the critical near-boundary case (`63` bytes) for 512-bit operations.

### Why this matters

- It weakens regression detection around off-by-one boundary mistakes specific to 64-byte payloads.
- A buggy implementation that accidentally accepts `63` bytes could still pass these tests.

### Proposed fix

- In 512-bit tests, replace `new byte[31]` with `new byte[63]` for boundary-focused checks.
- Optionally add both values (`31` and `63`) to preserve broad negative testing while ensuring boundary precision.

## Defect 2: trailing-byte preservation assertion is structurally inert

### Location

- `src/CSharp/Numerics/tests/Tools.Numerics.Tests/BitConverter512Tests.cs`
  - `TryWriteBytes_WritesAtBeginningAndReturnsLengthContract`

### Problem

The test allocates `byte[] target = new byte[64];` and then asserts that `target[64..]` remains unchanged.

Because the array length is exactly `64`, the slice is always empty, so the trailing-byte assertion never validates anything.

### Why this matters

- The test claims to validate that data beyond the fixed-width write region is untouched, but currently cannot detect regressions in that behavior.

### Proposed fix

- Increase the destination buffer size (for example, `new byte[80]`).
- Continue writing to `target.AsSpan()`.
- Assert the first 64 bytes match expected output and bytes `[64..]` remain at the sentinel value.
