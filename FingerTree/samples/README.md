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
