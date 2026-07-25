# C FingerTree

- Status: Active C workspace
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers implementing and reviewing the C port
- Scope: Build entry points, layout, validation, and current port boundaries for `src/C/FingerTree`

This workspace contains the C port of the native FingerTree work. It starts from the completed
[`src/Cpp/FingerTree`](../../Cpp/FingerTree/README.md) port and exposes a C11 API centered on a generic measured
finger-tree core.

The C workspace is intentionally dependency-light: the library is ordinary C, builds as a static library, and uses
focused native test executables registered with CTest. The canonical zip-zip set uses the platform cryptography
backend (Windows CNG or OpenSSL Crypto); the remaining surfaces have no external runtime dependency. The measured
core preserves immutable structural sharing through atomic reference-counted tree reps, shared lazy middle cells,
lazy deep-measure publication, digits, 2/3 nodes,
concatenation, split, locate, indexed replacement, and endpoint operations. The related C-facing surfaces currently
included are:

- `ft_persistent_deque`, an alias over the size-measured tree;
- `ft_reversible_deque`, an orientation-aware persistent deque with O(1) `reverse`, concat, split, and indexed
  edits over shared snapshots;
- `ft_sorted_set`, `ft_sorted_multiset`, and `ft_sorted_map`, persistent sorted wrappers over the deque/tree
  surface;
- `ft_canonical_sorted_set`, a type-erased persistent canonical zip-zip sorted set with cryptographically keyed
  deterministic ranks, reproducible seeded topology, fallible callbacks, policy-gated algebra, semantic
  cross-policy relations, concurrent lazy content digests, and structural-sharing diagnostics;
- `ft_brodal_heap`, a type-erased persistent Brodal-Okasaki bootstrapped skew-binomial min-heap with
  worst-case O(1) insert/meld/minimum, worst-case O(log n) delete-minimum, fallible policy callbacks,
  comparer-identity-gated melding, and a full fused-representation validator;
- `ft_priority_search_queue`, a type-erased persistent winner-cached AVL with one entry per ordered key,
  first-key/last-value replacement semantics, O(1) global minimum, O(log n) keyed updates and delete-minimum,
  and inclusive key-range/priority-threshold pruning;
- `ft_range_update_sequence`, an independent type-erased persistent implicit AVL with ordered cached measures,
  lazily composed update tags, O(1) whole-root updates, O(log n) proper range updates/queries, checked counts,
  failure-atomic callbacks/allocation, and structural-sharing diagnostics;
- `ft_priority_queue`, a generic persistent minimum-priority queue with FIFO tie-breaking for equal priorities;
- `ft_interval_tree`, a generic closed-interval tree facade over caller-supplied endpoint policies;
- `ft_interval_tree_i64`, a convenience closed-interval facade for signed 64-bit endpoints;
- `ft_persistent_interval_map`, a unique closed-interval-to-payload map composing the augmented
  interval tree with a complete-key sorted map for pruned overlap queries and exact lookup;
- `ft_persistent_chunked_bit_set`, a sparse measured sequence of nonzero 64-bit words with
  logarithmic membership, inclusive rank/select, ascending visitation, and linear sparse algebra;
- `ft_rrb_vector`, a type-erased 32-way relaxed radix-balanced vector with atomic structural
  sharing, checked prefix sizes, persistent range edits, and an append-only builder;
- `ft_daba_lite`, a mutable six-cursor FIFO monoid aggregator with 64-slot blocks, worst-case
  3/2/1 combine ceilings for insert/evict/query, injected allocation, and deterministic reclamation;
- `ft_rope`, a generic persistent chunked positional sequence backed by measured chunk leaves, plus
  `ft_rope_cursor`, an explicit-lifetime root-sharing gap cursor with persistent edits and retained branches;
- `ft_measured_rope`, a generic persistent chunked sequence with cached user measures and cumulative-measure
  navigation, plus `ft_measured_rope_cursor`, an explicit-lifetime gap cursor with ordered before/after measures
  and absolute monotone-prefix search;
- `ft_text_rope`, a character-rope facade backed by `ft_measured_rope` with a cached newline measure, insertion,
  removal, indexing, O(1) line count, O(log n) line navigation, validated line/column-to-offset conversion, and
  a nominal `ft_text_rope_cursor` facade.

The central C++ lazy-middle publication machinery is now present in the C core: endpoint overflow and boundary
pop repairs share memoized middle cells across persistent versions, and independently held immutable handles may
be used concurrently under normal handle-lifetime rules.

## Build

From this directory:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug --parallel 1 && ""$cmakeDir\ctest.exe"" --preset msvc-debug --parallel 1 --output-on-failure"
```

Use `msvc-release` for the optimized configuration. The `msvc-*` presets use Visual Studio's bundled Ninja by
absolute path, so CMake and Ninja do not need to be on `PATH` for that route. Keep the Visual Studio environment
setup, configure, build, and CTest run in one `cmd.exe` chain when starting from plain PowerShell; invoking
`VsDevCmd.bat` directly from PowerShell does not persist its environment changes in that process. Host-agnostic
`ninja-debug`, `ninja-release`, and `ninja-asan` presets are also available when CMake, Ninja, and a suitable
compiler are on `PATH`. CMake links `bcrypt` on Windows and resolves the maintained OpenSSL Crypto package on
other hosts for canonical-rank SHA-256, HMAC-SHA-256, and secure random bytes. For release commands, sanitizer
validation, benchmark entry points, warning policy, and generated-output locations, see the
[validation guide](docs/validation.md).

## Layout

- `include/durable7/finger_tree/fingertree.h` contains the public C API.
- `include/durable7/finger_tree/canonical_sorted_set.h` contains the independent canonical zip-zip
  sorted-set and rank-policy API.
- `include/durable7/finger_tree/brodal_okasaki_heap.h` contains the independent persistent
  Brodal-Okasaki heap and policy API.
- `include/durable7/finger_tree/priority_search_queue.h` contains the independent winner-cached
  priority-search queue, owned-entry, and policy API.
- `include/durable7/finger_tree/range_update_sequence.h` contains the independent persistent
  range-update sequence, algebra-policy, ownership, visitor, and diagnostic API.
- `include/durable7/finger_tree/persistent_interval_map.h` contains the derived
  payload-bearing interval-map API.
- `include/durable7/finger_tree/rrb_vector.h` contains the separate RRB vector API.
- `include/durable7/finger_tree/daba_lite.h` contains the separate mutable DABA Lite API.
- `src/fingertree.c` contains the measured-tree implementation and its wrappers;
  `src/brodal_okasaki_heap.c` contains the fused bootstrapped skew-binomial heap;
  `src/canonical_sorted_set.c` contains the immutable zip-zip core and cryptographic policy implementation;
  `src/priority_search_queue.c` contains the persistent winner-cached AVL;
  `src/range_update_sequence.c` contains the lazy implicit-AVL range-update core;
  `src/persistent_interval_map.c` contains the dual-index interval map;
  `src/rrb_vector.c` contains the independent RRB core and builder; and `src/daba_lite.c` contains
  the independent sliding-window aggregator.
- `tests/` contains the [core and focused CTest executables](tests/README.md).
- `samples/` contains deterministic C sample executables that are also registered as CTest smoke tests; see
  [`samples/README.md`](samples/README.md).
- `benchmarks/` contains a dependency-light timing harness for quick local comparisons; see
  [`benchmarks/README.md`](benchmarks/README.md).
- `docs/` contains C-specific API, usage, and validation notes.
