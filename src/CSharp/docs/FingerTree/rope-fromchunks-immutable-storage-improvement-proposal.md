# Rope FromChunks Immutable Storage Improvement Proposal

- Status: Implemented 2026-07-01
- Created (UTC): 2026-06-30T18:56:56Z
- Repository HEAD: 85732ea070a1b01d20207554bff87db4a1bab18c
- Audience: Maintainers of the C# FingerTree rope API
- Scope: `Rope<T>.FromChunks`, `MeasuredRope<T, TMeasure, TMeasureOps>.FromChunks` if added later, and immutable
  chunk ownership documentation

## Summary

`Rope<T>.FromChunks(params ReadOnlyMemory<T>[] chunks)` used to avoid copying caller-provided memory. That was useful
for bulk import, but `ReadOnlyMemory<T>` is a read-only view rather than an immutable ownership type. If the memory
was backed by a mutable array, the caller could mutate that array after the rope was created and silently change the
contents of an object documented as immutable and persistent. The C# implementation now copies every imported block
into rope-owned chunks, so the ordinary public API preserves immutable snapshot semantics.

This was not a defect in the internal chunk algorithms: chunk edits allocate fresh arrays, slices only share backing
storage, and the rope itself never mutates chunk memory. The weak point was the former public ownership boundary of
the zero-copy import API.

## Why It Matters

The repository documentation repeatedly describes public collections as immutable snapshots safe for concurrent
reads. Under the former implementation, that guarantee held only if the backing chunk storage was not externally
mutated after publication. The old `FromChunks` signature could not enforce that precondition and its XML
documentation did not state it strongly enough for callers. Copying on import removes that ownership precondition
from the safe public API.

Under the old zero-copy import, external mutation could break:

- snapshot immutability: an older rope version can observe different element values later;
- concurrent-read reasoning: a reader can race with an external writer to the original array;
- sorted, measured, or text invariants in future chunked variants if cached measures no longer match mutated data.

## Implemented Resolution

`FromChunks` now copies each non-empty input block, splitting over-large blocks into bounded rope-owned chunks as it
imports them. A regression test mutates both small and over-large source arrays after construction and verifies the
rope snapshot remains unchanged.

The C++ port uses copying construction for spans/ranges and reserves zero-copy import for
`std::shared_ptr<const std::vector<T>>` storage, making the ownership requirement visible at the type boundary.
