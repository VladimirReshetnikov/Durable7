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
- [Implementation notes](implementation-notes.md) records concrete C# comparisons, justified C++ divergences,
  validation observations, and links to any defect reports found during the port.
- [API notes](api-notes.md) records C++ API conventions and active differences from the C# workspace.
- [Validation](validation.md) records build, test, stress, and benchmark entry points for this workspace.
- [Port quality review](cpp-port-quality-review-2026-06-30T20-17-39Z.md) is an independent review of
  the finished port's correctness, concurrency/memory-model fidelity, API completeness, test
  coverage, and build/documentation accuracy, with findings ranked by severity, a prioritized fix
  list, and a cross-check against a second, independently-run review.
