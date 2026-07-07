# C Tungsten Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the C Tungsten-collections port
- Scope: C API, implementation, and tests under `src/C/Tungsten`

This workspace is the C17 port of the Tungsten `List` and `Association` collection family. It exposes
type-erased, explicit-lifetime APIs through
[`tungsten.h`](include/tools/data_structures/tungsten/tungsten.h).

- `tds_tungsten_list` is a value-type facade over the C FingerTree persistent deque.
- `tds_tungsten_association` composes the C HAMT with an internal ref-counted AVL sequence ordered by
  sparse stamps. The HAMT gives keyed lookup and stored-key recovery; the stamp sequence gives ordered
  traversal, indexed access, keyed position lookup, positional edits, relabeling, slicing, and sorting
  with the same asymptotic bounds as the C# reference.

Build and test from `src/C`:

```powershell
.\build.ps1 -Workspace Tungsten -RunTests
.\build.ps1 -Workspace Tungsten -Configuration Release -RunTests
```

The dependency-free CTest executable in [`tests`](tests/tungsten_c_tests.c) covers list operations,
Tungsten Association ordering rules, custom key policies, relabel stress, and deterministic generated
histories against an ordered-pair model, plus retained-snapshot reader threads on Windows with a
sequential fallback on other C targets.
