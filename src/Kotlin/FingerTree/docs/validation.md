# Kotlin FingerTree Validation

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers validating the Kotlin FingerTree workspace
- Scope: Build command, tool bootstrap, and deterministic test coverage

Run from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace FingerTree
```

The command compiles `FingerTree/src` and `FingerTree/test` with the Kotlin command-line compiler and
runs the test executable. If no Java 21+ runtime is available on `PATH` on Windows, the script downloads a local
Temurin JDK 21 under `src/Kotlin/build/tools`; on non-Windows hosts, provide Java 21+ through `PATH` or
`JAVA_HOME`. It also downloads and verifies the Kotlin 2.4.0 compiler archive before compilation. All generated
files stay under the ignored `build` directory.
The build wrapper launches one compiler process and then one test JVM, pins the Kotlin backend to one
thread with `-Xbackend-threads=1`, and uses no Gradle daemon or worker pool. Concurrent-reader cases
create their bounded threads inside that single test JVM.
On Windows the script enables inherited non-interactive OS error handling before tool startup, and it launches
the test JVM in AWT headless mode so failures stay on the console and return a nonzero exit.

The test executable covers persistent deque snapshots, reversible orientation, measured prefix
splits/locates, sorted bag/set/map ordering and ranges, stable cached-priority dequeue, max-high closed
interval queries and coalescing, complete positional/range editing surfaces for positional and measured ropes,
comparator-aware sorted-map bulk construction, measured text line navigation, and rope builder conveniences.
Positional cursor coverage locks empty/start/end gaps, nullable-safe peeks, exact clean snapshots,
identity-preserving seek and empty insertion, one-shot range capture, unconditional representative
replacement, retained branches, concurrent readers, and a 750-command gap/list model.
Counting-comparator guards over 65,536-element sorted collections prove that bag counting bounds, set
rank/neighbor navigation, and keyed map lookup finish within one logarithmic descent.
Representation coverage validates AVL balance and identity sharing across
every facade, a 5,000-command sequence model, 100,000-element construction, policy compatibility,
overflow and comparison regressions, and concurrent readers over retained snapshots.
The rope overflow regression constructs a logarithmic shared DAG through self-concatenation, reaches
the maximum representable `Int` size without materialization, and verifies every positional growth
path and both cursor overflow paths fail before publication while all retained inputs remain usable.

RRB validation covers every 32-way boundary through 100,000 elements, unequal-height and uneven
fragment concatenation, exact-boundary leaf identity, regular-versus-relaxed size-table invariants,
a 10,000-operation retained-snapshot model, and 2,000 adversarial split/concat rounds with explicit
density and height bounds. Builder tests cover full-tail transfer, partial-tail copying, cached
snapshots, adopted prefixes, fail-fast iteration, clearing, and source-array isolation. Nullable
elements, checked count overflow, invalid overflowing ranges, no-op identity, and concurrent readers
have dedicated cases.

DABA Lite validation exhausts all 1,024 insert/evict histories of length ten against a
noncommutative matrix monoid, runs a 100,000-operation randomized FIFO model, and crosses the
63/64/65 and 127/128/129 chunk boundaries under 512 sliding-window cycles each. A non-default
identity drives all four fixup phases and observes the exact reachable three/two/one combine maxima.
Every reachable insert and eviction `combine`/`empty` callback ordinal is forced to throw and checked
for unchanged statistics, aggregate, and continued usability; the chunk-allocation boundary and
`clear()` identity failure have dedicated rollback cases. Storage tests check prompt retired-slot
and predecessor-chunk collectability while keeping the aggregator alive. Validation also locks down
callback-free region/capacity statistics, O(1) clear to one block, reuse, nullable identities, and
direct reuse of a `MeasurePolicy` as a monoid.

Canonical zip-zip validation checks bulk and purely incremental permutation convergence,
delete/reinsert convergence, a 15,000-operation retained-snapshot model, bulk first-representative
retention, nullable stored-representative versus miss lookup, explicit-comparator hash coherence and
rejection, and all receiver-comparer set relations including asymmetric cross-policy equality. Exact
keyed-HMAC and `ZZT2` public-seed vectors lock down byte order and derivation; same-seed policies
reproduce shape/digest/statistics, caller-key copies survive source mutation, and independently
generated hidden keys produce distinct retained ranks. The latter is the suite's sole intentionally
probabilistic assertion: collision of the checked 128-bit secondary/content pair has probability at
most 2^-128.

A deterministic opposite-sign secondary-rank pair proves heap ordering uses unsigned 64-bit order.
Policy-gated algebra, identity-preserving no-ops, and node-identity counts prove both add and remove
retain at least 90% of a 2,000-node set off their edited paths. A 4,096-node fully colliding priority
chain exercises stack-safe bulk build, validation, removal, reinsertion, digesting, and equality.
Counting-comparator coverage proves digest inequality returns before semantic comparison; concurrent
readers exercise lazy digest publication. A local reflection-injected cached-count fault verifies
that structural validation rejects corrupted metadata.

Brodal-Okasaki validation drains ascending, descending, equal-priority, and melded 4,096-element
shapes while decoding every fused forest boundary. A 20,000-operation branching multiset history
revisits retained roots; representative ties, comparator-identity rejection, nullable minimum, and
exact empty-meld/no-op sharing have dedicated cases. Comparison counters through 65,536 elements
enforce zero comparisons for minimum/iteration, one through five for insert/meld, and the audited
`32 * log2(n) + 8` delete-min ceiling. A large deletion must share all but logarithmically many tree
objects, and concurrent readers traverse retained snapshots.

Priority-search validation drives every AVL rotation direction, deletion rebalancing, and a
50,000-key ascending construction. A 20,000-operation keyed model retains and later rechecks every
snapshot. Tests distinguish comparator equality from object equality for replacement no-ops,
preserve first key representatives, enforce key-order tie deletion, and quantify off-path sharing.
Counting comparators prove an impossible threshold prunes at the root and an exact-key range visits
at most one AVL path with the expected comparison equation; mixed range/threshold queries remain
in key order. Custom nullable key/priority comparators prove entry lookup, minimum views, removal
results, and exact no-ops remain unambiguous when all three stored fields are null. Concurrent
readers exercise lookup, iteration, winners, and pruned queries.

This workspace has no Kotlin benchmark harness. The executable suite deliberately includes the
4,096-node degeneracy case, 12,000-node concurrent digest case, and 15,000-command model as workload
and complexity guardrails, but it does not report timings. Comparative canonical-set benchmarks live
in the C# BenchmarkDotNet workspace; a Kotlin timing harness should be added only with a reproducible
JMH setup rather than embedding stopwatch assertions in correctness tests.
Likewise, the JVM Brodal audit does not copy C# allocated-byte ceilings: HotSpot object layout,
escape analysis, tiered compilation, and collector choice make them non-portable. Exact comparison
counts and identity-sharing bounds are the deterministic JVM substitutes.
