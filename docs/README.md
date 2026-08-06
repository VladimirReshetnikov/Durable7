# Durable7 Documentation

- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: Maintainers and AI agents working in the Durable7 repository
- Scope: Repository-level documentation index

This directory contains repository-level guides, reference material, and extraction provenance.
Library-specific design and API documentation lives beside each workspace under `src/`.

## Field guide

- [The Durable7 Field Guide](book/README.md) - `Persistent Data Structures`, a single 133-page PDF prepared in LaTeX covering every data-structure family in the repository: representation, design rationale, complexity, the exact point at which each guarantee stops, and the nine-language spellings. Source and built PDF are both committed. It is a companion to, not a replacement for, the workspace API specifications.

## Guides

- [Guides index](guides/README.md) - task-oriented repository procedures.
- [Repository onboarding](guides/repository-onboarding.md) - end-to-end orientation for choosing workspaces, task scope, documentation responsibilities, and validation evidence.
- [Agent workflows](guides/agent-workflows.md) - compact task-conditional workflow guidance inherited from the Tools repository where relevant.
- [Build and validation](guides/build-and-validation.md) - repository-wide validation matrix, exact build/test commands, CMake cache notes, and Markdown checks.
- [Documentation maintenance](guides/documentation-maintenance.md) - documentation placement, writing standards, metadata, and validation.
- [Porting and semantic parity](guides/porting-and-semantic-parity.md) - workflow for carrying behavior and documentation changes across C#, C++, C, Haskell, Kotlin, Rust, TypeScript, Python, and OCaml workspaces.

## Reference

- [Reference index](reference/README.md) - durable cross-workspace reference material.
- [Data structure catalog](reference/data-structure-catalog.md) - cross-language catalog of repository-owned data-structure families, including the strict persistent bimap and the current five-structure derived tranche in all nine languages, public entry points, and primary references.
- [Derived structure catalog](reference/derived-structure-catalog.md) - historical verified composition survey with current disposition notes for the shipped bimap, ordered map, hash multimap/relation, and other facades, plus the candidates that remain unbuilt and why. Consult the data-structure catalog for the authoritative shipped surface.
- [Frontier structure catalog](reference/frontier-structure-catalog.md) - current-state record of shipped Axis 1/selected Axis 2 work, nine-language semantic coverage and OCaml checkpoint boundaries, and remaining candidates beyond composition: new cores, hybrid/adaptive representation tiers, niche-specialized siblings, verdicts, and sequencing.
- [Documentation navigation matrix](reference/navigation-matrix.md) - task-oriented entry points for usage, API, validation, porting, history, and maintenance work.
- [Semantic contracts](reference/semantic-contracts.md) - shared behavior, ownership, policy, ordering, and documentation obligations across repository-owned data structures.
- [Test suite map](reference/test-suite-map.md) - cross-workspace map of test runners, routine commands, stress knobs, sample smoke tests, and benchmark boundaries.
- [Workspace map](reference/workspace-map.md) - language-first layout, data-structure workspace roles, port lineage, and documentation placement rules.

## Proposals

- [Ancestral slice queue (2026-07-25)](proposals/ancestral-slice-queue-2026-07-25.md) - experimental C# persistent queue whose handles denote appendable intervals of an append-tree path over an incremental level-ancestor arena; shipped Myers backend bounds are kept separate from the proved Alstrup–Holm O(1) instantiation.
- [Bilateral ancestral deque (2026-07-25)](proposals/bilateral-ancestral-deque-2026-07-25.md) - experimental C# restricted persistent deque of two oppositely oriented ancestry intervals with O(1) reverse and at-most-two-query slice/split.
- [Contextual rank sequence (2026-07-25)](proposals/contextual-rank-sequence-2026-07-25.md) - experimental C# measured finger-tree facade lifting a finite deterministic event machine into an all-start-state summary monoid for O(s log n) contextual event rank/select.
- [Persistent delta map (2026-07-25)](proposals/persistent-delta-map-2026-07-25.md) - experimental C# checkpoint-differential sorted map with a coalesced exact net-change index, O(1) checkpoint/rollback, and Theta(k + 1) change enumeration.
- [Persistent ancestral connection forest (2026-07-29)](proposals/persistent-ancestral-connection-forest-2026-07-29.md) - experimental C# branching insertion-only union-find over a sparse CHAMP parent map that answers first-connected-at-which-ancestor-version queries without searching history.
- [Persistent monotone action heap (2026-07-29)](proposals/persistent-monotone-action-heap-2026-07-29.md) - experimental C# Brodal–Okasaki heap with lazily composed monotone priority actions: O(1) worst-case whole-heap clamp/constant transforms preserving O(1) insert/meld and O(log n) delete-min.
- [Persistent run-delta vector (2026-07-29)](proposals/persistent-run-delta-vector-2026-07-29.md) - experimental C# fixed-length current/checkpoint RRB vector with an exact maximal dirty-run index, Theta(r) run discovery, and O(log n) run-length-independent accept/revert splices.
- [Repository-wide persistent cursor design](proposals/repository-wide-persistent-cursor-design.md) - implemented cross-language contract and exhaustive applicability record for cursors and private edit paths across every persistent family, including shipped family/language coverage, shared version/focus semantics, explicit exclusions, representation and complexity boundaries, ownership, validation, and the implementation ledger.
- [Benchmark-independent next data structures (2026-07-14)](proposals/benchmark-independent-next-structures-2026-07-14.md) - C#-first design and original eight-language execution record for persistent-HAMT single-pass updates, `PersistentHashBag`, the neutral `PersistentOrderedSet`, and `RangeUpdateSequence`; the later OCaml extension is indexed by the current catalogs. Benchmarks were not run and remain postponed for an isolated session.
- [Benchmark-independent cross-language completion audit (2026-07-15)](reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md) - detailed per-language implementation, test, package-wiring, dependency-boundary, review-report, and serialized-validation evidence for the completed four-surface proposal.
- [PersistentBiMap cross-language completion audit (2026-07-15)](reviews/persistent-bimap-cross-language-completion-2026-07-15.md) - detailed shared contract and evidence for the original strict eight-language bimap shipment; the OCaml extension is documented in the current catalog and OCaml test map.
- [Axis 2 final plan: lifecycle and sequence cursors](proposals/axis2-lifecycle-and-sequence-cursors.md) - authoritative C#-first plan: a version-bound Rope/MeasuredRope cursor leads, while one-way CHAMP transients and a fixed-layout frozen hash tier advance independently through workload, representation, correctness, memory, and break-even gates; automatic size/key specialization remains deferred.
- [Axis 2, cursor-first alternative (2026-07-13)](proposals/axis2-cursor-first-alternative-2026-07-13.md) - historical sequencing proposal incorporated into the final plan with corrections. It supplied cursor priority, a frozen signal gate, and branched-history scrutiny; its current-consumer, calendar-spike, transient-workload, and canonical-order claims are not the final contract.
- [Next data structures (2026-07-09)](proposals/new-data-structures-2026-07-09.md) - historical, partially realized slate: structural HAMT equality/diff, Patricia, and the C# hash bag shipped; Axis 2 supersedes its cursor schedule, and the remaining builder/facade/numerics ideas require current catalog and consumer evidence.
- [Persistent set of GUIDs design study (2026-07-12)](proposals/persistent-guid-set-design-study-2026-07-12.md) - which shipped collection fits a persistent GUID set best (`PersistentHashSet<Guid>` by default), whether a custom full-key 128-bit trie beats it (a capability/robustness win, not a faster membership set — decisive only for untrusted GUIDs, ordered/range queries, or structural set algebra), the measured GUID sort-order facts, a workload-indexed decision table, and consumer-gated adoption economics.

## Reviews

- [TypeScript collection port (2026-08-06)](reviews/typescript-collection-port-2026-08-06.md) - records the ninth and final port of the seven research-derived collections, completing every language: because it shipped after the complexity-parity campaign it is the only port that never weakened a bound, stating the reference's O(s) amortized endpoints and O(s log(min(n, m))) concatenation outright and earning the unconditional CHAMP factor by a verified-injective hash; a sixteen-agent adversarial audit confirmed ten findings of which exactly one was behavioural - a removal that published a non-canonical empty - while the largest module produced none, and four JavaScript hazards (no shared-memory lock, unreachable overflow ceilings, value-equal `===` defeating identity invariants, and `undefined` as a legal stored value) are handled rather than documented away.
- [Complexity parity retrospective (2026-08-06)](reviews/complexity-parity-retrospective-2026-08-06.md) - the story and transferable lessons of the campaign the census drove: how five join-tree cores became lazy Hinze–Paterson finger trees with byte-identical probe signatures across five languages, why amortization under persistence forced defunctionalized laziness everywhere (including past OCaml's native Lazy), the two equalize-upward events nobody planned, the two maintainer rulings recorded instead of hidden, and the evidence discipline - probes for what correctness tests cannot see, mutation checks with characteristic failure signatures - that made cross-language parity a measured property rather than a promise.
- [Cross-language complexity parity census (2026-08-05)](reviews/complexity-parity-census-2026-08-05.md) - the completed work order of the complexity-equalization campaign: a code-first census of all nine workspaces that found only four carrying the real lazy Hinze–Paterson core, ranked every representation and bound divergence in five classes, and now records every row struck - five join-tree cores replaced (all measuring byte-identical probe numbers), OCaml's six placeholder substrates rebuilt, Haskell's sorted family moved onto its own finger tree, two conditional guarantees made unconditional, and two maintainer-ruled regressions (arena block-boundary spike, sorted rank-select) documented identically everywhere rather than hidden.
- [Python collection port (2026-08-05)](reviews/python-collection-port-2026-08-05.md) - records the Python port of the seven research-derived collections and the two seams they share, taking coverage to eight languages and leaving only TypeScript: the backend seam becomes a structural `Protocol` that deliberately keeps representation statistics off the contract, bounds move in both directions with the run-delta vector earning a *stronger* worst-case claim than the baseline, and a two-wave adversarial audit confirmed eight findings and refuted five - none a behavioural defect, but one of them a shared test whose branching shape was silently a depth-zero star, hiding five distinct corruptions of the arena's jump arithmetic.
- [OCaml collection port (2026-08-05)](reviews/ocaml-collection-port-2026-08-05.md) - records the OCaml port of the seven research-derived collections and their shared level-ancestor seam, taking coverage to seven languages: three substrates deliver less than the baseline and the bounds say so, two OCaml-specific hazards (constant-record sharing and non-reflexive float equality) are handled rather than documented away, and a six-finding audit caught a constructor that could publish a corrupt cached summary as success.
- [C++ collection port (2026-08-05)](reviews/cpp-collection-port-2026-08-05.md) - records the C++ port of the seven research-derived collections and their shared level-ancestor seam, taking coverage to six languages: the backend, action algebra, comparers, and event machine all become compile-time concepts so the reference's runtime policy gates become compile errors, two bounds are earned that other ports must weaken, and a ten-finding adversarial audit caught three genuine defects including an unsafe arena allocation and an iterator that violated the equality requirement its own test asserted.
- [Kotlin collection port (2026-08-05)](reviews/kotlin-collection-port-2026-08-05.md) - records the Kotlin/JVM port of the seven research-derived collections and their shared level-ancestor seam, taking coverage to five languages: the arena is faithful because the JVM supplies what the reference assumes, two equality abstractions were consolidated into one retained policy, and an eight-finding adversarial audit found no correctness defect but four vacuous test assertions of one recurring shape.
- [Haskell collection port (2026-08-05)](reviews/haskell-collection-port-2026-08-05.md) - records the Haskell port of the seven research-derived collections and their shared level-ancestor seam, taking coverage to four languages: the arena disappears because a node is its own handle, policies become retained records of functions, and two substrate-driven bounds are stated weaker while the contextual sequence keeps a bound the Rust port must weaken.
- [Three-language parity audit (2026-08-05)](reviews/three-language-parity-audit-2026-08-05.md) - member-by-member adversarial verification that the Rust and C ports of the seven research-derived collections and the shared ancestor arena are complete and faithful to the C# baseline: no correctness defect in any port; nine findings fixed, including the C delta map's undelivered O(log(k + 1)) range-seek comparison bound and the missing C run-delta policy accessor.
- [Rust collection port review (2026-08-04)](reviews/rust-collection-port-review-2026-08-04.md) - records the promotion of the seven research-derived collections out of the `*.Experimental` namespaces and their Rust port, the seven intentional divergences from the C# baseline, and the adversarial parity verification that confirmed 12 of 17 candidate findings with no correctness defect in any port.
- [Experimental collections review (2026-07-29)](reviews/experimental-collections-review-2026-07-29.md) - line-by-line correctness, complexity-claim, API, and documentation review of all seven `*.Experimental` C# collections: no correctness defects; one validation fix and the missing heap/forest family-doc sections applied; duplication, catalog-indexing, and enhancement findings recorded for maintainer decision.
- [Persistent cursor cross-language review (2026-07-19)](reviews/persistent-cursor-cross-language-review-2026-07-19__3f7c1a9e4d02.md) - open review of every public cursor family in all nine ports: complete feature-parity matrix, eight validated fixes (C# invalid-default and fragment-cache defects, five C memory-safety defects, an OCaml measure-seek defect), and recorded backlog covering the nine-port Ordered multimap contract violation, silently super-logarithmic operations, design-document complexity claims no port delivers, and the workspace-documentation gap for every non-rope family.
- [Axis 2 lifecycle and sequence-cursor plan review (2026-07-13)](reviews/axis2-lifecycle-and-cursors-review-2026-07-13.md) - historical review of the pinned pre-synthesis plan. Its sequencing, frozen-signal, branched-history, transient-workload, and documentation findings are dispositioned—some accepted and some corrected—in the authoritative final plan.
- [Axis 1 new-cores review — round 2 (2026-07-12)](reviews/axis1-new-cores-review-round2-2026-07-12.md) - resolved review: verifies every first-round remediation and records closure of all fresh follow-ups, including non-vacuous cross-port CHAMP routing/topology gates, the conservative RRB cap, shared Rust Brodal comparer identity, C++ PSQ pruning, C Patricia sharing, the reverse Kotlin Ctrie race, and Patricia assertion coverage.
- [`PersistentHashSet<T>` structural set-algebra remediation (2026-07-12)](reviews/hamt-set-algebra-nonstructural-2026-07-12.md) - resolved review: records the former element-wise limitation and the completed Phase 2 remediation across all six ports, including same-type structural algebra, reference pruning, cached cardinalities, policy contracts, and failure-atomic C coverage.
- [Axis 1 new-cores review (2026-07-12)](reviews/axis1-new-cores-review-2026-07-12.md) - resolved and superseded first-round review of the shipped Axis 1 cores; its remediation table closes the original Critical-through-Low findings, and the round-2 report verifies them against source.
- [Cross-language implementation review (2026-07-11)](reviews/cross-language-implementation-review-2026-07-11.md) - resolved seven-pass follow-up review; its addendum records the later Rust/C++ HAMT builders, Kotlin logarithmic bounds, and C# allocation-test de-flake that close the original three-item backlog.
- [Cross-language implementation review (2026-07-10)](reviews/cross-language-implementation-review-2026-07-10.md) - resolved nine-pass correctness, semantic-parity, and complexity-parity review; its addendum maps every historical open item to the subsequent implementation and replacement validation evidence.

## Migration

- [Migration index](migration/README.md) - extraction provenance, path-history records, and retained history-rewrite artifacts.
- [Migration provenance](migration/extraction-provenance.md) - source repository, source HEAD, filter command, validation notes, and follow-up policy for the extraction from Tools.
- [Language-first reorganization](migration/language-first-reorganization.md) - old-to-new path map and documentation follow-up record for the 2026-07 move under `src/`.
- [Filter-repo commit map](migration/filter-repo-commit-map.tsv) - retained old-to-new commit mapping for the extracted history.

## Placement

Put repository-wide task procedures under `docs/guides`, durable cross-workspace maps under
`docs/reference`, forward-looking design proposals under `docs/proposals`, point-in-time review
reports under `docs/reviews`, extraction/history records under `docs/migration`, and long-form
typeset documents (LaTeX source plus the built PDF) under `docs/book`.

Documentation for the shared build and test tooling lives with the tooling, in
[`eng/README.md`](../eng/README.md), not here.

Put C# HAMT implementation, API, and validation documents under
[src/CSharp/docs/Hamt](../src/CSharp/docs/Hamt/README.md), C HAMT port documents under
[src/C/Hamt/docs](../src/C/Hamt/docs/README.md), and C++ HAMT port documents under
[src/Cpp/Hamt/docs](../src/Cpp/Hamt/docs/README.md). Put C# FingerTree usage, implementation, API,
validation, benchmark, and algorithm documents under
[src/CSharp/docs/FingerTree](../src/CSharp/docs/FingerTree/README.md), with native FingerTree usage,
API, port, and validation documents under [src/Cpp/FingerTree/docs](../src/Cpp/FingerTree/docs/README.md) and
[src/C/FingerTree/docs](../src/C/FingerTree/docs/README.md), and Rust FingerTree-family documents under
[src/Rust/FingerTree/docs](../src/Rust/FingerTree/docs/README.md). Put Kotlin HAMT and FingerTree-family
documents under [src/Kotlin/Hamt/docs](../src/Kotlin/Hamt/docs/README.md) and
[src/Kotlin/FingerTree/docs](../src/Kotlin/FingerTree/docs/README.md). Put Haskell package entry points under
[src/Haskell](../src/Haskell/README.md), with test coverage notes beside each package's cabal test
executable. Put the neutral C# insertion-ordered-set overview, usage, API, and validation documents
under [src/CSharp/docs/Ordered](../src/CSharp/docs/Ordered/README.md).
TypeScript package API and validation notes live under
[src/TypeScript/docs](../src/TypeScript/docs/api-notes.md), with its executable test map under
[src/TypeScript/test](../src/TypeScript/test/README.md).
Python package API and validation notes live under
[src/Python/docs](../src/Python/docs/api-notes.md), with its pytest/Hypothesis test map under
[src/Python/tests](../src/Python/tests/README.md).
OCaml package API and validation notes live under
[src/OCaml/docs](../src/OCaml/docs/api-notes.md), with its Alcotest/QCheck map under
[src/OCaml/tests](../src/OCaml/tests/README.md).
