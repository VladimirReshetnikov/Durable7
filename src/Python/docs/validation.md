# Python validation

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87
- Updated (UTC): 2026-07-17T00:25:16Z
- Updated Repository HEAD: a26aac8f4ec2fa60a2d4871568c2c02d24c9b2a2

Run `test.ps1` from this workspace. Its required gates are:

1. Ruff formatting and lint validation.
2. Strict Mypy analysis of the package and tests.
3. Pytest example, model, property, wire-format, adversarial, and concurrency tests.
4. Isolated PEP 517 source-distribution and wheel builds.
5. Twine metadata checks and a clean-environment installed-wheel smoke test.

The checked-in launcher keeps pytest single-process, disables pytest's optional on-disk cache, and
pins Rayon, CMake, and Make-compatible helper work to one worker. This keeps validation memory and
I/O predictable without weakening any executable gate. It selects `Scripts/python.exe` for Windows
virtual environments and `bin/python` for POSIX virtual environments, including the clean
installed-wheel smoke, so the same PowerShell entry point is portable across supported hosts.

The complete suite executes example, property, model, adversarial, exact-wire, failure-atomicity,
and concurrency tests. Python 3.11 and 3.14 lanes exercise the runtime surface; the static gate
targets the declared Python 3.11 language floor. Record exact test counts only from a completed full
gate, never by inferring parameterized or generated executions from source files.

The 2026-07-17 complete Python 3.13 serialized gate passes Ruff across 73 files, strict Mypy, all
225/225 pytest tests, isolated source and wheel builds, Twine metadata checks, and the installed-wheel
smoke test. The aggregate includes 31/31 new focused tests for the ordered multimap, map patch,
directed graph, indexed map, and chunked bit set. No benchmark is part of this gate.

Range-update coverage includes exhaustive affine-tag monoid/action checks, an ordered
noncommutative measure, all small split/rejoin boundaries, nested assignment/addition/affine tags,
edits through pending tags, nullable values and an active `None` tag, all callback failpoint
ordinals, validation precedence, cached-measure and AVL invariants, node sharing, deterministic
operation counters, independent snapshot iterators, concurrent readers, and generated branching
histories. These are structural and semantic gates; local benchmarks remain explicitly postponed
until they can run in isolation.

Ordered-set coverage includes exact first-representative and receiver-policy semantics, exhaustive
small final-index movements and relation truth tables, repeated sparse-label exhaustion with
retained branches, every range boundary, stable sort and algebra ordering, callback-failure
atomicity, concurrent snapshot readers, and generated construction/algebra/branching histories.

The ordered-map suite covers first-key/last-value construction, strict and replacing edits,
movement, ranges, stable sorting, relabel pressure, retained branches, and dual-index invariants.
Hash-multimap and relation suites cover no-empty-group contraction, exact counts, global
representatives, inverse roots, and symmetric removal. Interval-map coverage locks lexicographic
same-low keys, input validation, maximum-high pruning, policy identity, and cached annotations.
The new derived suites add grouped ordered-pair order, strict presence-safe patch composition,
explicit-vertex graph reversal, selector-atomic secondary indexing, and sparse-word rank/select
algebra with retained snapshots and structural diagnostics.

Recent HAMT coverage locks one-descent factory selection and failure atomicity, reusable detached
mutable-builder freezes (including the final two hash bits and collision buckets), routed map/set
construction, all transient-set relations, and hash-bag multiplicity, representative, overflow,
foreign-policy algebra, and generated collision models. The strict bimap gate covers two-domain
conflicts, policy-driven replacement, representatives, symmetric removal, presence-safe `None`,
failure atomicity, cached inverse identity, retained Hypothesis models, and concurrent readers.
The `RLock`-coordinated concurrent facade
adds retained-representative and present-`None` checks, exact publication generations, stable
canonical snapshots, all-collision generated histories, callback failure atomicity, and adversarial
same-thread reentry proving that compute retries instead of publishing an obsolete root. One-shot
reentrant hash/equality-policy hooks exercise `set`, `try_add`, `get_or_put`, and `remove` against
same-key and different-key nested publications, including exact return values, cached factory use,
and generation accounting.

Use `test.ps1 -SkipInstall` after the pinned tools in `requirements-dev.txt` are already installed.
`-SkipPackageSmoke` is reserved for narrow local iteration and is not a complete validation result.
All commands in the launcher run sequentially. Do not overlap Ruff, Mypy, pytest, package building,
or wheel smoke work with another language workspace's validation.
