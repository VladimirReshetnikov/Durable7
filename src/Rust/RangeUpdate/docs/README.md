# Rust Range-Update Sequence Documentation

- Created (UTC): 2026-07-15T10:22:25Z
- Repository HEAD: 83d2d4bc69d8c77980127695f656f0aa5ecf56bd
- Audience: Consumers, maintainers, reviewers, and port authors
- Scope: Documentation index for `tools-data-structures-range-update`

- [Crate overview and Rust API mapping](../README.md) describes the algebra trait, public surface,
  lazy invariant, persistence, result mapping, complexity, and focused validation command.
- [Normative C# contract](../../../CSharp/docs/FingerTree/range-update-sequence.md) defines the shared
  monoid/action laws, directional composition, callback and identity behavior, structural bounds,
  and cross-port validation obligations.
- [Executable test map](../tests/README.md) explains the deterministic public-API evidence.
- [Repository porting guide](../../../../docs/guides/porting-and-semantic-parity.md) governs future
  semantic changes across sibling workspaces.

## Intentional Rust mappings

The Rust policy is a nominal static type extending the public FingerTree `MeasurePolicy<T>` rather
than a retained runtime callback object. `usize` makes negative positions unrepresentable;
fallible edits and structural/range operations return `RangeUpdateError`, while `get` returns an
owned `Option<T>` because a pending tag may synthesize the logical value. Iterators are independent
snapshot-owning Rust iterator values and do not emulate C# enumerator boxing, `Current`, `Reset`, or
copied-struct fail-fast behavior. Cloning is the O(1) same-sequence factory shortcut; generic
`from_items` always enumerates and rebuilds because Rust cannot specialize an `IntoIterator`
constructor by the runtime source type. Persistent path copying requires `T: Clone`; measures and
tags are cloneable through their policy-trait bounds.

The representation is independently owned by this neutral crate. It uses the public ordered-measure
contract but does not wrap the general FingerTree engine, because logarithmic lazy range actions
require their own cached logical-measure/pending-tag invariant. It never references Tungsten.
