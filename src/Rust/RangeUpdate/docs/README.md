# Rust Range-Update Sequence Documentation

- Created (UTC): 2026-07-15T10:22:25Z
- Repository HEAD: 83d2d4bc69d8c77980127695f656f0aa5ecf56bd
- Audience: Consumers, maintainers, reviewers, and port authors
- Scope: Documentation index, cursor contract, and intentional Rust mappings for
  `tools-data-structures-range-update`

- [Crate overview and Rust API mapping](../README.md) describes the algebra trait, public surface,
  lazy invariant, persistence, result mapping, complexity, and focused validation command.
- [Normative C# contract](../../../CSharp/docs/FingerTree/range-update-sequence.md) defines the shared
  monoid/action laws, directional composition, callback and identity behavior, structural bounds,
  and cross-port validation obligations.
- [Executable test map](../tests/README.md) explains the deterministic public-API evidence.
- [Repository porting guide](../../../../docs/guides/porting-and-semantic-parity.md) governs future
  semantic changes across sibling workspaces.

## Positional and measured cursor

`RangeUpdateSequenceCursor<T, A>` is the crate's public cursor. `RangeUpdateSequence::cursor()`
creates one at gap zero and is infallible; `cursor_at(position)` accepts every boundary in `0..=len`
and returns `Result<_, RangeUpdateError>`. The cursor retains one immutable sequence version plus a
validated `usize` gap.

It is a **Profile R root-plus-position semantic checkpoint**. It retains no AVL frames, no
normalized edit spine, and no carried-tag context of its own: each operation delegates to the
owning sequence's ordinary indexed, range, or measure operation, which already carries inherited
lazy tags correctly on read and pushes them immutably on structural descent. Consequently the
cursor claims none of the C# rope tier's focused representation, memoization, callback ceiling,
allocation bound, or amortized-locality guarantees, and none of the focused implicit-AVL frame
bounds described in the repository cursor design.

Rust ownership supplies the invalid-default contract. The type has no `Default`, no moved-from state
is observable, and use after a move is a compile-time error, so every nameable cursor is fully
initialized; a cursor over an empty sequence is an ordinary value at gap zero for which
`is_at_start` and `is_at_end` are both true. Navigation and edits return new cursors, so every
retained cursor stays an independently branchable version, and `snapshot()` returns an owned
root-sharing sequence without consuming the cursor. Unlike the FingerTree crate, which splits its
`T: Clone` requirement per operation, the whole cursor surface here — creation, navigation, peeks,
measures, and edits alike — sits behind `T: Clone`, because a peek must materialize an owned logical
value and the sequence path-copies on every structural descent.

### Mixed `Result` and `Option` channels

This crate is the one place in the repository's Rust cursor tier where a single cursor type mixes
both channels, following the owning sequence's existing split between fallible edits and
nonthrowing indexed observation. Callers must not assume a uniform channel:

| Channel | Members |
| --- | --- |
| `Result<_, RangeUpdateError>` | `RangeUpdateSequence::cursor_at`, `seek`, `insert`, `measure_previous`, `measure_next`, `apply_previous`, `apply_next` |
| `Option<Self>` | `move_previous`, `move_next`, `delete_previous`, `delete_next`, `replace_next` |
| `Option<T>` | `peek_previous`, `peek_next` |
| Infallible | `RangeUpdateSequence::cursor`, `len`, `is_empty`, `position`, `is_at_start`, `is_at_end`, `measure_before`, `measure_after`, `snapshot` |

`seek` is `Result`-valued rather than `Option`-valued because it forwards to `cursor_at`, while unit
movement is `Option`-valued because a boundary is not an error. Both peeks return **owned**
`Option<T>` rather than borrowed references, because a pending ancestor tag may synthesize the
logical value that has no stored counterpart — the same reason `RangeUpdateSequence::get` returns
`Option<T>`.

### Surface, gap conventions, and tag semantics

Positional members are `len`, `is_empty`, `position`, `is_at_start`, `is_at_end`, `peek_previous`,
`peek_next`, `move_previous`, `move_next`, `seek`, `insert`, `delete_previous`, `delete_next`,
`replace_next`, and `snapshot`. Measure and range members are `measure_before`, `measure_after`,
`measure_previous(count)`, `measure_next(count)`, `apply_previous(count, tag)`, and
`apply_next(count, tag)`.

- `insert` leaves the gap after the inserted value at `position + 1`.
- `delete_previous` is backspace and moves the gap left to `position - 1`.
- `delete_next` and `replace_next` address the next element and keep the gap fixed.
- `apply_previous(k, tag)` targets `[position - k, position)` and `apply_next(k, tag)` targets
  `[position, position + k)`; both keep the gap fixed.

`replace_next` is unconditional because the generic core has no element-equality policy, and a newly
inserted or replacement element is a current logical value that no older tag retroactively changes.
`measure_previous`, `measure_next`, `apply_previous`, and `apply_next` validate their complete range
with the sequence's subtraction-safe `count <= available` check before any algebra callback, so a
zero-length measure returns the monoid identity without element or tag callbacks and a zero-length
or recognized-identity apply is a root-sharing no-op that never calls `is_identity` on an empty
range. `measure_before` combined with `measure_after` in that order equals the whole snapshot
measure, with all carried tags reflected; no inverse or commutativity is assumed.

### Honest local complexity

Let `n` be the sequence length. `len`, `is_empty`, `position`, the boundary predicates, cursor
creation, unit movement, `seek`, cloning, and `snapshot` are O(1) — they clone two handles and
rewrite an integer. Peeks are O(log n) tag-carrying descents that install no node and no mutable
cache. `insert`, `delete_previous`, `delete_next`, and `replace_next` are the sequence's O(log n)
path-copying edits.

Measure and range members inherit the owning sequence's bounds exactly. `measure_before` at the
start gap and `measure_after` at the end gap return the captured empty measure in O(1);
`measure_after` at the start gap and `measure_before` at the end gap return the cached root measure
in O(1); every proper prefix or suffix is an O(log n) descent that allocates no persistent node.
Neither performs a structural split. `apply_previous` and `apply_next` are O(1) when the requested
range is the whole sequence, because a nonidentity whole-sequence tag replaces only the root, and
O(log n) path copies otherwise. Because no frames are retained, a linear traversal by move-plus-peek
costs one O(log n) descent per step, and context space is O(1).

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
