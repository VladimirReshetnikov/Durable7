# Kotlin HAMT Validation

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers validating the Kotlin HAMT workspace
- Scope: Build command, tool bootstrap, and deterministic test coverage

Run from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace Hamt
```

The command compiles `Hamt/src` and `Hamt/test` with the Kotlin command-line compiler and runs the
test executable. If no Java 21+ runtime is available on `PATH` on Windows, the script downloads a local Temurin
JDK 21 under `src/Kotlin/build/tools`; on non-Windows hosts, provide Java 21+ through `PATH` or `JAVA_HOME`.
It also downloads and verifies the Kotlin 2.4.0 compiler archive before compilation. All generated files stay
under the ignored `build` directory.
On Windows the script enables inherited non-interactive OS error handling before tool startup, and it launches
the test JVM in AWT headless mode so failures stay on the console and return a nonzero exit.

The test executable covers map persistence, no-op root sharing, duplicate-key rejection, equal-hash
collision buckets, trie-order iteration, last-wins replacement with original-key retention, and set
algebra, including relations between sets built with different policies where the receiver's policy
is authoritative. Same-policy tests additionally pin self-operation instance identity, zero
rehashing, reference-pruned shared ancestry, all four structural algebra truth tables, and randomized
set models. A focused partially shared CHAMP lineage proves that `mapEquals` and typed `diff` skip
every reference-identical descendant, perform no rehashing, and invoke key-policy and value equality
exactly once for the single changed leaf.
Persistent factory-operation tests count exactly one hash and one selected callback across hit,
miss, collision, and bitmap paths. They verify caller-key callback identity, retained stored key and
equal-value representatives, present-null hits and updates, exact source-map identity for logical
no-ops, and failure atomicity when hashing, key equivalence, a selected factory, or value equality
throws.

Hash-bag tests cover first-representative construction, explicit distinct and 64-bit total counts,
expanded/distinct/entry order agreement, null elements, zero and negative pre-hash handling,
saturating removal, checked `Int` multiplicity overflow, and CHAMP/count invariants. Algebra tables
pin maximum union, minimum intersection, saturating difference, and checked additive sum. Separate
policy-mismatch cases verify eager receiver-policy normalization, checked class collapse, receiver
representative precedence, first-argument-order representatives for newly introduced classes,
identity-preserving no-ops, and operand retention after failure. A 1,000-operation deterministic,
constant-hash history compares point edits, totals, every class count, expanded iteration, and
internal invariants with a mutable model. Reflection guards lock the absence of ambiguous
`size`/`count` members and public bag builder/transient types.
Bimap tests cover key-first strict conflicts, independent hash/equality policies, stored
representatives, non-displacing replacement, symmetric lookup/removal, nullable values, policy-
preserving clear, cached reciprocal inverse identity, a 2,000-operation two-map model, retained
snapshots, policy-failure atomicity, structural validation, and concurrent readers.
The one-way CHAMP-session tier checks O(1)-shape adoption/publication through exact object identity,
policy identity, first stored representatives, null and collision entries, all point verbs, clear,
active reads and receiver-policy set relations, exact trie-order enumeration, acquisition-time view
capture, no-op preservation, successful-edit invalidation, retained source isolation, and
`IllegalStateException` across every consumed access category. Injected hash
and equivalence failures prove that the session remains unchanged, active, and retryable. Separate
deterministic map and set histories cross sixteen publication boundaries while comparing every
active and published version with mutable JVM models and rechecking every retained persistent
source. These are semantic tests only: the Kotlin session deliberately path-copies persistent
successors and has no benchmark or performance-win gate.
It also covers the mutable Ctrie's node-local GCAS and root/main RDCSS helping, deterministic
snapshot-versus-writer schedules in both linearization directions, deep and equal-hash tomb contraction, lazy renewal after
snapshot, same-reference no-op updates without equality callbacks, contended same-key updates,
equal-hash collision-node re-splitting when a later key has a distinct full hash, exact-policy and
stored-representative preservation across snapshot-to-CHAMP conversion (including null keys,
present-null values, mixed singleton/child canonical order, a frozen singleton tomb, collision
order, and later-write isolation), and a 250-round short-history
linearizability oracle under ordinary, shared-prefix, and all-equal-hash policies.

Merkle search-tree coverage includes:

- strict integer, nullable UTF-8, nullable bytes, UUID, and digest vectors, including malformed and
  noncanonical rejection;
- the exact C#/Rust policy-domain digest, empty digest, root digest, and full `MST2` golden block;
- canonical construction, insertion/removal contraction, independent-history convergence, no-op
  identity, and reference sharing across retained versions;
- inclusive ranges, typed nullable-value diffs, comparator-equivalent key retention, custom value
  relations, caller key/value reference preservation, and mutated-representative rejection by the
  deep canonical validator;
- a 12,000-operation retained-version model against `TreeMap`, concurrent snapshot readers, exact
  five-level adversarial geometry, and eight independent churn histories converging to identical
  roots, topology, statistics, and block bytes.

The persistence tier additionally validates:

- complete save/load/import round trips, store-completed partial packs, deterministic exports, and
  exact re-exported closure bytes;
- missing, tampered, malformed, noncanonical, foreign-domain, unsupported-algorithm, authenticated
  subtree-count, and crossed-child-reference rejection;
- each of the seven finite verification limits independently, including proof-query rejection
  before any codec invocation or block accounting;
- preflight conflict failures with zero destination writes, iterative missing-frontier repair, and
  closure-pruned synchronization packs;
- byte-exact `MSP2` membership, nonmembership, and inclusive-range descriptors, plus altered query,
  value, block, expansion, omission, duplicate, and unreachable-step failures;
- disjoint, identical, conflicting, resolved, deleted, and present-null three-way merges with no
  partial result exposed while any conflict remains; and
- eight-thread idempotent store writes, conflicting-address rejection, sorted digest snapshots,
  and defensive block/proof byte ownership.

For the stricter compiler gate used during persistence work, compile the same `Hamt/src` and
`Hamt/test` source set with Kotlin 2.4.0 and `-Werror`; the workspace is warning-clean.

The current serialized Kotlin 2.4.0/JVM 21 gate compiles with one backend thread, runs with one
active processor and the serial GC, and passes all 69 registered groups. Benchmarks are excluded
and remain postponed until an isolated run.
