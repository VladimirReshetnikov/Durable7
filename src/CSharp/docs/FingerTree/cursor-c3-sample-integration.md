# Cursor C3 sample integration record

- Created (UTC): 2026-07-14T00:36:33Z
- Repository HEAD: 62ee0d88c84d0b4b0f34a32d2570e8ec89afdb61
- Status: Shipped in the C# Tour and Editor samples
- Audience: Maintainers reviewing the Axis 2 cursor vertical slice
- Scope: C3 sample state, snapshot cadence, transcripts, and validation

## Result

C3 completes the first end-to-end Axis 2 cursor slice. Both text samples keep
`MeasuredRope<char, int, NewlineMeasure>` as their text representation and use the shipped
`MeasuredRopeCursor<char, int, NewlineMeasure>` for local edit state. There is no parallel text type
and no change to UTF-16, newline, code-point, or grapheme semantics.

The samples use the measured C2 gate's snapshot cadence of sixteen local cursor edits. They retain
cursor versions during editing and materialize canonical ropes only at explicit display or commit
boundaries. Undo/redo display and the alternate-branch display are explicit boundaries; they are not
hidden snapshot-after-every-edit work.

## Tour

The Tour's undo/redo history is now `List<MeasuredRopeCursor<char, int, NewlineMeasure>>`. Its list
index is named `historyPosition`, avoiding the old collision between a history index and an edit
cursor. Three logical commands perform sixteen cursor edits without snapshotting:

1. build `hello` in three local insertions;
2. append ` world` in six insertions; and
3. move six gaps back to position five and insert `,\nbrave` in seven insertions.

The first display materializes the sixteenth edit version. Undo and redo move among retained cursor
versions and snapshot only the version being displayed. The transcript locks the final text, redo
result, history position, and `16 cursor edits` cadence.

## Editor

The Editor retains its Unicode and newline-extra tour, then adds a measured-cursor act over
`alpha\nbeta\ngamma`. Starting from the beginning of the second line, it:

- inserts a local prefix;
- moves across `beta`, deletes the previous character, and restores it;
- types an emoji, ordinary text, a newline, and a check mark;
- reports the cursor gap as zero-based line 2, column 1 without first snapshotting;
- snapshots after exactly sixteen local edits; and
- creates an `alternate ` branch from the retained pre-edit cursor, proving the original and main
  branch remain unchanged.

The transcript locks the main text, cursor coordinates, alternate text, retained original, and
explicit snapshot-cadence statement.

## Validation

`SampleSmokeTests` drives both `Run(TextWriter)` entry points and checks the C3 transcript markers.
The sample projects also remain project references of the FingerTree test project, so the ordinary
solution and test gates compile them. C3 changes neither the cursor representation nor C2's locked
performance policy; it exercises the already accepted surface at the measured cadence.
