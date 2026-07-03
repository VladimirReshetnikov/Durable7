# Code Review Report — `DataStructures` snapshot

Generated: 2026-07-03
Artifact reviewed: `DataStructures(1).zip`
Excluded from review: `src/CSharp/docs/FingerTree/external/`

## Executive summary

This is a strong snapshot overall. The repository is unusually well-organized, unusually well-documented, and backed by a real testing culture rather than just a few happy-path smoke tests. The C# workspace in particular looks like the semantic anchor for the ports: nullable enabled, XML-doc warnings elevated to errors, samples checked into the solution, benchmarks separated cleanly, and very broad test coverage.

The review found **two real native-port correctness defects** that I would treat as blocking for release-quality confidence:

1. **C++ FingerTree:** `rope::from_chunks(...)` can segfault immediately because `rope_chunk::from_storage` dereferences a moved-from `shared_ptr` through an argument evaluation order bug.
2. **C FingerTree:** several public wrapper structs are presented as assignable value-like handles, but they internally contain **self-referential policy pointers**. Plain C struct assignment (`queue = next;`, `map = next;`, etc.) can leave dangling pointers and produces sanitizer-visible use-after-scope failures in the repo’s own tests, samples, and usage docs.

After those are fixed, the remaining issues are mostly about **validation reach and portability**, not core data-structure design. The HAMT workspaces looked healthy in the validations I could run. The Kotlin ports also built and passed their executable tests in a non-Windows environment even though the checked-in bootstrap script is Windows-only.

## Finding summary

| ID | Severity | Confidence | Area | Summary |
| --- | --- | --- | --- | --- |
| CR-01 | Critical | High | `src/Cpp/FingerTree` | `rope::from_chunks` crashes because `rope_chunk::from_storage` can read `storage->size()` after moving `storage` |
| CR-02 | High | High | `src/C/FingerTree` | Self-owned wrapper policies make plain struct assignment unsafe; ASan reports stack-use-after-scope in repo tests/samples |
| CR-03 | Medium | High | C/C++ FingerTree build layer | Hard-coded MSVC/Visual Studio Ninja presets reduce portability and, more importantly, reduce bug-finding coverage |
| CR-04 | Medium | High | Kotlin bootstrap | `src/Kotlin/build.ps1` is Windows-specific even though the Kotlin sources/tests themselves are portable |

## Scope and method

I reviewed the repository snapshot as a mixed static + dynamic code review.

### Static review scope

After excluding `src/CSharp/docs/FingerTree/external/`, the snapshot contains **392 files** and about **101,179 text lines**.

I read the top-level repo guidance and workspace maps, then sampled source and tests across every language root, with the deepest attention on:

- `src/CSharp/src`, `src/CSharp/tests`, and workspace validation/docs
- `src/C/FingerTree` and `src/Cpp/FingerTree`
- `src/C/Hamt` and `src/Cpp/Hamt`
- `src/Kotlin/Hamt` and `src/Kotlin/FingerTree`
- representative Rust and Haskell files/docs where execution was not possible locally

I also ran an internal Markdown link-resolution check over repository-owned `*.md` files (again excluding `src/CSharp/docs/FingerTree/external/`). That check found **0 broken internal links**.

### Dynamic validation I was able to run

| Workspace | Command shape | Result |
| --- | --- | --- |
| `src/C/Hamt` | `gcc ... src/hamt.c tests/hamt_tests.c` | Passed |
| `src/C/Hamt` with ASan/UBSan | `gcc -fsanitize=address,undefined ...` | Passed |
| `src/Cpp/Hamt` | `g++ ... tests/persistent_hamt_tests.cpp` | Passed |
| `src/Cpp/Hamt` with ASan/UBSan | `g++ -fsanitize=address,undefined ...` | Passed |
| `src/Kotlin/Hamt` | `kotlinc ... -jvm-target 20 && java -jar ...` | Passed |
| `src/Kotlin/FingerTree` | `kotlinc ... -jvm-target 20 && java -jar ...` | Passed |
| `src/C/FingerTree` | CMake + Ninja + CTest debug build | Passed |
| `src/C/FingerTree` with ASan/UBSan | CMake + Ninja + sanitizer flags + CTest | Failed with reproducible use-after-scope |
| `src/Cpp/FingerTree` | CMake + Ninja + CTest debug build | Smoke suite segfaulted |
| `src/Cpp/FingerTree` minimal repro | `g++ -fsanitize=address,undefined ... /tmp/rope_probe.cpp` | Reproduced `rope::from_chunks` crash |

### Toolchain limits in this environment

I could not execute these workspaces here because the corresponding toolchains were absent:

- `src/CSharp`: `dotnet` not installed
- `src/Rust`: `cargo` / `rustc` not installed
- `src/Haskell`: `ghc` / `cabal` not installed

So my comments on those areas are static-review comments only, not execution-backed verdicts.

## What is already very good

### 1. Repository hygiene is excellent

The repo is navigable in a way that most multi-language repositories are not. `README.md`, `docs/reference/workspace-map.md`, `docs/reference/test-suite-map.md`, and `docs/guides/build-and-validation.md` make it clear what the canonical implementations are, where the ports stand, how to build each workspace, and how to reason about semantic parity.

This matters because the repo is doing something inherently tricky: it carries one semantic family across multiple languages without turning into a pile of half-related ports.

### 2. The testing story is real, not ornamental

The managed workspace is especially strong here:

- **64 C# test files**
- **531 `[Fact]` tests** and **28 `[Theory]` tests** in the checked-in C# test tree
- sample smoke coverage wired into the C# solution
- property/model-based test coverage called out explicitly in the local READMEs

The native FingerTree ports also have substantial checked-in coverage:

- the C++ smoke runner registers **122 named tests**
- the C FingerTree test executable covers **17 named test areas** plus sample smoke executables

That breadth is a big positive. The defects I found are not signs of no tests; they are signs of the kinds of defects that **ordinary test shapes and one-platform validation can miss**.

### 3. The HAMT ports looked healthy in the validation I could run

Both C and C++ HAMT passed cleanly under ordinary local builds and under ASan/UBSan. That does not prove perfection, but it is a strong signal that the porting discipline there is paying off.

### 4. The C# workspace is set up like a serious maintained library

`src/CSharp/Directory.Build.props` enables nullable reference types, preview language features, XML doc generation, and promotes key XML doc warnings to errors. That is exactly the kind of friction that keeps a public library healthy over time.

I could not run the C# solution here, so I am not making an execution-backed claim about correctness. But from structure alone, this looks like the canonical workspace and the one the other ports should continue to follow semantically.

## Detailed findings

## CR-01 — Critical — C++ FingerTree `rope::from_chunks` can dereference a moved-from `shared_ptr`

**Location:** `src/Cpp/FingerTree/include/tools/data_structures/finger_tree/detail/rope_chunk.hpp:38-45`
**Related path:** `src/Cpp/FingerTree/include/tools/data_structures/finger_tree/detail/measured_rope_chunk.hpp:53-63`

### What I found

`rope_chunk::from_storage` currently returns a chunk like this:

```cpp
return rope_chunk{std::move(storage), 0, storage->size()};
```

That is not safe.

The constructor arguments are not guaranteed to evaluate in the order the code visually suggests. `std::move(storage)` may be evaluated before `storage->size()`. Once `storage` has been moved from, dereferencing it is invalid. On libstdc++/GCC 14 in this environment, that becomes an immediate null dereference.

### Why this matters

This is not a theoretical standard-lawyer nit. It is **directly user-visible through the public API**:

- `tools::data_structures::finger_tree::rope<T>::from_chunks(...)`
- and, via delegation, measured-rope chunk construction paths as well

Any caller who constructs a rope from external shared chunk storage is exposed.

### Evidence

#### A. The C++ FingerTree smoke suite segfaults

A CMake/Ninja build of `src/Cpp/FingerTree` completed, but the smoke test executable segfaulted after the reversible-deque tests and before the rope tests finished.

The last visible output was the tail end of the reversible-deque section, after which CTest reported:

```text
Test #1: fingertree.smoke .................***Exception: SegFault
```

That narrowed the fault to the rope portion of the suite.

#### B. A minimal ASan/UBSan repro fails immediately

This tiny program is enough:

```cpp
#include <tools/data_structures/finger_tree/finger_tree.hpp>
#include <memory>
#include <vector>

namespace ft = tools::data_structures::finger_tree;

int main() {
    auto first = std::make_shared<const std::vector<int>>(std::vector<int>{1,2,3});
    auto r = ft::rope<int>::from_chunks({first});
    return static_cast<int>(r.size());
}
```

With `-fsanitize=address,undefined`, the failure points directly at `rope_chunk.hpp:44`:

```text
include/tools/data_structures/finger_tree/detail/rope_chunk.hpp:44:63: runtime error:
member call on null pointer
...
#1 tools::data_structures::finger_tree::detail::rope_chunk<int>::from_storage(...)
#2 tools::data_structures::finger_tree::rope<int>::from_chunks(...)
```

### Recommended fix

Capture the size before moving the pointer:

```cpp
const auto size = storage->size();
return rope_chunk{std::move(storage), 0, size};
```

That is the entire fix.

In my validation session, caching the size before the move eliminated the minimal repro crash immediately. I would also add two regression tests:

1. `rope<T>::from_chunks({shared_ptr})` with a single non-empty external chunk
2. the analogous measured-rope construction path

### Priority

**Fix this first.** It is a concrete crash in public API surface.

---

## CR-02 — High — C FingerTree self-owned wrappers are not safe value types

**Primary locations:**

- `src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h:49-52` (`ft_tree`)
- `src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h:217-225` (`ft_sorted_map`)
- `src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h:277-283` (`ft_rope`)
- `src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h:310-317` (`ft_measured_rope`)
- `src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h:373-382` (`ft_priority_queue`)
- `src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h:416-419` (`ft_interval_tree_i64`)

**Representative implementation sites:**

- `src/C/FingerTree/src/fingertree.c:2133-2146`
- `src/C/FingerTree/src/fingertree.c:5187`
- `src/C/FingerTree/src/fingertree.c:5376`
- `src/C/FingerTree/src/fingertree.c:5422`
- `src/C/FingerTree/src/fingertree.c:5751`
- `src/C/FingerTree/src/fingertree.c:6502`
- `src/C/FingerTree/src/fingertree.c:7286`
- `src/C/FingerTree/src/fingertree.c:7359`
- `src/C/FingerTree/src/fingertree.c:7436`
- `src/C/FingerTree/src/fingertree.c:7492`
- `src/C/FingerTree/src/fingertree.c:7535`
- `src/C/FingerTree/src/fingertree.c:7559`

### What I found

The underlying generic tree handle is:

```c
typedef struct ft_tree {
    const ft_tree_policy* policy;
    ft_tree_rep* rep;
} ft_tree;
```

That is fine **when the policy is external and stable**.

The problem appears in wrappers that **embed their own `ft_tree_policy policy;` field** and also contain an `ft_tree tree;`. Those wrappers then explicitly rebind the tree back to the wrapper’s internal policy, e.g.:

```c
result->tree.policy = &result->policy;
```

That pattern is repeated in `ft_sorted_map`, `ft_rope`, `ft_measured_rope`, `ft_priority_queue`, and interval-tree wrappers.

A plain C struct assignment then copies the stale interior pointer as-is:

```c
queue = next;
map = next;
```

After that assignment, `queue.tree.policy` or `map.tree.policy` still points into the **old source wrapper object**, not the destination object. Once the source wrapper leaves scope or is disposed, the destination holds a dangling pointer.

In other words: these wrappers look like ordinary assignable C value types, but they are **not closed under struct assignment**.

### Why this matters

This is a public-API design hazard, not just an internal bug.

- The wrappers are public structs, not opaque handles.
- C programmers will naturally assign structs by value.
- The repo’s own tests, samples, and docs demonstrate exactly that usage pattern.

### Evidence

#### A. ASan/UBSan catches the bug in the repo’s own tests

A sanitizer build of `src/C/FingerTree` passed ordinary compilation and then failed in `fingertree_c.core` with a stack-use-after-scope:

```text
ERROR: AddressSanitizer: stack-use-after-scope
READ of size 8 at ...
#0 ft_value_copy ... src/fingertree.c:248
#4 ft_sorted_map_entry_at ... src/fingertree.c:5297
#5 test_sorted_map ... tests/fingertree_c_tests.c:876
```

ASan identifies the invalid memory as being inside the stack variable `next` from this exact pattern in the test:

- `src/C/FingerTree/tests/fingertree_c_tests.c:866-869`

```c
ft_sorted_map next;
ft_sorted_map_insert(&map, &keys[index], &values[index], &next);
ft_sorted_map_dispose(&map);
map = next;
```

#### B. ASan/UBSan also catches it in the checked-in sample executable

The sanitizer build also failed in `fingertree_c.sample.showcase`:

```text
ERROR: AddressSanitizer: stack-use-after-scope
#0 ft_value_copy ... src/fingertree.c:248
#4 ft_priority_queue_try_peek ... src/fingertree.c:7380
#5 ft_priority_queue_try_pop ... src/fingertree.c:7409
#6 run_priority_queue ... samples/showcase.c:48
```

The invalid memory again came from the stack variable `next` in the sample:

- `src/C/FingerTree/samples/showcase.c:32-39`
- `src/C/FingerTree/samples/showcase.c:47-55`

#### C. The public usage guide teaches the unsafe pattern

The problem is reinforced by the docs. `src/C/FingerTree/docs/usage.md` shows:

- `queue = next;` and `queue = rest;` at lines `213-239`
- `intervals = next;` at lines `249-262`

So this is not “user misuse”; it is the repository’s documented persistent-update style for these wrappers.

### Scope of impact

This specific issue does **not** apply equally to every C handle type.

- `ft_tree`, `ft_persistent_deque`, `ft_sorted_set`, `ft_sorted_multiset`, and `ft_reversible_deque` take external policy pointers and are not using the same self-owned wrapper-policy pattern.
- The hazard applies to wrappers that embed a policy and then point their internal `ft_tree` at that embedded policy.

The effect is immediately reproducible for **sorted map** and **priority queue** because later operations dereference policy-driven copy logic. For ropes and interval-tree wrappers the same structural hazard is present; whether it explodes immediately depends on which later operations touch the dangling policy pointer.

### Recommended fix

I do **not** think this should be papered over with a documentation note alone.

#### Best fix

Redesign the affected public wrappers so that they are either:

- opaque handles with implementation-owned storage, or
- thin structs pointing at a separately allocated stable wrapper state block, so copying the struct keeps a valid policy pointer

#### Acceptable transitional fix

If you want to preserve the current public shape temporarily:

1. explicitly document that plain assignment / `memcpy` / by-value return are invalid for affected wrapper types,
2. provide explicit replace/move/swap helpers,
3. update every sample/test/doc to use those helpers,
4. add sanitizer validation to prevent regressions.

But I would still call that a stopgap, because the current public shape strongly invites the wrong usage.

### Priority

**Fix second, immediately after CR-01.** It is real UB in public documented usage.

---

## CR-03 — Medium — the native FingerTree build layer is too tightly bound to one MSVC installation

**Locations:**

- `src/C/FingerTree/CMakePresets.json:10-27`
- `src/Cpp/FingerTree/CMakePresets.json:10-27`
- related build assumptions throughout the FingerTree validation guides

### What I found

The checked-in FingerTree CMake presets only define `msvc-debug` and `msvc-release`, and both hard-code `CMAKE_MAKE_PROGRAM` to a Visual Studio Insiders Ninja path:

```json
"CMAKE_MAKE_PROGRAM": "C:/Program Files/Microsoft Visual Studio/18/Insiders/.../ninja.exe"
```

That is consistent with the repo’s documented Windows baseline, so this is **not** a contradiction of the current docs. But it is still an engineering limitation, because the code is closer to portable than the preset layer suggests.

### Why this matters

The important point is not just contributor convenience. The more important point is that **both substantive native defects from this review surfaced only when I validated outside the default MSVC path**:

- GCC/UBSan/ASan exposed the C++ rope crash directly
- ASan exposed the C wrapper handle-lifetime bug directly

The codebase benefits from a broader validation surface than the current preset story encourages.

### Evidence from this review

- `src/C/FingerTree` built and passed CTest on Linux/Ninja without any source changes.
- `src/Cpp/FingerTree` also configured and built on Linux/Ninja; the smoke suite then exposed a real defect.
- The current preset files are the main portability bottleneck, not the code itself.

### Recommended fix

Keep the existing MSVC presets, but add at least:

- `ninja-debug`
- `ninja-release`
- `clang-asan`
- `gcc-asan`

and stop hard-coding the Ninja executable path when `ninja` can be resolved from `PATH`.

I would also add a short sanitizer section to both FingerTree validation guides, because the defects found here are exactly the kind of bugs sanitizers are good at catching.

---

## CR-04 — Medium — `src/Kotlin/build.ps1` is Windows-specific even though the Kotlin code is portable

**Location:** `src/Kotlin/build.ps1`

### What I found

The script bootstraps a Windows JDK and assumes Windows tool names and paths:

- downloads `temurin-jdk-21-windows-x64.zip` (`lines 70-73`)
- searches for `java.exe` (`lines 75-80`)
- expects `kotlinc.bat` (`lines 98-103`)
- hard-targets JVM 21 in the compile step (`line 139`)

### Why this matters

The Kotlin sources themselves are not the problem. In this review environment, both Kotlin workspaces compiled and ran successfully with the ambient Linux `kotlinc` toolchain using `-jvm-target 20`.

So the portability limitation is in the bootstrap script, not in the port logic.

### Evidence from this review

Both of these passed locally:

- `src/Kotlin/Hamt`
- `src/Kotlin/FingerTree`

### Recommended fix

Either:

- make the bootstrap logic OS-aware, or
- add a minimal Gradle wrapper / shell entry point, or
- keep `build.ps1` but add a documented “ambient toolchain” path that works cross-platform without the Windows bootstrap branch

This is not a correctness blocker like CR-01/CR-02, but it is a meaningful quality-of-life and validation-reach improvement.

## Suggested fix order

1. **CR-01:** patch `rope_chunk::from_storage` and add a direct regression test for `from_chunks({shared_ptr})`
2. **CR-02:** redesign or stabilize the C self-owned wrapper policy model; at minimum stop documenting plain assignment for affected wrappers
3. **Add sanitizer validation lanes** for native FingerTree workspaces
4. **Loosen preset/bootstrap portability** so Linux/Clang/GCC validation becomes routine instead of ad hoc

## Areas that looked healthy from this review

These are not blanket proofs of correctness, but they are worth calling out because they reduce the review surface that needs urgent attention:

- **C HAMT:** passed ordinary and sanitizer-backed local validation
- **C++ HAMT:** passed ordinary and sanitizer-backed local validation
- **Kotlin HAMT / FingerTree:** both executable test jars passed locally
- **Repository-owned Markdown docs:** internal link graph resolved cleanly
- **C# workspace structure:** strong static signs of maintainability and serious test discipline

## Final assessment

This is a high-quality repository with two native-port issues that are serious precisely because the rest of the work is so careful. The architecture, docs, and tests are better than average by a wide margin. I would not describe the snapshot as shaky; I would describe it as **strong, with two defects that deserve immediate correction and with a validation surface that should be widened so this class of bug gets caught earlier**.

If you fix CR-01 and CR-02 and then add sanitizer-backed native validation, the repo’s overall confidence level goes up substantially.

## Appendix A — Commands run

These are representative commands from the validation pass.

### C HAMT

```bash
gcc -std=c11 -Iinclude src/hamt.c tests/hamt_tests.c -o /tmp/hamt_c_tests && /tmp/hamt_c_tests

gcc -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iinclude src/hamt.c tests/hamt_tests.c \
    -o /tmp/hamt_c_tests_asan && /tmp/hamt_c_tests_asan
```

### C++ HAMT

```bash
g++ -std=c++20 -Iinclude tests/persistent_hamt_tests.cpp \
    -o /tmp/hamt_cpp_tests && /tmp/hamt_cpp_tests

g++ -std=c++20 -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iinclude tests/persistent_hamt_tests.cpp \
    -o /tmp/hamt_cpp_tests_asan && /tmp/hamt_cpp_tests_asan
```

### Kotlin HAMT / FingerTree

```bash
kotlinc $(find src test -name '*.kt' | sort) -jvm-target 20 -include-runtime -d ...
java -jar ...
```

### C FingerTree

```bash
cmake -S . -B /tmp/c-ft-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c-ft-build
ctest --test-dir /tmp/c-ft-build --output-on-failure
```

Sanitizer pass:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

### C++ FingerTree minimal repro

```bash
g++ -std=c++23 -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iinclude /tmp/rope_probe.cpp -o /tmp/rope_probe && /tmp/rope_probe
```

## Appendix B — Dynamic validation limitations

I did not execute the following due missing local toolchains in this environment:

- `src/CSharp` (`dotnet` missing)
- `src/Rust` (`cargo` / `rustc` missing)
- `src/Haskell` (`ghc` / `cabal` missing)

That means this report contains **execution-backed findings** for the native C/C++ and Kotlin workspaces, and **static-review observations only** for C#, Rust, and Haskell.
