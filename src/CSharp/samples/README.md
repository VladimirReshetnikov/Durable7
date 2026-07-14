# Samples

- Created (UTC): 2026-06-14T18:28:03Z
- Updated (UTC): 2026-07-02T20:50:27Z
- Repository HEAD: 1cc49d57d1949230877f1e10c24465f7905905dd
- Audience: Maintainers and users exploring the C# FingerTree sample programs
- Scope: Runnable sample tours under `src/CSharp/samples`

Runnable demonstrations of `Tools.DataStructures.FingerTree`. The samples are ordinary console
projects, but their main logic lives in `Run(TextWriter)` methods so the test suite can smoke-test
the deterministic transcript markers without depending on console I/O.

From `src/CSharp/samples`, run a sample with:

```powershell
dotnet run --project .\Tools.DataStructures.FingerTree.Tour -c Release
dotnet run --project .\Tools.DataStructures.FingerTree.Showcase -c Release
dotnet run --project .\Tools.DataStructures.FingerTree.Editor -c Release
```

From the workspace root `src/CSharp`, prefix the project paths with `samples\`.

## `Tools.DataStructures.FingerTree.Tour`

A short, narrated end-to-end tour built around a persistent text buffer, in three acts:

- **Undo/redo** over retained measured cursor versions — three logical commands perform sixteen
  local cursor edits before the first display snapshot; the history index is explicitly a
  `historyPosition`, not a second cursor concept.
- **O(log n) line and column navigation** via the newline measure (`LineColumnOf`, `GetLine`, `OffsetOf`).
- **Lock-free concurrent reading** — a background thread snapshots a growing buffer while a writer publishes successive versions through a single volatile reference; it takes millions of consistent, never-torn snapshots with no locks held.

The program runs a bounded, deterministic scenario and exits.

```powershell
dotnet run --project .\Tools.DataStructures.FingerTree.Tour -c Release
```

## `Tools.DataStructures.FingerTree.Showcase`

The "one measured tree, many data structures" thesis, in four acts — the same general measured finger tree becomes each structure purely by choice of measure:

- **Meldable priority queue** (minimum-priority measure): enqueue tasks, meld two queues, drain in priority order.
- **Weighted random sampling** (cumulative-sum measure): 100,000 seeded draws land within a fraction of a percent of the configured weights.
- **Order-statistic sorted set**: k-th element, rank, range, and set algebra.
- **Interval index** (maximum-endpoint measure): overlap queries against a set of intervals.

The acts are seeded and reproducible. The logic is exposed as `ShowcaseProgram.Run(TextWriter)` so it is smoke-tested.

```powershell
dotnet run --project .\Tools.DataStructures.FingerTree.Showcase -c Release
```

## `Tools.DataStructures.FingerTree.Editor`

The editor-grade text extras, in five acts over measured text:

- **Three different lengths** — a document mixing an emoji (a surrogate pair) and a decomposed accented letter has 29 UTF-16 chars, 28 code points, and 25 grapheme clusters.
- **Newline style detection** (`CrLf`) and carriage-return-stripped lines.
- **Offset addressing** — converting between character offsets and code-point / grapheme indices.
- **Localized cursor editing** — a sixteen-edit insertion/deletion/movement burst containing Unicode,
  line/column reporting at the gap, and an alternate branch from a retained old cursor.

Both main edit bursts use the measured C2 benchmark cadence of sixteen and snapshot only at explicit
display/commit boundaries. The scenarios are deterministic, exposed through `Run(TextWriter)`, and
smoke-tested. See the [C3 integration record](../docs/FingerTree/cursor-c3-sample-integration.md).

```powershell
dotnet run --project .\Tools.DataStructures.FingerTree.Editor -c Release
```

## Smoke Tests

`tests/Tools.DataStructures.FingerTree.Tests/SampleSmokeTests.cs` drives `TourProgram.Run`,
`ShowcaseProgram.Run`, and `EditorProgram.Run` with a captured writer and checks transcript markers.
Run the focused sample gate from `src/CSharp` with:

```powershell
.\test.ps1 -Filter FullyQualifiedName~SampleSmokeTests
```

Use the full workspace validation guide when sample changes also touch library APIs, build shape, or
performance claims: [`../docs/FingerTree/validation.md`](../docs/FingerTree/validation.md).
