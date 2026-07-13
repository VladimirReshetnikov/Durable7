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
set models.
It also covers the mutable Ctrie's node-local GCAS and root/main RDCSS helping, deterministic
snapshot-versus-writer schedules in both linearization directions, deep and equal-hash tomb contraction, lazy renewal after
snapshot, same-reference no-op updates without equality callbacks, contended same-key updates,
explicit conversion back to persistent CHAMP, and a 250-round short-history linearizability oracle
under ordinary, shared-prefix, and all-equal-hash policies.

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
