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

## Independent reviews

Three reviews of the finished port were produced independently (separate sessions and toolchains,
no shared context). They converge on the same top findings — `try_locate`/`try_locate_by_measure`
discarding the miss-path `measure_before`, the missing named-operation free-function layer, and the
absent structure-level (tree/rope) concurrency stress test — and all three agree the shipped code
has no correctness or data-race defect. They are kept as separate documents.

The first correction pass for those converged findings is recorded in
[`implementation-notes.md`](implementation-notes.md#checkpoint-independent-review-corrections).

- [Independent review report](cpp-port-review-report-2026-06-30T20-06-08Z.md) (Opus 4.8) - outcome
  and process review backed by hands-on quantitative experiments: tear-free concurrent first reads,
  allocation-free endpoint/measure reads, flat branching-persistence marginal cost, and O(1)
  `reverse()`.
- [Port review](cpp-fingertree-port-review-2026-06-30T20-14-15Z.md) (Codex) - methodology,
  validation results, git-process findings, outcome findings, and recommendations.
- [Port quality review](cpp-port-quality-review-2026-06-30T20-17-39Z.md) (Sonnet 5) - 8-dimension
  multi-agent review with adversarial per-finding verification and hands-on concurrency/exception
  probes (clean under a plain build and AddressSanitizer), with findings ranked by severity and a
  prioritized fix list.
