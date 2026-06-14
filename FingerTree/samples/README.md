# Samples

Runnable demonstrations of `Tools.DataStructures.FingerTree`.

## `Tools.DataStructures.FingerTree.Tour`

A short, narrated end-to-end tour built around a persistent text buffer, in three acts:

- **Undo/redo** as a cursor over O(1) snapshots — each retained version is a reference, not a copy (structural sharing).
- **O(log n) line and column navigation** via the newline measure (`LineColumnOf`, `GetLine`, `OffsetOf`).
- **Lock-free concurrent reading** — a background thread snapshots a growing buffer while a writer publishes successive versions through a single volatile reference; it takes millions of consistent, never-torn snapshots with no locks held.

The program runs a bounded, deterministic scenario and exits.

```bash
dotnet run --project samples/Tools.DataStructures.FingerTree.Tour -c Release
```

## `Tools.DataStructures.FingerTree.Showcase`

The "one measured tree, many data structures" thesis, in four acts — the same general measured finger tree becomes each structure purely by choice of measure:

- **Meldable priority queue** (minimum-priority measure): enqueue tasks, meld two queues, drain in priority order.
- **Weighted random sampling** (cumulative-sum measure): 100,000 seeded draws land within a fraction of a percent of the configured weights.
- **Order-statistic sorted set**: k-th element, rank, range, and set algebra.
- **Interval index** (maximum-endpoint measure): overlap queries against a set of intervals.

The acts are seeded and reproducible. The logic is exposed as `ShowcaseProgram.Run(TextWriter)` so it is smoke-tested.

```bash
dotnet run --project samples/Tools.DataStructures.FingerTree.Showcase -c Release
```

## `Tools.DataStructures.FingerTree.Editor`

The editor-grade text extras, in four acts over a document built with `RopeBuilder`:

- **Three different lengths** — a document mixing an emoji (a surrogate pair) and a decomposed accented letter has 29 UTF-16 chars, 28 code points, and 25 grapheme clusters.
- **Newline style detection** (`CrLf`) and carriage-return-stripped lines.
- **Offset addressing** — converting between character offsets and code-point / grapheme indices.

Deterministic, exposed as `EditorProgram.Run(TextWriter)` and smoke-tested.

```bash
dotnet run --project samples/Tools.DataStructures.FingerTree.Editor -c Release
```
