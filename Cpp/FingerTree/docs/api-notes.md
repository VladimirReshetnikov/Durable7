# C++ FingerTree API Notes

- Status: Initial notes
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Audience: Maintainers implementing and reviewing public C++ APIs
- Scope: C++ naming, contracts, and intentional differences from the C# workspace

The public namespace is `tools::data_structures::finger_tree`.

The C++ port follows the repository's C# semantics, but it uses idiomatic C++ spelling:

- collection observers use `empty`, `size`, `front`, `back`, `at`, and `operator[]`;
- persistent updates return new values and do not mutate existing snapshots;
- absent ranks use `std::optional<std::size_t>` rather than a `-1` sentinel;
- multi-value returns use named result structs with structural equality;
- container types deliberately do not define pointer-based default equality;
- runtime comparators are stored by the sorted collection wrappers, while priority and interval measures use
  compile-time comparison policy state;
- concurrently published structure, lazy-state, and measure-box pointers use atomic `std::shared_ptr` publication.

The first wave targets MSVC `/std:c++latest`, while keeping the implementation to stable C++20/23-era facilities
where practical. CMake currently models the target as `CXX_STANDARD 23` and adds `/std:c++latest` explicitly for
MSVC because this bundled CMake rejects `CXX_STANDARD 26` for the installed compiler.
