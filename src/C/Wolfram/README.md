# C Wolfram Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the C Wolfram-collections port
- Scope: C API, implementation, and tests under `src/C/Wolfram`

This workspace is the C17 port of the Wolfram `List` and `Association` collection family. It exposes
type-erased, explicit-lifetime APIs through
[`wolfram.h`](include/tools/data_structures/wolfram/wolfram.h).

- `tds_wolfram_list` is a value-type facade over the C FingerTree persistent deque.
- `tds_wolfram_association` composes the C HAMT with an internal ref-counted AVL sequence ordered by
  sparse stamps. The HAMT gives keyed lookup and stored-key recovery; the stamp sequence gives ordered
  traversal, indexed access, keyed position lookup, positional edits, relabeling, slicing, and sorting
  with the same asymptotic bounds as the C# reference.

Build and test from `src/C`:

```powershell
.\build.ps1 -Workspace Wolfram -RunTests
.\build.ps1 -Workspace Wolfram -Configuration Release -RunTests
```

The dependency-free CTest executable in [`tests`](tests/wolfram_c_tests.c) covers list operations,
Wolfram Association ordering rules, custom key policies, relabel stress, and deterministic generated
histories against an ordered-pair model.
