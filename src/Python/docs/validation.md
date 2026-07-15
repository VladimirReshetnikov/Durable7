# Python validation

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87

Run `test.ps1` from this workspace. Its required gates are:

1. Ruff formatting and lint validation.
2. Strict Mypy analysis of the package and tests.
3. Pytest example, model, property, wire-format, adversarial, and concurrency tests.
4. Isolated PEP 517 source-distribution and wheel builds.
5. Twine metadata checks and a clean-environment installed-wheel smoke test.

The complete suite currently executes 128 example, property, model, adversarial, exact-wire,
failure-atomicity, and concurrency tests. Python 3.11.15 and 3.14.4 have both run the full suite;
the static gate targets the declared Python 3.11 language surface.

Ordered-set coverage includes exact first-representative and receiver-policy semantics, exhaustive
small final-index movements and relation truth tables, repeated sparse-label exhaustion with
retained branches, every range boundary, stable sort and algebra ordering, callback-failure
atomicity, concurrent snapshot readers, and generated construction/algebra/branching histories.

Recent HAMT coverage locks one-descent factory selection and failure atomicity, reusable detached
mutable-builder freezes (including the final two hash bits and collision buckets), routed map/set
construction, all transient-set relations, and hash-bag multiplicity, representative, overflow,
foreign-policy algebra, and generated collision models.

Use `test.ps1 -SkipInstall` after the pinned tools in `requirements-dev.txt` are already installed.
`-SkipPackageSmoke` is reserved for narrow local iteration and is not a complete validation result.
