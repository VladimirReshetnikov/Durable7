# C++ FingerTree Documentation

- Status: Initial documentation index
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Audience: Maintainers and AI agents implementing the C++ port
- Scope: C++ port documentation under `Cpp/FingerTree/docs`

## Current Documents

- [Port plan](port-plan.md) defines the implementation sequence, public surface, validation strategy, and
  C#-to-C++ design mapping.
- [Port plan editorial notes](port-plan-editorial-notes.md) records the non-obvious hazards behind the plan,
  especially lazy memoization, type erasure, comparator regimes, and C++ memory-model rules.
- [API notes](api-notes.md) records C++ API conventions and active differences from the C# workspace.
- [Validation](validation.md) records build, test, stress, and benchmark entry points for this workspace.
