# C FingerTree Tests

- Created (UTC): 2026-07-02T21:10:40Z
- Repository HEAD: 0e776179bac890ed5b792de5134c10bbc0e9d808
- Audience: Maintainers validating the C FingerTree port
- Scope: Native test executable and source organization under `src/C/FingerTree/tests`

The C FingerTree workspace has six focused native test executables. `fingertree_c_tests` is
registered as `fingertree_c.core`; `rrb_vector_c_tests` is registered as
`fingertree_c.rrb_vector`; `daba_lite_c_tests` is registered as `fingertree_c.daba_lite`;
`canonical_sorted_set_c_tests` is registered as `fingertree_c.canonical_sorted_set`;
`brodal_okasaki_heap_c_tests` is registered as `fingertree_c.brodal_okasaki_heap`;
and `priority_search_queue_c_tests` is registered as `fingertree_c.priority_search_queue`. Each source
contains its runner, assertion macros, policy helpers, and test cases. The canonical executable uses
the library's Windows CNG or OpenSSL Crypto backend but no test-framework dependency.

The runner prints one `[pass]` line per named test case, writes failed requirements to standard error with file and
line information, and exits non-zero if any test increments the failure count. A successful core run ends with
`all C FingerTree tests passed`. Focused runners identify their surface in the corresponding final marker:
`all C RRB vector tests passed`, `all C DABA Lite tests passed`, `all C canonical sorted-set tests passed`,
`all C Brodal-Okasaki heap tests passed`, or `all C priority search queue tests passed`.

## Test Cases

`fingertree_c_tests.c` registers these cases:

- `concurrent snapshot refcounts` copies, reads, updates, and disposes shared immutable snapshots from multiple
  threads.
- `reversible deque` checks logical reversal, endpoint edits, mixed-orientation concat, split/rejoin,
  set/insert/remove, persistence, and traversal.
- `tree endpoint/index/split/concat` covers the generic tree/deque surface, including indexed replacement.
- `lazy middle force paths` exercises memoized middle publication through reads, pops, split, concat, traversal,
  and disposal.
- `measure locate and split` covers size and custom measure-guided navigation.
- `structural split and locate costs` checks deep-tree split/locate boundaries and enforces logarithmic
  value-copy/measure-combine ceilings over 4,096 elements.
- `sorted set and multiset` covers uniqueness, duplicates, rank access, removal, and traversal.
- `sorted facade structural bounds` covers 4,096-element signpost-guided bounds, comparison ceilings, and
  zero-copy read-only searches under a counting value policy.
- `sorted map` covers insert, set, duplicate rejection, lookup, rank access, traversal, and persistence.
- `rope` covers chunked positional construction, indexing, traversal, split/reconcat, chunk-local editing,
  boundary coalescing, bounded chunk counts, append, and persistence.
- `rope cursor`, `rope cursor model`, and `rope cursor concurrent readers` cover explicit handle ownership,
  empty/start/end and chunk-seam gaps, copied peeks, navigation, exact-alias persistent edits, retained branches,
  unconditional replacement, array/rope insertion, failure-output preservation, a deterministic 750-command
  model, and concurrent copies/reads/branches over one retained cursor.
- `rope chunk boundaries` covers empty/singleton transitions, exact-maximum chunks, overflow splitting, removal
  re-coalescing, and exact-boundary concat for both positional and measured ropes.
- `measured rope` covers cached and prefix measures, cumulative-measure locate/split, chunk-local editing,
  coalescing, bounded chunk counts, append, and persistence.
- `measured rope cursor`, `measured rope cursor ordered measure`, `measured rope cursor model`, and
  `measured rope cursor concurrent readers` cover explicit handle ownership, chunk seams, ordered partitions,
  absolute search hits/misses/empty ropes, retained branches, failure-output preservation, a noncommutative
  measure, a deterministic 750-command model, and racing distinct-handle reads/branches.
- `priority queue` covers minimum-first drain order and FIFO stability through a 128-element equal-priority run.
- `interval tree` covers the signed 64-bit closed-interval facade.
- `generic interval tree` covers caller-supplied endpoint policies, invalid interval rejection, max-high descent,
  shared-annotation lifetime, and comparison-count complexity ceilings.
- `text rope` covers measured construction/editing, indexing, line count, both directions of line/column navigation,
  invalid-column rejection, bounded chunk counts, and traversal.
- `text rope cursor` covers the nominal text facade, newline partitions/search, line/column interoperation,
  C-string edits, retained snapshots, miss-to-end behavior, and persistence.
- `text rope long edit script` covers retained snapshots and repeated edits across a multi-line document, comparing
  indexing, traversal, line counts, and line/column navigation against a plain C string model.

`rrb_vector_tests.c` covers:

- radix boundaries and unequal-height concatenation through 100,000 values;
- exact leaf sharing, root no-op identity, and relaxed-layout diagnostics;
- a 10,000-operation list model with aliasing updates and retained snapshots;
- 2,000 adversarial split/concat rounds with density and height bounds;
- append-builder cached snapshots and adopted immutable prefixes;
- value copy/destroy lifetime balance and policy-pointer compatibility;
- deterministic failpoint allocation rollback for construction, updates, and builder staging; and
- concurrent vector copy/read/validate/dispose over atomic node references.

`canonical_sorted_set_tests.c` registers these cases:

- `canonical crypto vectors and unsigned priority` verifies exact keyed and seeded `ZZT2` SHA-256/HMAC vectors,
  unsigned secondary comparison, random-policy separation, copied keyed input, and public-seed diagnostics.
- `canonical topology and representatives` compares bulk and incremental shape, first-representative retention,
  nullable payloads, delete/reinsert convergence, borrowed lookup, identity diagnostics, content hashes, and
  validator statistics.
- `canonical deep collisions and lifecycle` forces one priority across 4,096 ordered values and exercises
  explicit-stack construction, lookup, removal/reinsertion, hashing, validation, and disposal.
- `canonical randomized histories and snapshots` checks 10,000 persistent updates against a sorted reference
  model while retaining and revisiting old versions.
- `canonical algebra relations aliasing and sharing` covers union/intersection/difference, exact policy identity,
  all proper and nonproper set relations, same-size/different-type-tag rejection, matching-tag asymmetric
  receiver comparators, exact result aliasing, canonical same-seed shape, and exact shared-node counts.
- `canonical allocation and callback atomicity` sweeps allocator and callback failures across bulk construction,
  point updates, algebra, normalization, hashing, and validation while checking output immutability and ownership
  balance.
- `canonical concurrent digest copy and readers` stresses distinct-handle copy/read/hash/validate/dispose across
  eight readers and verifies atomic lazy-digest publication.

`brodal_okasaki_heap_tests.c` registers these cases:

- `Brodal bounds representatives and sharing` validates ascending, descending, and fully equivalent 4,096-value
  heaps; exact insert/meld/delete comparison ceilings; empty-side root sharing; incompatible policies; self-meld
  logical multiplicity; exact removed representatives; and fused-tree statistics.
- `Brodal randomized retained history` runs 10,000 branching insert, meld, and delete-minimum operations against
  retained multiset snapshots, checking count, minimum, and structure throughout.
- `Brodal failure atomicity and lifetimes` exhausts every observed allocator/comparator point for point and bulk
  operations, every bulk-copy position, alias rollback, try-delete copy rollback, validation/visit failures, and
  final reference/callback accounting.
- `Brodal concurrent readers` runs eight independent copy/minimum/validate/dispose readers over a shared
  10,000-value immutable heap.

`priority_search_queue_tests.c` registers these cases:

- `PSQ representatives no-ops and minimum` covers first key-representative retention, comparer-versus-exact
  priority equality, exact value no-ops, nullable representations, try-add/remove, owned-entry lifetime,
  descending key policies, deterministic priority/key winner order, and policy/type mismatch.
- `PSQ rotations deletions stack safety and sharing` exercises all AVL rotations, deletion repairs, exact
  root/node sharing, a 4,095-key adversarial insertion order, and 50,000 ascending inserts without recursive
  stack growth.
- `PSQ range threshold pruning equations` checks inclusive mixed ranges in key order and exact key/priority
  comparison equations for impossible thresholds and exact-key searches through cached-winner pruning.
- `PSQ twenty-thousand retained model` runs a 20,000-operation randomized keyed model while retaining and
  revisiting 96 immutable snapshots.
- `PSQ failure atomicity and callback sweeps` exhausts every observed allocation, key/priority/value copy,
  priority/value equality, key/priority comparison, visit, validation, sharing, point-update, deletion, and
  array-construction failpoint, including alias rollback, untouched outputs, and exact lifetime accounting.
- `PSQ concurrent distinct-handle readers` runs eight independent copy/lookup/minimum/validate/visit/dispose
  readers over a shared 10,000-entry immutable queue.

`daba_lite_tests.c` covers:

- all 1,024 ten-step insert/evict histories against a noncommutative matrix FIFO model;
- a 100,000-operation variable sum window plus periodic naive reaggregation and structural checks;
- 63/64/65 and 127/128/129 block boundaries followed by sustained slide churn and complete drains;
- all singleton, flip-and-shrink, shift, and shrink phases with exact maximum 3/2/1 combine counts;
- callback-free structural validation and the fixed seven-temporary worst-path staging bound;
- all four create, two boundary-growth, and two clear allocator failpoints with exact rollback and
  allocation-liveness assertions;
- populated handle ownership transfer, inert moved-from queries/destruction, and continued destination use;
- prompt owned-value and retired-block reclamation, O(n + c) clear, and reuse after clear; and
- maximum-alignment callback storage plus empty/nonempty clear callback counts.

## Build And Run

From `src/C/FingerTree`, build and run the core CTest target:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug --parallel 1 && ""$cmakeDir\ctest.exe"" --preset msvc-debug --parallel 1 --output-on-failure -R ""^fingertree_c\.core$"""
```

Run the built executables directly when changing runner diagnostics or a focused test case:

```powershell
.\out\build\msvc-debug\tests\fingertree_c_tests.exe
.\out\build\msvc-debug\tests\canonical_sorted_set_c_tests.exe
.\out\build\msvc-debug\tests\brodal_okasaki_heap_c_tests.exe
.\out\build\msvc-debug\tests\priority_search_queue_c_tests.exe
.\out\build\msvc-debug\tests\rrb_vector_c_tests.exe
.\out\build\msvc-debug\tests\daba_lite_c_tests.exe
```

Use the workspace [validation guide](../docs/validation.md) for Release validation, warning policy,
generated-output locations, sample smoke tests, and benchmark entry points.
