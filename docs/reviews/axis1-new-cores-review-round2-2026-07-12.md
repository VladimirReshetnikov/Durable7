# Axis 1 New-Cores Review — Round 2 (Remediation Verification + Fresh Review) — 2026-07-12

- Created (UTC): 2026-07-12T20:19:07Z
- Repository HEAD (reviewed): 4ef23a612168f810bc1216dcc6ff9b8693f18d63 (on `origin/main` merged as `0ae95fa`)
- Audience: Maintainers and AI coding agents working on the repository-owned Axis 1 cores
- Scope: (1) verification that every finding in the [2026-07-12 Axis 1 review](axis1-new-cores-review-2026-07-12.md)
  was actually and correctly fixed by the remediation pass, and (2) a fresh independent adversarial
  review of the Axis 1 cores across all six languages, hunting for new defects and for regressions the
  remediation introduced; fixes applied this round

> **Current-state note (resolved 2026-07-12): no pending work remains in this report.** The original
> findings and recommendations below are retained as review provenance. Commits `48bb943` and
> `0a70600` close every Low/informational follow-up, add the missing deterministic and malformed-state
> gates, and incorporate two defects found while implementing the review: native RRB cap-level shift
> overflow and Kotlin CHAMP diagnostic shift masking. See the
> [resolution addendum](#resolution-addendum--2026-07-12) for item-by-item evidence.

## Summary

The remediation pass (commits `87b763f`, `a5e63c0`, `3e23cf4`, `bae48c0`, `dfa89d7`, `eaf8b8d`, merged
via `0ae95fa`) **resolved every finding** from the first Axis 1 review. Each fix was verified against the
actual source this round — not taken on the strength of its commit message — and the two Critical items
(the C canonical-set heap overflow and the Kotlin Ctrie snapshot isolation break) are correctly and
completely fixed, including the lock-free RDCSS port. Independent re-derivation confirmed the Merkle
golden vectors, and the Kotlin Ctrie suite (43 tests including four new gates) and the C#/Rust Patricia
suites were run green by the reviewers.

The fresh review found **no new Critical, High, or Medium correctness defect** in any core. It did find
one Medium **test-coverage** gap the remediation left behind — the new CHAMP canonicalization validators
in Kotlin (and C++ on libstdc++/libc++) run against test data that structurally cannot exercise the shapes
they police, so they pass vacuously — plus a handful of Low/informational parity nits. Two items were
fixed outright this round (the Kotlin CHAMP test coverage, and a Rust RRB failure-model parity divergence);
the rest are recorded below.

## Method

Six adversarial passes, one per core-area, each with a dual mandate: verify the specific prior fix is
correct and complete (constructing the original failure scenario and confirming it is now impossible), and
freshly re-review the changed code for new defects or regressions. Two areas spawned corroborating
sub-passes (PSQ, and the Merkle persistence tier), giving three independent verdicts on the Merkle wire and
persistence surfaces. The C# reference workspace was built clean (0 warnings) and its full suite run green
(Numerics 319, Hamt, Tungsten, FingerTree; `test.ps1` exit 0) as a baseline. Applied fixes were validated
by rebuilding and re-running the affected workspace.

## Part 1 — Verification of the 2026-07-12 findings

Every finding is FIXED. Verdicts are from source tracing this round; where a reviewer executed the suite it
is noted.

| Prior finding (severity) | Verdict | Verification evidence |
| --- | --- | --- |
| C canonical zip-set removal overflow (Critical) | **FIXED, no regression** | `ft_canonical_allocate_merge_path` sizes scratch `2*(height+1)`; true max seam is `2*height−2` (margin 4); the guard bounds-checks before each write with `result` still NULL (no leak); rebuild refcounts balance; zip topology is line-for-line with C# `Merge`; the new `test_interior_removal_merge_seam` drains an 8192-node treap (height ≥ 14) in scrambled order. |
| Kotlin Ctrie snapshot RDCSS (Critical) | **FIXED** | `87b763f` ports the root descriptor: `snapshot()` installs `RootDescriptor(before, main, after)` and `complete(RootDescriptor)` commits only if `before.node.main === expectedMain`, else aborts and retries — the exact prior interleaving (writer commits M1 into the old-gen node while the snapshot holds stale M0) now re-reads M1 and cannot lose the write. Every root consumer routes through the helping `readRoot()`. 43 Kotlin HAMT tests pass, including the deterministic `ctrieSnapshotDoesNotLoseCommittedWriter`. No helping cycle, correct memory model, ABA defeated. |
| Kotlin Ctrie concurrency evidence (High) | **FIXED** | Deterministic post-`readMain` race test + `ctrieReaderHelpsInstalledGcas` + a 250-round exponential linearizability oracle asserting snapshot contents equal a consistent model state. |
| Kotlin Ctrie tomb contraction (Medium) | **FIXED** | `TNode` + `contract`/`contractCollision`/`cleanTombs` + the root-no-tomb invariant, byte-for-byte with the C# reference; `ctrieRemovalContractsDeepTombs` collapses a deep `1<<30` chain to a single indirection node. |
| Merkle degenerate golden vector (High) | **FIXED** | `a5e63c0` pins a 14-entry / 4-block / 3-level tree, byte-for-byte identical across all six suites. Independently re-derived with `hashlib`: the domain digest, the empty-child sentinel, the 174-byte root block, and `root_hash = SHA256(root_block)` all match. The pinned block is genuinely non-degenerate: level byte 2, `subtreeCount` 14, a 2-entry block, and a non-empty child digest. Confirmed test-only (no production line touched), so it cannot have regressed proof/sync/merge. |
| C# core wire anchor (Low) | **FIXED** | `MerkleEncodingWireTests` now asserts an absolute `MST2` block-bytes vector against the core `Node.BlockBytes`. |
| Haskell SHA-256 boundary evidence (Low) | **FIXED** | 55/56/64/65-byte vectors (padding + multi-block boundaries) pinned; all match `hashlib`. |
| C# Patricia combining algebra (Medium) | **FIXED** | `PatriciaMapCore` now has a genuine prefix-aligned combining `Union`/`Intersect` with the same overlap dispatch as the non-combining core; public overloads route through it; no-op root identity restored (`CombinedLeaf` reuse + receiver-root short-circuit). 130/130 Hamt tests pass; the model test uses an asymmetric combiner (a flipped argument order would fail). |
| Rust Patricia shared-subtree pruning (Medium) | **FIXED, gate correct** | A `short_circuit_shared: bool` is threaded through every recursive call; built-in `union`/`intersect` pass `true` (return the `Arc`-identical subtree by reference), user `union_with`/`intersect_with` pass `false` (the combiner still runs per shared key). Verified in both directions with call-count assertions; 5/5 Rust patricia tests including a 2000-op `BTreeMap` oracle. |
| Haskell Patricia equal-value replacement (Low) | **FIXED** | Documented as the deliberate `Eq`-free rebuild path in Haddock and workspace guidance. |
| RRB density contract (Medium) | **FIXED** | Boundary-only redistribution is documented as a conscious design choice in the catalog and all six ports' local contracts; density ceilings are test/benchmark gates, not validator invariants. Confirmed no concat/split logic changed (byte-for-byte), element order/counts conserved, no invalid nodes. |
| RRB maximum height (Low) | **FIXED** (see new F-RRB) | Unified to `(count-storage-bitwidth − 1)/5` — correctly 6 for 32-bit-count ports, 12 for 64-bit; Haskell's real `6`-on-64-bit bug corrected to 12; Rust enforces and tests an over-height rejection. |
| Five-port CHAMP canonicalization evidence (Medium) | **FIXED (validators)** — but Kotlin/C++ coverage insufficient (see new F-CHAMP) | Recursive canonical validators added in C, C++, Haskell, Kotlin, Rust; all are non-vacuous by inspection (each rejects a single-payload branch while permitting the legitimate unary bridge and a ≥2-entry collision child); dfa89d7 is inspection-only (no `insert`/`remove`/`lookup`/collapse path changed). C, Haskell, and Rust drive them with spreading hashes and genuinely exercise bridges/collisions/collapse. |
| Haskell CHAMP validator (Low) | **FIXED** | New clause rejects an under-full branch (`< 2` entries) unless its sole child is itself a `Branch`; 0- and 1-element maps (root `Empty`/`Leaf`) are unaffected, so it does not over-reject. |
| CHAMP policy compatibility (Low) | **FIXED** | C++ and Haskell state the compatible-policy precondition for `map_equals`/`diff`; Rust documents intentional semantic equality across `BuildHasher` state (key equivalence is fixed by `Eq`). |
| Native Brodal meld shape (informational) | **DOCUMENTED** | Rank-bucket-with-carry recorded as equivalent to `uniquify`/`unionUnique` in the catalog and C/C++ API notes. |
| DABA Lite cleanups (informational) | **DOCUMENTED** | Rust overlay-read comment (verified behaviorally a no-op), single-unlink trim tied to one-block-per-evict, native O(n+c) clear all recorded. |

## Part 2 — Fresh review: new findings

### Fixed this round

#### CHAMP canonicalization test coverage — the new validator runs on data that cannot exercise it (Medium; Kotlin CONFIRMED, C++ PLAUSIBLE; Kotlin fixed this round)

The remediation added the canonicalization validators the first review asked for, but in Kotlin the test that
drives them cannot build the shapes they police. `champCanonicalizationAndDiff`
([HamtTests.kt](../../src/Kotlin/Hamt/test/tools/datastructures/hamt/HamtTests.kt)) populated the map with
dense integer keys `0..511` under identity hashing (`DefaultHashPolicy.hash = key.hashCode()`, no spread).
Such keys form a maximally dense two-level trie — the root branches on bits 0–4 into 32 children, each a
16-payload node on bits 5–8 — which structurally cannot contain a unary bridge, a collision node, a
leaf-as-bitmap-child, or an under-full node. The delete/reinsert churn removed only every third key, dropping
each 16-entry child to ~10–11 entries, never to a single entry, so the branch-collapse path was never
triggered either. Consequently `underfullBitmapNodes == 0`, `invalidLeafChildren == 0`, and the topology
comparison all passed **trivially**: a canonicalization regression (a `remove` that failed to collapse a
branch down to one entry) would ship green. C++'s equivalent test has the same property on libstdc++/libc++,
whose `std::hash<int>` is identity (adequate only on MSVC, whose `std::hash<int>` is FNV-1a). C, Haskell, and
Rust use spreading/salted hashes and genuinely exercise the guard; C# uses `i % 9 == 0 ? 17 : i * 0x01010101`.

**Fix (Kotlin, this round):** the test now uses a `SpreadingHashPolicy` (mirroring the C# reference —
`i % 9 == 0 ? 17 : i * 0x01010101`) so the 512-key independent-history, topology-equality, and churn
assertions run on a genuinely sparse trie with bridges and collision runs (`collisionPayloads > 0` is now
asserted), plus a **deterministic non-vacuity block**: a `TableHashPolicy` builds an exact deep two-entry
branch under a unary bridge (keys 0 and 1 sharing the low ten hash bits, diverging at shift 10; key 2
diverging at the root), removes one entry, and asserts the survivor is inlined and the bridge collapsed
(`underfullBitmapNodes == 0`, and the collapsed topology equals the direct build of `{0, 2}`). Were the
collapse skipped, `underfullBitmapNodes` would be 1 and the topology would diverge — so the assertion now
fails loudly on a regression.

**Recommended follow-up (C++):** apply the same spreading-hash treatment to the C++ canonicalization test so
the guard is exercised on the GCC/Clang lanes, not only MSVC.
**Resolution:** completed by `0a70600`; all three strict compiler lanes pass the non-vacuous fixture.

#### RRB Rust height enforcement diverged from the reference failure model (Low, fixed this round `4ef23a6`)

`bae48c0` made Rust the only RRB port that aborts the process on the normal persistent-construction path:
`branch()` enforced `height <= MAXIMUM_HEIGHT` with a hard `assert!` that runs on every concat/split/
set_node/build_level. The C# reference builds an over-height branch silently (the `has_regular_layout` guard
routes it onto the relaxed size-table path — no shift UB) and reports it only from the validator; the C port
returns a recoverable `FT_STATUS_OVERFLOW`. An over-height branch is unreachable with constructible inputs
(it needs a count that exhausts the `usize` domain), so this was never a live abort, but it is a
commit-introduced failure-model divergence. Fixed: the `branch()` check is now a `debug_assert!` (keeping the
debug-time invariant) so release builds match the reference; `validate_node` still hard-rejects. Also removed
a dead duplicate root-height check in `validate_structure` (`validate_node` already rejects and returns via
`?` first). Validated: `cargo test -p tools-data-structures-fingertree --lib rrb` 12/12 (including the
over-height rejection test), full crate test + clippy warning-clean.

### Historical findings for follow-up (resolved)

- **CHAMP port validators omit the hash-prefix routing check (Low).** The C# reference asserts every payload/
  child sits in the slot its hash prefix selects; none of the five port canonical validators verify routing
  or slot ordering (they check bitmap/count consistency, collision size ≥ 2, leaf-child prohibition, and the
  under-full/bridge rule). A misrouting bug would pass the port validators; partly compensated by the topology
  comparators.
- **CHAMP topology comparators compare collision nodes by (hash, size) only (Low).** C/C++/Rust/Haskell compare
  collision nodes by hash and length, not entry contents — weaker than C#'s key-ordered comparison. Cannot
  false-pass in the current suites (compared maps are built from identical data) but is a latent weakness.
- **RRB height cap is tight, not conservative (Low).** Each cap equals `minimum_height(domain_max)` exactly, so
  a legal `+1`-slack tree at the very top count band would be rejected by every port. Untriggerable (needs
  ~2³¹ / ~2⁶⁰ elements); worth a comment in the cap derivation.
- **Rust Brodal–Okasaki `with_comparer` wraps each comparer in a fresh `Arc` (Low).** Two heaps built from the
  *same* custom comparer object are rejected as incompatible for `meld`, whereas C# `Meld` uses
  `ReferenceEquals` and would accept them. Stricter-but-safe (never causes misordering; natural/default heaps
  still meld). Document the identity model or key compatibility on comparer contents.
- **C++ PSQ `enumerate_at_most` pushes both children unconditionally in the in-range branch (Low).** At an exact
  boundary key it descends one out-of-range subtree and filters it out, costing O(log n) extra visited nodes
  per boundary, where the other five gate the pushes on the bound. Results are identical and within the
  documented `O(log n + v)`. Gate the pushes on `upper < 0` / `lower > 0` for visited-set parity.
- **C Patricia `intersect_nodes` combine-leaf omits the right-leaf sharing reuse (informational).** When a user
  combiner returns the right operand's value, C allocates a fresh leaf where the other ports reuse the right
  leaf. Keys/values/order identical — a structural-sharing micro-asymmetry only.
- **Ctrie Kotlin (informational).** The deterministic race test pins the lost-update direction; the reverse
  (a writer linearizing after the snapshot's root advance must be absent from the frozen view) is covered by
  the linearizability oracle and a sequential test rather than a dedicated deterministic hook. Empty sentinels
  are freshly allocated rather than shared singletons (correctness-neutral — CAS is by identity on the threaded
  value). Snapshot/writer mutual-abort liveness is inherent lock-free progress, identical to C#.
- **Patricia unasserted branches (informational).** C# self-union-with-combine call count and Rust's built-in
  partial-shared-subtree reference-return are correct by construction but not directly asserted.

## Resolution addendum — 2026-07-12

Every follow-up above is resolved. The historical severity labels describe the reviewed state, not
the current tree.

| Finding | Resolution |
| --- | --- |
| C++ CHAMP canonicalization coverage | `0a70600` replaces library-dependent integer hashing with the explicit spreading policy used by the reference and adds an exact deep-bridge collapse fixture. MSVC, GCC, and Clang strict suites pass. |
| CHAMP hash-prefix routing | `0a70600` threads prefix/mask state through the C, C++, Haskell, Kotlin, and Rust diagnostics, rejects invalid terminal slots and over-depth paths, and covers valid shift-30 slots 0–3 in every practical port. Rust directly constructs a misrouted node; Kotlin additionally pins over-depth and bitmap/run-cardinality rejection. |
| CHAMP collision topology comparison | `0a70600` compares collision key sets without insertion-order dependence through each port's key policy. Kotlin's collision-blind topology string was also replaced by policy-aware comparison after the cross-port audit found the same latent weakness there. |
| RRB top-band height slack | `48bb943` changes the absolute cap to `floor((countBits - 1) / 5) + 1` (7/13), preserving the documented legal `minimumHeight + 1` seam slack. C and C++ now guard cap-level capacity shifts and force relaxed layout before any oversized native shift; Rust pins cap acceptance and cap+1 rejection. |
| Rust Brodal comparer identity | `48bb943` adds caller-shared `Arc<dyn OrderComparer<T>>` construction, so independently constructed heaps retaining one comparer object may meld while distinct custom policies remain incompatible. |
| C++ PSQ range pruning | `48bb943` gates child pushes at inclusive key boundaries and adds exact-key comparison ceilings proving only the search path is visited. |
| C Patricia right-result sharing | `48bb943` retains the right leaf when an intersection combiner returns a right-equal value, with direct root-sharing coverage. |
| Kotlin Ctrie reverse snapshot race | `0a70600` adds a deterministic post-root-advance schedule proving a later writer is absent from the frozen view and present in the live trie. |
| Patricia assertion gaps | `48bb943` pins exact C# self-union callback counts at both widths and Rust `Arc` identity for partially shared built-in union and intersection subtrees. |

The implementation was independently reviewed after editing. That review confirmed the prefix-mask
arithmetic through shift 30, compact-array/bitmap ordering, policy compatibility, collision-key
semantics, and C++ exception behavior. It also found the native RRB and Kotlin CHAMP edge cases
described above before the commits were accepted.

Post-resolution validation is green: C# 976 tests; every Kotlin and Haskell workspace; the complete
Rust workspace (96 FingerTree unit tests, Brodal/PSQ integration, 30 HAMT unit tests, 15 Merkle wire,
19 Merkle persistence, 8 Tungsten, and doctests); C HAMT/Patricia/Merkle plus all eight FingerTree
CTest targets; C++ HAMT/Merkle under MSVC, GCC, and Clang plus all 23 FingerTree CTest targets.
Rust formatting and warning-denied Clippy also pass. A parallel C# run first encountered an MSBuild
worker-process exit (`MSB4166`) under five-way compiler contention; the immediate isolated full rerun
passed, confirming an infrastructure failure rather than a product/test failure.

## Verified clean (re-derived this round)

The cores the first review found clean were independently re-derived, not assumed:

- **Priority search queue (all six)** — the stale-winner-after-rotation class is disproven again: every
  structural mutation funnels through the single winner-recomputing node constructor (two independent passes
  concurred). One Low C++ pruning-tightness nit (above); no correctness or C memory-safety defect.
- **Brodal–Okasaki heap (all six)** — O(1) worst-case bounds honest; the C/C++ rank-bucket-with-carry meld is
  equivalent to `uniquify`/`unionUnique`; native self-meld DAGs reclaim without double-free.
- **DABA Lite (all five)** — query is `Combine(front, aggregateB)` in strict FIFO order (verified against
  non-commutative monoids); callback atomicity via plan-then-commit or rollback; bounded reclaim; documented
  O(n+c) native clear.
- **Canonical zip-zip set (all six)** — order-independent shape/digest; native ownership balances; the C
  overflow fix verified correct with no regression.
- **Merkle persistence / proofs / sync / merge (all six)** — no constructible forgery (digest recompute +
  key-forced descent + exact expansion + parent→child digest binding to a fixed root), sync converges and
  terminates, three-way merge keeps present-vs-absent distinct, the verification budget is enforced before
  allocation on every path, and native decode is bounds/overflow-safe. Triangulated by three independent
  passes; a few non-issues (C sorting expanded indexes — C# sorts too; range-proof double-visit impossible;
  C++ budget coupling) were investigated and dismissed.

## Validation

- Base merged from `origin/main` (`2fb11c0..0ae95fa`), fast-forward, no conflicts.
- C#: `dotnet build` clean (0 warnings); `test.ps1` exit 0 (Numerics 319 + Hamt + Tungsten + FingerTree).
- Rust RRB fix (`4ef23a6`): `cargo test -p tools-data-structures-fingertree --lib rrb` 12/12; full crate test
  + clippy warning-clean.
- Kotlin CHAMP test-coverage fix: `build.ps1` compiled and ran all three Kotlin workspaces' suites,
  exit 0, every test passing — including the reworked `champCanonicalizationAndDiff` (registered in the
  runner), which now exercises a sparse trie with collision runs plus the deterministic deep-collapse
  non-vacuity block.
- Other findings are static-analysis findings; no code was changed for them, so no further build was required.

## Relationship to other documents

- [Axis 1 new-cores review (2026-07-12)](axis1-new-cores-review-2026-07-12.md) — the first-round findings this
  report verifies as fixed; its "Remediation status" table records the fix commits.
- [Frontier structure catalog](../reference/frontier-structure-catalog.md) — the per-language contracts for the
  cores; the RRB density/height and CHAMP entries reflect the remediation's documentation.
- [Cross-language implementation review (2026-07-11)](cross-language-implementation-review-2026-07-11.md) — the
  engine-core review that predates the Axis 1 cores.
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md) — the workflow used to
  resolve the former cross-language nits and validate the resulting parity changes.
