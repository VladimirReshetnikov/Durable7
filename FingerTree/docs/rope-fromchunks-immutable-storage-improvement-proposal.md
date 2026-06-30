# Rope FromChunks Immutable Storage Improvement Proposal

- Status: Proposed
- Created (UTC): 2026-06-30T18:56:56Z
- Repository HEAD: 85732ea070a1b01d20207554bff87db4a1bab18c
- Audience: Maintainers of the C# FingerTree rope API
- Scope: `Rope<T>.FromChunks`, `MeasuredRope<T, TMeasure, TMeasureOps>.FromChunks` if added later, and immutable
  chunk ownership documentation

## Summary

`Rope<T>.FromChunks(params ReadOnlyMemory<T>[] chunks)` deliberately avoids copying caller-provided memory. That is
useful for bulk import, but `ReadOnlyMemory<T>` is a read-only view rather than an immutable ownership type. If the
memory is backed by a mutable array, the caller can mutate that array after the rope is created and silently change
the contents of an object documented as immutable and persistent.

This is not a defect in the internal chunk algorithms: chunk edits allocate fresh arrays, slices only share backing
storage, and the rope itself never mutates chunk memory. The weak point is the public ownership boundary of the
zero-copy import API.

## Why It Matters

The repository documentation repeatedly describes public collections as immutable snapshots safe for concurrent
reads. That guarantee holds only if the backing chunk storage is not externally mutated after publication. The
current `FromChunks` signature cannot enforce that precondition and its XML documentation does not state it
strongly enough for callers.

External mutation can break:

- snapshot immutability: an older rope version can observe different element values later;
- concurrent-read reasoning: a reader can race with an external writer to the original array;
- sorted, measured, or text invariants in future chunked variants if cached measures no longer match mutated data.

## Proposed Options

1. Document the precondition explicitly on `FromChunks`: caller-provided memory must be immutable for the lifetime
   of every rope version that may reference it.
2. Add a copying API with an obvious name, or emphasize `Create`/`CreateRange` as the safe default for ordinary
   arrays.
3. Consider renaming the zero-copy API to make ownership visible, for example `FromOwnedChunks` or
   `UnsafeFromChunks`, if source compatibility is still flexible.
4. Add a regression test that demonstrates the current hazard and pins the chosen documentation or API behavior.

The C++ port uses copying construction for spans/ranges and reserves zero-copy import for
`std::shared_ptr<const std::vector<T>>` storage, making the ownership requirement visible at the type boundary.
