# Project Preferences

- Created (UTC): 2026-06-30T01:28:46Z
- Updated (UTC): 2026-07-02T20:58:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: Maintainers and AI agents preserving repository-specific working preferences
- Scope: Owner and project preferences that should guide future Durable7 work

This file records inferred project and owner preferences that are useful for future work in the standalone
Durable7 repository. For procedural rules, use the root [README](README.md), [AGENTS.md](AGENTS.md), and
the [documentation maintenance guide](docs/guides/documentation-maintenance.md).

- Treat documentation as living current-state engineering memory. When code moves or responsibilities change, update active README and docs links in the same sweep.
- Prefer deterministic complexity guards over timing assertions. Comparer counts, operation counts, allocation ceilings, and retained-version marginal-cost checks are better regression tests for persistent data structures than wall-clock thresholds.
- Prefer API documentation that explains contracts, invariants, complexity, allocation behavior, and persistence/concurrency consequences rather than restating signatures.
- Preserve the split between the tuned `FingerTreeDeque<T>` and the general measured `FingerTree<TElement, TMeasure, TMeasureOps>` unless a future design deliberately changes that public shape.
- Keep external study material clearly segregated under `src/CSharp/FingerTree/docs/external` and never imply that it is covered by the repository license.
- Commit self-contained validated changes directly on `main`; because `origin` is configured, push to
  `origin/main` unless Vladimir explicitly asks not to.
