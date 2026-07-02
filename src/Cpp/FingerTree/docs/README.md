# C++ FingerTree Documentation

- Status: Informational
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Audience: Maintainers and AI agents implementing the C++ port
- Scope: C++ port documentation under `src/Cpp/FingerTree/docs`

## Current Documents

- [Port plan](port-plan.md) defines the implementation sequence, public surface, validation strategy, and
  C#-to-C++ design mapping.
- [Port plan editorial notes](port-plan-editorial-notes.md) records the non-obvious hazards behind the plan,
  especially lazy memoization, type erasure, comparator regimes, and C++ memory-model rules.
- [Implementation notes](implementation-notes.md) records concrete C# comparisons, justified C++ divergences,
  validation observations, and links to any defect reports found during the port.
- [API notes](api-notes.md) records C++ API conventions and active differences from the C# workspace.
- [Usage guide](usage.md) shows public include paths, value-semantics patterns, common update flows, and facade quick starts.
- [Validation](validation.md) records build, test, stress, warning-policy, generated-output, and
  benchmark-harness-status guidance for this workspace.
- [Tests README](../tests/README.md) maps the native smoke runner, domain test files, direct executable path, and
  tearable concurrency stress control.

## Independent Reviews

Three reviews of the finished port were produced independently (separate sessions and toolchains,
no shared context). They originally converged on the same top findings:

- `try_locate` / `try_locate_by_measure` discarded the miss-path `measure_before`;
- the named-operation free-function layer was missing;
- structure-level tree/rope concurrency stress tests were absent;
- Milestone 8 samples and benchmarks were still missing.

The first correction pass is recorded in
[`implementation-notes.md`](implementation-notes.md#checkpoint-independent-review-corrections): the locate result
shape is now total, the named-operation layer is present, and structure-level tearable concurrency stress tests are
part of `fingertree.smoke`. The current active gap is Milestone 8: no C++ `samples/` or `benchmarks/` directory is
checked in yet. Stateful command-sequence shrinking, install/export packaging, CI, and multi-compiler or
ThreadSanitizer coverage also remain future validation work.

The review reports are historical snapshots. Keep them intact as evidence of the review process, and use the
current [API notes](api-notes.md), [usage guide](usage.md), [validation guide](validation.md), and correction
checkpoint for today's contract.

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
