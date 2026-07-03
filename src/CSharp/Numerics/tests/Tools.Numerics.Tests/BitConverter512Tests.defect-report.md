# Defect Report: `BitConverterEx512Tests` boundary coverage gaps

## Scope

File: `src/CSharp/Numerics/tests/Tools.Numerics.Tests/BitConverter512Tests.cs`

## Observed defects

### 1. Insufficient-buffer tests do not target 512-bit boundary

Several negative-path assertions use a 31-byte input buffer for 512-bit APIs (`TryWriteBytes`, span decoding, and array decoding).

For 512-bit values, the fixed binary width is 64 bytes. Using 31 bytes still fails (so the test passes), but it does not verify the near-boundary condition where length is exactly one byte short (`63`). This weakens regression protection for off-by-one mistakes.

#### Impact

- Potential off-by-one defects around the 64-byte cutoff could escape detection if they incorrectly accept 63 bytes.
- The tests currently verify “far too short” inputs rather than the critical edge case.

#### Proposed fix

Change short-buffer inputs from 31 bytes to 63 bytes in 512-bit-specific negative-path checks:

- `TryWriteBytes_WritesAtBeginningAndReturnsLengthContract`
- `To512_ArrayOverloads_ThrowForInvalidInput`
- `To512_SpanOverloads_ThrowForShortInput`

### 2. Trailing-byte preservation claim is currently unverified

`TryWriteBytes_WritesAtBeginningAndReturnsLengthContract` initializes a 64-byte target buffer and then asserts on `target[64..]`. That slice is empty, so the “untouched trailing bytes” behavior is not actually exercised for the 512-bit path.

#### Impact

- The current assertions provide no coverage for accidental writes past the fixed-width payload boundary when destination buffers are larger than 64 bytes.

#### Proposed fix

Use a destination buffer larger than 64 bytes (for example 96 bytes), and assert that bytes from index 64 onward retain their sentinel value after writes.
