# FingerTree

- Status: Draft implementation workspace
- Created (UTC): 2026-04-27T18:33:25Z
- Repository HEAD: df8ea08345ca22ba76e6f4fc7e92d0fd41686de3
- Audience: Maintainers implementing and reviewing the finger-tree deque
- Scope: Project layout and validation entry points for `src/DataStructures/FingerTree`

`src/DataStructures/FingerTree` contains the .NET 10 C# preview workspace for `Tools.DataStructures.FingerTree`, a planned persistent catenable deque backed by a simplified finger tree.

The current source project exposes the public API shape and intentionally throws `NotImplementedException` from its members. The test project is a TDD contract suite: it is expected to fail until the implementation is added.

## Layout

- `FingerTree.sln` is the solution entry point.
- `src/Tools.DataStructures.FingerTree/` contains the public library.
- `tests/Tools.DataStructures.FingerTree.Tests/` contains the xUnit TDD suite.
- `docs/` contains API and algorithm design references.

## Validation

Use the local .NET SDK:

```powershell
dotnet test .\FingerTree.sln
```

The initial scaffold should compile, then fail behavior tests because the public API is not implemented yet.
