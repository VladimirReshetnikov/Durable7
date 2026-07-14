# C++ Tungsten Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the C++ Tungsten-collections port
- Scope: Header-first C++ Tungsten collection workspace under `src/Cpp/Tungsten`

This workspace ports the C# `Tools.DataStructures.Tungsten` collection family to C++23 value types.
The public headers live under [`include/tools/data_structures/tungsten`](include/tools/data_structures/tungsten):

This is an application-specific leaf port. It may consume the C++ HAMT and FingerTree libraries,
but no general C++ library may depend on Tungsten or treat its kernel-derived behavior as a
baseline. Fork reusable mechanics into an independently owned implementation; C# is authoritative
only for the sibling Tungsten ports. See the normative
[application-leaf boundary](../../../docs/reference/tungsten-application-leaf-boundary.md).

- `persistent_list<T>` wraps the C++ FingerTree persistent deque and exposes the Tungsten `List`
  operation vocabulary.
- `persistent_association<Key, T, Hash, KeyEqual, ValueEqual>` composes the C++ HAMT with a stamp-ordered
  persistent deque, preserving the C# Tungsten Association ordering rules and gapped relabel behavior.
- `tungsten.hpp` is the aggregate include.

Build and test from `src/Cpp`:

```powershell
.\build.ps1 -Workspace Tungsten -RunTests
```

The CTest executable in [`tests`](tests/tungsten_tests.cpp) covers examples, invalid-argument and injected-callback
exception paths, custom equality/hash policies, relabel stress, bulk-builder index rebuilds (policy preservation
and detachment from later versions), generated histories against ordered list/map models (including range,
key-take, and stable sort operations), and retained-snapshot reader threads.
