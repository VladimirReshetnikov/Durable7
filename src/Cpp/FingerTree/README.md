# C++ FingerTree

- Status: Active C++ workspace
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Updated (UTC): 2026-07-16T22:52:15Z
- Updated against repository HEAD: 88164edb086096800b2fb32eeaa7e7a1e556e183
- Audience: Maintainers implementing and reviewing the C++ port
- Scope: Build entry points, layout, and validation for `src/Cpp/FingerTree`

This workspace contains the C++ port of the `FingerTree` data-structure library. The port follows
[`docs/port-plan.md`](docs/port-plan.md): a header-first library under the namespace
`tools::data_structures::finger_tree`, CMake/Ninja build entry points for the local MSVC toolchain, and CTest
validation from the first milestone onward.

`persistent_interval_map<Endpoint, Value, Comparison, ValueEqual>` is the payload-bearing interval
sibling. It orders validated closed intervals by the complete `(low, high)` key and uses one
measured finger tree whose annotation retains both the complete rightmost interval and maximum high
endpoint. Exact lookup, strict or replacing edits, point stabbing, and output-sensitive overlap
enumeration therefore need no second index; distinct overlapping intervals remain distinct.

`persistent_chunked_bit_set` stores ascending nonzero 64-bit words in the measured tree. Cached
word/population/last-word annotations give logarithmic membership, inclusive rank, and select in
the nonnegative signed-32-bit domain; algebra merges represented word streams without scanning
clear space.

Alongside the measured-tree family, the workspace now ships `rrb_vector<T>`: a persistent 32-way relaxed
radix-balanced vector with dense regular branches, cumulative-size relaxed branches, boundary-spine split and
concatenation, and an append builder whose immutable snapshots remain isolated from later staging. The public
aggregate header includes the vector and its builder; representation diagnostics and adversarial model tests keep
the regular/relaxed distinction, density, height, and sharing contracts observable to maintainers.

`range_update_sequence<T, Algebra>` is the independently implemented persistent implicit-AVL sibling for
algebraic range transformation. Its static policy supplies an ordered measure monoid, a directional tag monoid,
and consistent actions on elements and cached subtree measures. Optional pending tags are pushed immutably only
for structural edits; indexing, forward iteration, and range measurement instead carry inherited tags without
publishing replacement nodes. A full nonidentity update replaces one root in O(1), and arbitrary updates,
queries, splits, concatenations, and indexed edits remain O(log n). Checked `size_t` growth, policy-exception
atomicity, physical-sharing diagnostics, retained-root value iterators, and a compact exact-maximum shared-DAG fixture
are part of the native contract. No benchmark claim accompanies this shipment.

The same public package also ships `daba_lite<T, MonoidPolicy>`, the deliberately mutable six-cursor DABA Lite
sliding-window aggregator. Insert, eviction, and query have exact three/two/one `combine` ceilings; linked
64-slot blocks keep every slide worst-case O(1), retire crossed blocks promptly, and expose callback-free
representation statistics. Throwing monoid callbacks leave the published window unchanged. C++ deterministic
destruction is called out explicitly: `clear()` releases all owned values and blocks before returning and is
therefore O(n + c), unlike the tracing-GC C# reference's constant root swap.

`canonical_sorted_set<T>` is the policy-canonical sorted sibling. Its retained `zip_tree_rank_policy<T>` derives
the exact C# ZZT2 geometric/unsigned-secondary/content rank tuple through HMAC-SHA-256, with independent random,
public-seed, and caller-keyed modes. Explicit-stack Cartesian build and zip/unzip updates remain safe on a fully
colliding linear tree; immutable nodes share both paths and stored representatives, lazy atomic digests accelerate
same-policy inequality, and semantic equality deliberately follows the receiver's comparer across policy families.

`brodal_okasaki_heap<T, Less>` is the immutable bootstrapped skew-binomial priority core. Insert, minimum, and
meld are worst-case O(1), delete-minimum is worst-case O(log n), and the audited insertion/meld path performs no
more than five comparator calls. Heaps derived from one retained comparator policy can meld while preserving all
comparer-tied representatives and exact shared subtrees. Move-only values are owned behind immutable handles;
the result-returning delete-minimum surface preserves the exact removed handle with its persistent remainder.
Allocation-free iterative reclamation keeps the C#-faithful decreasing-root and equal-root shapes stack-safe in
deterministic C++ destruction.

`priority_search_queue<Key, Priority, Value, KeyLess, PriorityLess>` is the immutable keyed priority core. Its
direct AVL nodes cache both size/height and the priority-then-key subtree winner, giving O(log n) keyed updates,
O(1) minimum lookup, and O(log n) minimum deletion without a second index. Comparator-equivalent updates retain
the first concrete key representative while replacing priority and payload; exact no-ops additionally require
ordinary priority and payload equality. Shared component handles support move-only keys, priorities, and payloads,
and the inclusive range/priority query prunes whole subtrees from cached winners while returning key-ordered entry
handles.

The positional `rope<T>` exposes a persistent `rope_cursor<T>` checkpoint for gap-based editing. The C++ cursor
is deliberately a cheap root-sharing rope snapshot plus a position: construction, movement, seek, and snapshot
are O(1), while peeks and point edits retain the rope's O(log n) descent plus bounded chunk work. This is not the
C# zipper implementation and makes no O(1)-amortized local-edit claim.

`measured_rope_cursor<T, MeasurePolicy>` applies the same immutable snapshot-plus-gap model to the exact measured
rope root, adds ordered before/after measures and absolute monotone prefix search, and carries a usable end cursor
on a miss. `text_rope_cursor` is the exact `char`/newline-policy alias, so snapshots retain every text helper and
positions remain byte-oriented like the existing C++ text rope. Checked `size_t` preflights reject known-count
growth before new element-measure callbacks. This semantic checkpoint adds no zipper, allocation, or benchmark
claim.

The native runner and benchmark harness remain repository-owned, so configuring a preset does not implicitly run
a package manager. The canonical rank policy uses the operating-system CNG provider (`bcrypt`) on Windows and the
system OpenSSL Crypto package elsewhere; these are linked transitively by the exported interface target. There is
deliberately no `vcpkg.json` because neither platform crypto route is acquired through vcpkg.

The active CMake model is C++23 plus MSVC `/std:c++latest`: the interface library advertises `cxx_std_23`, test
targets use `CXX_STANDARD 23`, and MSVC targets receive `/std:c++latest` explicitly because the bundled CMake does
not model the installed compiler's latest language mode as a standard number.

## Build

From this directory:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug --parallel 1 && ""$cmakeDir\ctest.exe"" --preset msvc-debug --parallel 1 --output-on-failure"
```

Use `msvc-release` for the optimized configuration. Keep the Visual Studio environment setup, configure, build,
and CTest run in one `cmd.exe` chain when starting from plain PowerShell; invoking `VsDevCmd.bat` directly from
PowerShell does not persist its environment changes in that process. The checked-in presets do not pin a Visual
Studio installation: they resolve `ninja` from the initialized environment or `PATH`. The validation matrix also covers GCC/MinGW
and Clang Debug/Release CTest lanes in separate `out/build/<compiler>-<configuration>` directories.
Host-agnostic `ninja-debug`, `ninja-release`, and `ninja-asan` presets are available when CMake, Ninja, and a
suitable C++23 compiler are on `PATH`. For release commands, sanitizer validation, stress controls, warning
policy, and generated-output locations, see the [validation guide](docs/validation.md).

CMake builds tests, samples, and the dependency-free benchmark harness by default. Disable individual developer
surfaces with `FINGERTREE_BUILD_TESTS=OFF`, `FINGERTREE_BUILD_SAMPLES=OFF`, or
`FINGERTREE_BUILD_BENCHMARKS=OFF`. The test suite includes a real installed-package consumer: it installs the
headers and package metadata to a private staging prefix, configures a new project with only `find_package`, links
`tools::data_structures::finger_tree`, and runs the resulting executable.

## Install And Consume

Install the header-first package from any configured build directory:

```powershell
cmake --install out/build/msvc-release --prefix out/install/fingertree --config Release
```

An external CMake project can then consume it without a source-tree include path:

```cmake
find_package(ToolsDataStructuresFingerTree 0.1 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE tools::data_structures::finger_tree)
```

Point `CMAKE_PREFIX_PATH` or `ToolsDataStructuresFingerTree_DIR` at the installation prefix when it is outside
CMake's normal search locations. The installed package includes version compatibility metadata and only public
headers as code artifacts, plus the repository MIT-0 license under the installation data directory; repository
tests, samples, and benchmarks are never part of the consumer build.

## Layout

- `include/tools/data_structures/finger_tree/` contains the public header-first library.
- `include/tools/data_structures/finger_tree/detail/` contains implementation helpers.
- `tests/` contains the [CTest-registered native smoke runner](tests/README.md) and shared test support.
- `samples/` contains the deterministic [showcase and persistent-snapshot tour](samples/README.md).
- `benchmarks/` contains the [dependency-free Milestone 8 harness](benchmarks/README.md).
- `cmake/` contains the installed-package configuration and consumer-smoke driver.
- `docs/` contains the port plan and C++-specific API, usage, implementation, review, and validation notes.
