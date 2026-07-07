# C++ Wolfram Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the C++ Wolfram-collections port
- Scope: Header-first C++ Wolfram collection workspace under `src/Cpp/Wolfram`

This workspace ports the C# `Tools.DataStructures.Wolfram` collection family to C++23 value types.
The public headers live under [`include/tools/data_structures/wolfram`](include/tools/data_structures/wolfram):

- `persistent_list<T>` wraps the C++ FingerTree persistent deque and exposes the Wolfram `List`
  operation vocabulary.
- `persistent_association<Key, T, Hash, KeyEqual, ValueEqual>` composes the C++ HAMT with a stamp-ordered
  persistent deque, preserving the C# Wolfram Association ordering rules and gapped relabel behavior.
- `wolfram.hpp` is the aggregate include.

Build and test from `src/Cpp`:

```powershell
.\build.ps1 -Workspace Wolfram -RunTests
```

The CTest executable in [`tests`](tests/wolfram_tests.cpp) covers examples, custom equality/hash
policies, relabel stress, and generated histories against ordered list/map models.
