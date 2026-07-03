# Code review report: `DataStructures.zip`

**Review date:** 2026-07-03
**Snapshot:** uploaded repository archive extracted locally from `/mnt/data/DataStructures.zip`
**Explicit exclusion:** `src/CSharp/docs/FingerTree/external/` was not reviewed or counted.
**Reviewed size:** 390 regular files excluding the external directory, plus 2 symlinked guidance aliases (`AGENTS.md`, `CLAUDE.md`). The main reviewed code corpus is approximately 40.8k lines of C#, 12.7k lines of C, 12k lines of C++ headers, 7k lines of Rust, 2.7k lines of Kotlin, and 2.6k lines of Haskell.

## Executive summary

This is a serious, ambitious repository with unusually good documentation discipline for a data-structure porting project. The validation matrix is coherent, Markdown links are clean, many tests are model-like rather than just example-like, and the native/C# code generally tries to preserve snapshot semantics instead of mutating in place. The strongest parts I could validate locally were the HAMT ports and the non-sanitized C FingerTree build. The most important problems I found are not stylistic; they are concrete correctness and memory/lifetime defects.

The two release-blocking findings are:

1. **C FingerTree composite structs are not safely movable by ordinary C assignment, but first-party tests, samples, and docs use exactly that pattern.** Under AddressSanitizer, the C FingerTree suite fails with `stack-use-after-scope` in `ft_sorted_map` and `ft_priority_queue`. The root cause is an internal pointer such as `map.tree.policy` or `queue.tree.policy` pointing to the `policy` subobject of a temporary `next` variable after `current = next;`. This is undefined behavior and can become use-after-scope, double-free-like ownership confusion, or corrupted callbacks depending on stack reuse.

2. **C++ FingerTree `rope<T>::from_chunks` segfaults on non-empty small chunks.** `rope_chunk<T>::from_storage` moves a `std::shared_ptr` and then evaluates `storage->size()` from the moved-from pointer in the same braced initializer. The full C++ FingerTree smoke test fails with a segfault, and a small AddressSanitizer repro points directly at `rope_chunk.hpp:44`.

The most important non-release-blocking findings are:

- C# `Rope<T>` and `MeasuredRope<T,...>` range checks use `index + count > Count`; this can overflow and allow invalid ranges to proceed, including potential huge allocation in `GetRange`.
- C# wide integer parse APIs document `NumberStyles` as a bitwise combination but internally accept only exact `NumberStyles.Integer` or exact `AllowHexSpecifier`-only styles. This rejects normal callers using `NumberStyles.HexNumber`, while the code then trims whitespace unconditionally.
- Kotlin FingerTree checkpoint code repeats the same `start + count > size` overflow pattern in range APIs.
- Native build presets are effectively MSVC-only even though the CMake files have GCC/Clang branches and the Linux/GCC checks are useful enough to expose real bugs. There is no CI workflow in the snapshot.

## Validation performed

I used the repository’s own validation guide as the starting point, then adapted where the container lacked the expected Windows/.NET/Rust/Haskell tooling.

### Tooling available locally

- GCC/G++: `14.2.0`
- CMake: `3.31.6`
- Ninja: `1.12.1`
- Java: OpenJDK `21.0.10`
- Kotlin compiler on `PATH`: `kotlinc-jvm 1.9.0`

### Tooling not available locally

- `dotnet` was not installed, so the C# solution could not be built or tested.
- `cargo`/`rustc` were not installed, so Rust tests could not be run.
- `ghc`/`cabal` were not installed, so Haskell tests could not be run.
- PowerShell was not installed, so the repository’s `*.ps1` orchestration scripts could not be executed directly.

### Checks and results

| Area | Command style | Result |
| --- | --- | --- |
| C FingerTree, normal GCC/CMake/CTest | `cmake -S src/C/FingerTree ...`, `cmake --build`, `ctest` | **Pass:** 3/3 tests passed. |
| C FingerTree, GCC + ASan/UBSan | same CMake build with `-fsanitize=address,undefined` | **Fail:** 2/3 tests failed; sanitizer reports `stack-use-after-scope`. |
| C HAMT, GCC | direct `gcc -std=c17 -Wall -Wextra -Wpedantic -Werror ...` | **Pass:** 18/18 tests passed. |
| C HAMT, GCC + ASan/UBSan | direct GCC sanitizer build | **Pass:** 18/18 tests passed. |
| C++ HAMT, G++ | direct `g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror ...` | **Pass:** 22/22 tests passed. |
| C++ HAMT, G++ + ASan/UBSan | direct G++ sanitizer build | **Pass:** 22/22 tests passed. |
| C++ FingerTree, G++/CMake/CTest | `cmake -S src/Cpp/FingerTree ...`, `cmake --build`, `ctest` | **Fail:** `fingertree.smoke` segfaults. |
| Kotlin HAMT | compiled and ran with installed `kotlinc 1.9.0`, `-jvm-target 20` | **Pass:** all 7 executable checks passed. |
| Kotlin FingerTree | compiled and ran with installed `kotlinc 1.9.0`, `-jvm-target 20` | **Pass:** all 10 executable checks passed. |
| Kotlin with repository target | installed `kotlinc 1.9.0` and `-jvm-target 21` | **Fail locally:** compiler does not support JVM target 21; repo script expects Kotlin 2.4.0 bootstrap. |
| Markdown local link check | Python link checker over `*.md`, excluding external directory | **Pass:** 0 missing local Markdown targets. |
| stale path/typo scan from validation guide | `rg` scan excluding external and migration provenance | **Pass:** no hits. |

## Priority findings

### P0-1. C FingerTree composite public structs contain self-pointers and become invalid after ordinary assignment

**Severity:** High / release-blocking for the C FingerTree API
**Area:** `src/C/FingerTree`
**Evidence:** ASan/UBSan CTest failure; first-party tests, samples, and docs use the unsafe pattern.

The regular C FingerTree build passes:

```text
100% tests passed, 0 tests failed out of 3
```

But the same suite under AddressSanitizer and UndefinedBehaviorSanitizer fails:

```text
1/3 Test #1: fingertree_c.core ................***Failed
ERROR: AddressSanitizer: stack-use-after-scope
#0 ft_value_copy src/C/FingerTree/src/fingertree.c:248
#1 ft_element_copy_leaf_at src/C/FingerTree/src/fingertree.c:1626
#2 ft_tree_at src/C/FingerTree/src/fingertree.c:2215
#3 ft_sorted_map_entry_at src/C/FingerTree/src/fingertree.c:5297
#4 test_sorted_map src/C/FingerTree/tests/fingertree_c_tests.c:876
...
Address ... is located in stack frame test_sorted_map ... 'next' (line 866)
```

The showcase sample also fails:

```text
2/3 Test #2: fingertree_c.sample.showcase .....***Failed
ERROR: AddressSanitizer: stack-use-after-scope
#0 ft_value_copy src/C/FingerTree/src/fingertree.c:248
#1 ft_tree_at src/C/FingerTree/src/fingertree.c:2215
#2 ft_priority_queue_try_peek src/C/FingerTree/src/fingertree.c:7380
#3 ft_priority_queue_try_pop src/C/FingerTree/src/fingertree.c:7409
#4 run_priority_queue src/C/FingerTree/samples/showcase.c:48
...
Address ... is located in stack frame run_priority_queue ... 'next' (line 32)
```

The root pattern is visible in the tests:

```c
ft_sorted_map next;
REQUIRE_STATUS(ft_sorted_map_insert(&map, &keys[index], &values[index], &next), FT_STATUS_OK);
ft_sorted_map_dispose(&map);
map = next;
```

and in the priority queue sample:

```c
ft_priority_queue next;
if (ft_priority_queue_push(&queue, &values[index], &priorities[index], &next) != FT_STATUS_OK) { ... }
ft_priority_queue_dispose(&queue);
queue = next;
```

The docs also teach the same pattern in `src/C/FingerTree/docs/usage.md:221-226` and `src/C/FingerTree/docs/usage.md:231-239` for priority queues, and `src/C/FingerTree/docs/usage.md:257-262` for interval trees.

The underlying data structures embed a policy object and also contain a nested tree that stores a pointer to that policy object. For example:

```c
typedef struct ft_sorted_map {
    ft_tree_policy policy;
    ft_tree tree;
    ft_value_type key_type;
    ft_value_type value_type;
    ft_compare_fn compare_key;
    void* compare_context;
    ft_sorted_map_entry_context* entry_context;
} ft_sorted_map;
```

and:

```c
typedef struct ft_priority_queue {
    ft_tree_policy policy;
    ft_tree tree;
    ft_value_type value_type;
    ft_value_type priority_type;
    ft_compare_fn compare_priority;
    void* compare_context;
    uint64_t next_ordinal;
    ft_priority_queue_entry_context* entry_context;
} ft_priority_queue;
```

The implementation tries to rebase returned values after operations:

```c
result->tree.policy = &result->policy;
```

Examples include `ft_sorted_map_set`/`insert`/`remove` around `src/C/FingerTree/src/fingertree.c:5376`, `5422`, `5479`, priority queue around `7359` and `7436`, rope around `5652`, `5751`, `5980`, measured rope around `6382`, `6502`, `6833`, and interval tree around `7492`, `7535`, `7559`, `7817`, `7873`, `7904`.

That rebasing is correct only until the struct is moved. After `queue = next;`, the bytes are copied into `queue`, but `queue.tree.policy` still points to `&next.policy`, the policy subobject in the soon-dead stack variable. Later reads of `queue.tree.policy->value.copy` become a use-after-scope. This is a classic self-referential struct relocation bug.

Affected public wrapper types appear to include at least:

- `ft_sorted_map`
- `ft_rope`
- `ft_measured_rope`
- `ft_priority_queue`
- `ft_interval_tree_i64`
- generic `ft_interval_tree`

`ft_tree` itself is less affected because the policy is externally supplied; direct assignment is safe only if the external policy outlives all tree values. `ft_sorted_multiset`/`ft_sorted_set` also use an external policy and are less immediately dangerous, but the public API should be explicit about ownership/lifetime there too.

**Recommended fix:** make public composite values either relocatable or non-relocatable by construction. The safest options are:

1. **Opaque handles:** hide the structs and allocate the state internally. This removes accidental assignment/memcpy/realloc misuse from the public ABI.
2. **Store policy by value inside `ft_tree` and avoid interior pointers.** That makes `ft_tree` physically relocatable, but this needs care because policy contexts such as `entry_context` still point to heap-owned state.
3. **Add explicit move/adopt helpers and forbid direct assignment in docs.** This is the least invasive short-term fix:

```c
void ft_priority_queue_move(ft_priority_queue* destination, ft_priority_queue* source)
{
    *destination = *source;
    destination->tree.policy = &destination->policy;
    memset(source, 0, sizeof *source);
}
```

Each composite type needs its own rebasing helper. Tests, samples, and docs should replace `current = next;` with `ft_*_move(&current, &next);`. The source should be zeroed so accidental later disposal is harmless.

4. **Add an internal invariant check in debug/sanitizer builds:** for wrapper types with embedded policies, verify `wrapper->tree.policy == &wrapper->policy` before every public operation.

**Suggested regression tests:** keep the current assignment-shaped test, but express it through the official move helper. Also add an ASan CI job that runs exactly the first-party sample patterns. This bug is nearly invisible without sanitizers because stack slots are often reused in a benign-looking way.

### P0-2. C++ FingerTree `rope<T>::from_chunks` dereferences a moved-from `shared_ptr`

**Severity:** High / release-blocking for C++ FingerTree
**Area:** `src/Cpp/FingerTree`
**Evidence:** CTest segfault; independent ASan repro; minimal local fix verified against the repro.

The C++ FingerTree build with GCC 14 completes, but the single smoke test fails:

```text
1/1 Test #1: fingertree.smoke .................***Exception: SegFault  5.20 sec
0% tests passed, 1 tests failed out of 1
```

The last passing line before the segfault is:

```text
[pass] reversible deque empty behavior is degenerate
```

`smoke_tests.cpp` calls `add_reversible_deque_tests(tests);` and then `add_rope_tests(tests);`, so the failure starts in the rope test family. The first rope test calls:

```cpp
require_sequence_equal(ft::rope<int>::from_chunks({first, empty, large, last}), expected);
```

The minimal ASan repro fails as follows:

```text
ERROR: AddressSanitizer: SEGV on unknown address 0x000000000008
#0 std::vector<int>::size() const
#1 tools::...::detail::rope_chunk<int>::from_storage(...) rope_chunk.hpp:44
#2 tools::...::rope<int>::build_tree_from_chunks(...) rope.hpp:437
#3 tools::...::rope<int>::from_chunks(...) rope.hpp:77
```

The problematic code is in `src/Cpp/FingerTree/include/tools/data_structures/finger_tree/detail/rope_chunk.hpp:38-45`:

```cpp
[[nodiscard]] static rope_chunk from_storage(storage_pointer storage)
{
    if (storage == nullptr) {
        throw std::invalid_argument("rope chunk storage cannot be null");
    }

    return rope_chunk{std::move(storage), 0, storage->size()};
}
```

In this braced initialization, the first initializer moves `storage` into the `rope_chunk` constructor argument, and the third initializer then reads `storage->size()`. After moving a `std::shared_ptr`, the source pointer is empty. ASan shows the resulting null dereference.

The call path is public:

- `rope<T>::from_chunks(...)` at `src/Cpp/FingerTree/include/tools/data_structures/finger_tree/rope.hpp:75-78`
- `build_tree_from_chunks(...)` at `rope.hpp:422-439`
- `tree.append(chunk_type::from_storage(std::move(storage)))` at `rope.hpp:436-438`

Any non-empty chunk with `storage->size() <= max_chunk_size` goes through the bad `from_storage` path. Larger chunks go through `append_split`, so they do not hit this exact line first.

**Fix:** compute the length before moving the pointer.

```cpp
[[nodiscard]] static rope_chunk from_storage(storage_pointer storage)
{
    if (storage == nullptr) {
        throw std::invalid_argument("rope chunk storage cannot be null");
    }

    const auto length = storage->size();
    return rope_chunk{std::move(storage), 0, length};
}
```

I applied this change locally only to the minimal ASan repro, restored the snapshot afterward, and the repro advanced from crashing to producing the expected `from_chunks` size. I did not mark the full C++ smoke suite as verified after the patch because a full template rebuild did not complete within the command timeout.

**Also check:** `measured_rope_chunk::from_storage` at `src/Cpp/FingerTree/include/tools/data_structures/finger_tree/detail/measured_rope_chunk.hpp:53-55` delegates to `rope_chunk::from_storage`, so fixing `rope_chunk` fixes that path as well.

**Suggested regression test:** add a small direct test that constructs a rope from a single small external chunk:

```cpp
auto backing = std::make_shared<const std::vector<int>>(std::vector<int>{1, 2, 3});
auto rope = ft::rope<int>::from_chunks({backing});
FT_REQUIRE_EQUAL(rope.size(), 3U);
FT_REQUIRE_EQUAL(rope.at(0), 1);
```

This should be run under ASan as well as the normal smoke suite.

### P1-1. C# `Rope<T>` and `MeasuredRope<T,...>` range checks can overflow

**Severity:** Medium; correctness and denial-of-service risk via invalid allocation path
**Area:** `src/CSharp/src/Tools.DataStructures.FingerTree`
**Evidence:** static review; contrast with safer `FingerTreeDeque<T>.CheckRange` helper.

`Rope<T>` uses the pattern:

```csharp
if (index < 0 || count < 0 || index + count > Count)
    throw RangeError(index, count);
```

in:

- `Rope.cs:210-213` (`RemoveRange`)
- `Rope.cs:225-228` (`Slice`)
- `Rope.cs:246-252` (`GetRange`)
- `Rope.cs:306-312` (`CopyTo`, with `destination.Length`)

`MeasuredRope<T,TMeasure,TMeasureOps>` repeats the same pattern in:

- `MeasuredRope.cs:214-217`
- `MeasuredRope.cs:229-233`
- `MeasuredRope.cs:250-255`
- `MeasuredRope.cs:390-396`

In C#, `int` arithmetic in an unchecked context wraps. For example, with a small rope, `index = 5` and `count = int.MaxValue` can make `index + count` negative. The guard can pass even though the range is invalid. In `GetRange`, that can lead to `new T[count]` before a later operation has a chance to reject the range, which turns a normal argument error into a huge allocation attempt or `OutOfMemoryException`.

The repository already contains the safer idiom in `FingerTreeDeque<T>.CheckRange`:

```csharp
ArgumentOutOfRangeException.ThrowIfNegative(index);
ArgumentOutOfRangeException.ThrowIfNegative(count);
if (count > Count - index) { ... }
```

That exact shape avoids addition overflow because the subtraction happens after `index` is validated.

**Recommended fix:** add a shared helper for rope ranges and use it everywhere.

```csharp
private void CheckRange(int index, int count)
{
    ArgumentOutOfRangeException.ThrowIfNegative(index);
    ArgumentOutOfRangeException.ThrowIfNegative(count);
    if ((uint)index > (uint)Count || count > Count - index)
        throw RangeError(index, count);
}

private void CheckCopyRange(int index, int length)
{
    ArgumentOutOfRangeException.ThrowIfNegative(index);
    if ((uint)index > (uint)Count || length > Count - index)
        throw RangeError(index, length);
}
```

For `CopyTo`, `Span<T>.Length` cannot be negative, so the only missing pieces are `index > Count` and `destination.Length > Count - index`.

**Suggested tests:** add edge tests for all affected methods:

```csharp
var rope = Rope<int>.FromEnumerable([1, 2, 3]);
Assert.Throws<ArgumentOutOfRangeException>(() => rope.GetRange(2, int.MaxValue));
Assert.Throws<ArgumentOutOfRangeException>(() => rope.Slice(2, int.MaxValue));
Assert.Throws<ArgumentOutOfRangeException>(() => rope.RemoveRange(2, int.MaxValue));
Assert.Throws<ArgumentOutOfRangeException>(() => rope.CopyTo(2, new int[int.MaxValue /* use a smaller crafted span in real test */]));
```

For the `CopyTo` case, avoid actually allocating a giant array in the test; use a custom path or test `index = int.MaxValue` with a small destination.

### P1-2. C# wide integer `NumberStyles` handling is much narrower than the public API contract implies

**Severity:** Medium API compatibility issue
**Area:** `src/CSharp/src/Tools.Numerics`
**Evidence:** static review; tests mostly cover `NumberStyles.Integer` and `AllowHexSpecifier`, not common combinations such as `HexNumber`.

Public parse docs say the `style` parameter is:

```xml
A bitwise combination of <see cref="NumberStyles"/> values that specifies permitted syntactic elements.
```

Example location: `UInt256.cs:270-272` and similar parse overloads across `UInt256`, `UInt512`, `UInt1024`, `Int256`, `Int512`, and `Int1024`.

Internally, decimal parse accepts only exact `NumberStyles.Integer`:

```csharp
if (style != NumberStyles.Integer)
    return false;
```

Example: `UInt256.cs:873-878`; signed variants repeat this, e.g. `Int256.cs:974-979`.

Hex parse accepts only `AllowHexSpecifier` with no other flags:

```csharp
if ((style & ~NumberStyles.AllowHexSpecifier) != 0)
    return false;
text = text.Trim();
```

Example: `UInt256.cs:931-937`; signed equivalent at `Int256.cs:1056-1063`. The same pattern appears in all 256/512/1024 signed and unsigned types:

```text
Int1024.cs:1030-1031, 1111-1115
Int256.cs:975-976, 1056-1060
Int512.cs:996-997, 1077-1081
UInt1024.cs:876-878, 933-937
UInt256.cs:873-877, 931-935
UInt512.cs:874-876, 931-935
```

This creates a mismatch. `NumberStyles.HexNumber` is the normal .NET style for hex input and includes whitespace flags in addition to `AllowHexSpecifier`. The current code rejects it, even though it then trims whitespace unconditionally. Similarly, callers may expect equivalent combinations such as `AllowLeadingWhite | AllowTrailingWhite | AllowLeadingSign` to work for decimal, because the public docs describe a bitwise combination.

**Recommended fix:** decide whether the API intends to mimic .NET parsing or expose a deliberately tiny subset.

If .NET-like behavior is intended:

- Validate supported decimal flags as a subset rather than exact equality.
- Validate supported hex flags as a subset of `AllowHexSpecifier | AllowLeadingWhite | AllowTrailingWhite`.
- Only trim whitespace when the corresponding style bits allow it.
- Add explicit tests for `NumberStyles.HexNumber` and for legal decimal flag subsets.

If the tiny subset is intentional:

- Update public XML docs to say only `NumberStyles.Integer` and `NumberStyles.AllowHexSpecifier` are accepted.
- Stop saying “bitwise combination” in public docs.
- Consider rejecting whitespace under hex if whitespace flags are not accepted; unconditional trimming is surprising when the style says whitespace is not permitted.

### P1-3. Kotlin FingerTree checkpoint repeats overflow-prone range arithmetic

**Severity:** Medium/low; semantic checkpoint correctness
**Area:** `src/Kotlin/FingerTree`
**Evidence:** static review; Kotlin executable tests pass but do not cover overflow edges.

The Kotlin FingerTree README correctly says this is a “semantic checkpoint port” and “does not claim asymptotic parity for every operation.” That is good framing. Within that checkpoint, though, range validation repeats the same overflow pattern as the C# ropes:

```kotlin
if (index < 0 || count < 0 || index + count > size) {
    return null
}
```

Locations:

- `Rope.kt:83-89` (`removeRange`)
- `Rope.kt:91-97` (`slice`)
- `Core.kt:135-143` (`splitRange`)
- `Sorted.kt:122-128` (`SortedBag.getRange`)
- `Sorted.kt:227-233` (`SortedSet.getRange`)
- `Sorted.kt:411-417` (`SortedMap.getRange`)

Kotlin `Int` arithmetic wraps on overflow. An invalid `start + count` can become negative and pass the guard. The subsequent `drop(start + count)`, `take(count)`, or `drop(start).take(count)` may then return a wrong value or throw a library exception instead of returning `null`.

**Recommended fix:** use subtraction after validating the start:

```kotlin
if (index < 0 || count < 0 || index > size || count > size - index) {
    return null
}
```

Add tests with `count = Int.MAX_VALUE` and small non-empty collections.

### P1-4. C copy callbacks abort on allocation failure despite public `FT_STATUS_NO_MEMORY` design

**Severity:** Medium for robust C-library behavior; lower if “abort on OOM during deep value copy” is an explicit policy
**Area:** `src/C/FingerTree/src/fingertree.c`

The C API has `FT_STATUS_NO_MEMORY`, and many public functions carefully return it. However several internal `ft_copy_fn` callbacks allocate and call `abort()` on failure:

- `ft_sorted_map_entry_copy` aborts at `fingertree.c:5017-5025`.
- `ft_rope_chunk_copy` aborts at `fingertree.c:5544-5547`.
- `ft_measured_rope_chunk_copy` aborts at `fingertree.c:6181-6195`.
- `ft_priority_entry_copy` aborts at `fingertree.c:7135-7143`.
- `ft_interval_entry_copy` aborts at `fingertree.c:7687-7694`.

This is understandable given the current callback signature:

```c
typedef void (*ft_copy_fn)(void* destination, const void* source, void* context);
```

There is no way for the callback to report failure. But it weakens the public contract: a function that appears to return `FT_STATUS_NO_MEMORY` may terminate the process on allocation failure once execution enters a value copy callback.

**Recommended fix:** in the long term, change the callback contract to return `ft_status`:

```c
typedef ft_status (*ft_copy_fn)(void* destination, const void* source, void* context);
```

Then propagate failures through `ft_value_copy`, `ft_element_clone`, node creation, tree concat/split paths, etc. If ABI churn is not acceptable, document that allocation failure inside configured value-copy callbacks is fatal, and reserve `FT_STATUS_NO_MEMORY` for allocations outside copy callbacks. That is less ideal, but at least it makes the behavior explicit.

### P2-1. Native CMake presets are MSVC-specific even though GCC/Clang builds are useful and mostly supported

**Severity:** Medium process risk
**Area:** `src/C/FingerTree`, `src/Cpp/FingerTree`, repository validation

The C and C++ FingerTree `CMakeLists.txt` files are not inherently Windows-only. They explicitly configure non-MSVC warnings:

```cmake
-Wall
-Wextra
-Wpedantic
-Werror
```

The C++ project sets `cxx_std_23` and has a normal CMake target model. I built both C and C++ FingerTree workspaces with GCC/Ninja on Linux. That was valuable: GCC/ASan found the C self-pointer bug, and GCC/ASan found the C++ rope crash.

But both `CMakePresets.json` files only define `msvc-debug` and `msvc-release` and hardcode Visual Studio’s bundled Ninja path:

```json
"CMAKE_MAKE_PROGRAM": "C:/Program Files/Microsoft Visual Studio/18/Insiders/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
```

Locations:

- `src/C/FingerTree/CMakePresets.json:8-28`
- `src/Cpp/FingerTree/CMakePresets.json:8-28`

This makes `cmake --preset ...` non-portable even though the build system is otherwise close to portable.

**Recommended fix:** add host-agnostic presets:

```json
{
  "name": "ninja-debug",
  "generator": "Ninja",
  "binaryDir": "${sourceDir}/out/build/ninja-debug",
  "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
}
```

and matching test/build presets. Keep the MSVC presets if they are important, but do not make them the only path.

Also add CI jobs for:

- Windows/MSVC Debug and Release
- Linux/GCC Debug with ASan/UBSan
- Linux/Clang Debug with ASan/UBSan if Clang’s C++23 coverage is sufficient

There are no `.github/workflows` files in the snapshot, so this appears not to be automated at repository level yet.

### P2-2. Toolchain reproducibility is under-specified for C# and Kotlin

**Severity:** Low/medium process risk
**Area:** build/release engineering

The C# workspace targets `net10.0`, suppresses the preview SDK message, and uses C# preview features in `src/CSharp/Directory.Build.props:1-10`:

```xml
<TargetFramework>net10.0</TargetFramework>
<SuppressNETCoreSdkPreviewMessage>true</SuppressNETCoreSdkPreviewMessage>
<LangVersion>preview</LangVersion>
```

That is fine for a forward-looking repo, but there is no `global.json` in the snapshot. A developer or CI machine with the “wrong” preview SDK may get different compiler behavior. Pinning the SDK would make review/build evidence more stable.

The Kotlin script pins `KotlinVersion = "2.4.0"`, verifies a compiler ZIP SHA, and targets JVM 21 (`src/Kotlin/build.ps1:8-9`, `138-139`). It also bootstraps a Windows Temurin JDK if Java 21 is not on `PATH` (`build.ps1:70-83`). That is reasonable for a Windows-first workflow, but it means:

- The Kotlin validation path requires network access unless the tool cache already exists.
- The script is Windows-specific (`java.exe`, `.bat`, PowerShell).
- Local `kotlinc 1.9.0` cannot compile with `-jvm-target 21`; I had to use `-jvm-target 20` to validate semantic tests in this container.

**Recommended fix:** add explicit toolchain documentation and, if cross-platform review is desired, provide either a Gradle build or a small POSIX script that uses an already-installed compiler/JDK. For C#, add `global.json` pinning the expected .NET 10 SDK band.

### P2-3. UTF-8 parsing overloads for C# wide integers allocate strings

**Severity:** Low performance/API polish
**Area:** `src/CSharp/src/Tools.Numerics`

All wide integer UTF-8 parse overloads convert the byte span to a string before parsing:

```csharp
return ParseCore(Encoding.UTF8.GetString(utf8Text), style, provider);
```

and:

```csharp
TryParse(Encoding.UTF8.GetString(utf8Text), style, provider, out result);
```

Locations include:

```text
Int256.cs:331, 446
Int512.cs:331, 446
Int1024.cs:331, 446
UInt256.cs:309, 426
UInt512.cs:309, 426
UInt1024.cs:311, 428
```

This is not a correctness bug, and these types do not currently implement `IUtf8SpanParsable<T>`. But the public overloads look like allocation-avoiding APIs, and the implementation allocates. For numeric code, callers often choose UTF-8 spans specifically to avoid transcoding.

**Recommended fix:** parse ASCII digits directly from `ReadOnlySpan<byte>` for invariant decimal/hex. For culture-specific signs, either keep the string path for non-invariant providers or precompute UTF-8 sign tokens. A pragmatic staged approach:

1. Add direct fast paths for invariant decimal and hex.
2. Fall back to string only when provider-specific sign tokens require it.
3. Add allocation tests or benchmarks for the UTF-8 APIs.

## Positive observations

These are worth keeping; they are not “findings” in the negative sense.

### Documentation and validation discipline is strong

The repository has a cross-workspace validation guide, test maps, local validation docs, and explicit scope language for ports/checkpoints. The Markdown link check found no missing local links outside the excluded external directory. The stale-path scan from the validation guide also found no hits.

### HAMT ports looked solid under available validation

C HAMT and C++ HAMT both compiled cleanly with strict GCC warnings. Their deterministic test suites passed normally and under ASan/UBSan:

```text
C HAMT:   18 test(s) passed
C++ HAMT: 22 test(s) passed
```

The tests cover collision buckets, structural sharing, custom comparers, set algebra, and randomized histories. That is the right shape for persistent hash trie validation.

### Kotlin checkpoint is honestly scoped

`src/Kotlin/FingerTree/README.md` explicitly says the implementation is a semantic checkpoint and does not claim asymptotic parity with the final lazy measured-spine implementation. That wording prevents a lot of confusion. The executable Kotlin tests passed once compiled with the installed compiler’s supported JVM target.

### Rust crates use a good safety baseline

Both Rust crates declare `#![forbid(unsafe_code)]` (`src/Rust/Hamt/src/lib.rs:1`, `src/Rust/FingerTree/src/lib.rs:1`). I could not run Cargo in this container, but that is the right default posture for these data structures.

### C# FingerTreeDeque already has the right range-check idiom

`FingerTreeDeque<T>.CheckRange` uses `count > Count - index`, avoiding addition overflow. The rope code should reuse this shape.

## Suggested next steps

1. **Fix the C FingerTree move/assignment problem first.** It is present in samples/docs/tests and can invalidate every composite wrapper whose nested tree points at an embedded policy. Decide whether the API is opaque, relocatable, or explicitly moved by helper.
2. **Fix C++ `rope_chunk::from_storage` and add a direct `from_chunks` regression.** This is a small code change with high confidence.
3. **Add sanitizer CI for native code.** Require at least Linux/GCC ASan/UBSan for C HAMT, C FingerTree, C++ HAMT, and C++ FingerTree. The C and C++ issues above were both easy for sanitizers to catch.
4. **Fix C#/Kotlin range guards.** This is low-risk and should be backed by overflow-edge tests.
5. **Clarify or broaden C# `NumberStyles` support.** Either implement common `NumberStyles` combinations or tighten public docs to the exact accepted styles.
6. **Pin toolchains.** Add C# `global.json`, host-agnostic CMake presets, and a reproducible Kotlin path that does not require an interactive Windows-only bootstrap for every validator.

## Appendix A: key command evidence

### C FingerTree normal build

```text
1/3 Test #1: fingertree_c.core ................   Passed
2/3 Test #2: fingertree_c.sample.showcase .....   Passed
3/3 Test #3: fingertree_c.sample.snapshots ....   Passed
100% tests passed, 0 tests failed out of 3
```

### C FingerTree sanitizer build

```text
1/3 Test #1: fingertree_c.core ................***Failed
ERROR: AddressSanitizer: stack-use-after-scope
#0 ft_value_copy .../src/C/FingerTree/src/fingertree.c:248
#3 ft_sorted_map_entry_at .../src/C/FingerTree/src/fingertree.c:5297
#4 test_sorted_map .../src/C/FingerTree/tests/fingertree_c_tests.c:876

2/3 Test #2: fingertree_c.sample.showcase .....***Failed
ERROR: AddressSanitizer: stack-use-after-scope
#0 ft_value_copy .../src/C/FingerTree/src/fingertree.c:248
#2 ft_priority_queue_try_peek .../src/C/FingerTree/src/fingertree.c:7380
#3 ft_priority_queue_try_pop .../src/C/FingerTree/src/fingertree.c:7409
#4 run_priority_queue .../src/C/FingerTree/samples/showcase.c:48
```

### C++ FingerTree crash

```text
1/1 Test #1: fingertree.smoke .................***Exception: SegFault
0% tests passed, 1 tests failed out of 1
```

ASan repro:

```text
ERROR: AddressSanitizer: SEGV on unknown address 0x000000000008
#0 std::vector<int>::size() const
#1 detail::rope_chunk<int>::from_storage(...) rope_chunk.hpp:44
#2 rope<int>::build_tree_from_chunks(...) rope.hpp:437
#3 rope<int>::from_chunks(...) rope.hpp:77
```

### Kotlin tests with installed compiler fallback

```text
HAMT:
PASS mapUpdatesPreserveOldVersions
PASS noOpUpdateAndAbsentRemoveShareRoots
PASS addRejectsDuplicates
PASS collisionsAreStoredAndRemoved
PASS iterationStreamsTrieOrder
PASS setItemsAreLastWinsAndRetainOriginalKey
PASS setAlgebraUsesSetMembership

FingerTree:
PASS dequePreservesSnapshots
PASS reversibleDequeUsesLogicalOrientation
PASS reversibleDequeConcatenatesMixedOrientations
PASS reversibleDequeKeepsMixedConcatHistoriesNavigable
PASS measuredTreeSplitsAndLocatesByPrefix
PASS sortedCollectionsKeepOrderAndRelations
PASS sortedMapIsLastWinsAndNavigable
PASS priorityQueueDequeuesStably
PASS intervalTreeUsesClosedOverlapAndCoalesces
PASS ropesEditAndNavigateText
```

### Unavailable toolchains

```text
dotnet: command not found
cargo: command not found
```

I did not infer pass/fail status for the C#, Rust, or Haskell test suites without their toolchains.
