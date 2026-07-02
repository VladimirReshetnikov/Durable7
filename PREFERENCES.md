# Project Preferences

- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3

This file records inferred project and owner preferences that are useful for future work in the standalone DataStructures repository.

- Treat documentation as living current-state engineering memory. When code moves or responsibilities change, update active README and docs links in the same sweep.
- Prefer deterministic complexity guards over timing assertions. Comparer counts, operation counts, allocation ceilings, and retained-version marginal-cost checks are better regression tests for persistent data structures than wall-clock thresholds.
- Prefer API documentation that explains contracts, invariants, complexity, allocation behavior, and persistence/concurrency consequences rather than restating signatures.
- Preserve the split between the tuned `FingerTreeDeque<T>` and the general measured `FingerTree<TElement, TMeasure, TMeasureOps>` unless a future design deliberately changes that public shape.
- Keep external study material clearly segregated under `src/CSharp/FingerTree/docs/external` and never imply that it is covered by the repository license.
- Commit self-contained validated changes directly on `main`; add a remote and push only when publication is intentional.
