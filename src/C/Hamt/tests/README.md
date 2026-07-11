# C HAMT Tests

- Created (UTC): 2026-07-02T21:13:37Z
- Repository HEAD: 30159246f73321480596ee7d9971a951f939280d
- Audience: Maintainers validating the C HAMT port
- Scope: Native test executable and source organization under `src/C/Hamt/tests`

`hamt_tests.c` is the dependency-free native test executable for the C HAMT port. The workspace
[`build.ps1`](../build.ps1) script compiles it together with `src/hamt.c` into
`build/<Configuration>/hamt_tests.exe`.

`patricia_tests.c` is the companion dependency-free executable for the explicit-width Patricia
maps and sets. The build script compiles it with `src/patricia.c` into
`build/<Configuration>/patricia_tests.exe` and runs it after the HAMT suite.

The runner keeps a static table of named test cases, prints `[PASS]` after each successful case, and exits on the
first failed check with file, line, and expression diagnostics. A successful run ends with `<N> test(s) passed`.

## Test Cases

The executable registers these cases:

- `empty map has no entries`
- `set item adds replaces and preserves old versions`
- `add and try_add reject duplicates`
- `remove and try_remove delete present keys`
- `set_many and clear preserve contracts`
- `create_range last wins and retains first equivalent key`
- `equal hash collision bucket preserves every key`
- `deep shared hash prefixes lookup and remove correctly`
- `depth seven iterator traversal`
- `allocation failures unwind node_set and merge`
- `collision bucket splits and hash mismatch probes miss`
- `collision bucket equal value keeps root and key object`
- `structure root shape and sharing`
- `CHAMP independent histories and typed diff`
- `iterator copy advances independently`
- `random history matches model and preserves snapshots`
- `scripted collision snapshot story`
- `random history with colliding hashes matches model`
- `set add remove contains and persistence`
- `set custom comparer retains first item`
- `set algebra matches model`
- `set symmetric_except treats duplicates as one item`
- `concurrent retained snapshot reads`

## Build And Run

From `src/C/Hamt`, build and run the Debug test executable:

```powershell
.\build.ps1 -RunTests
```

Run the built executable directly when changing runner diagnostics or investigating a local failure:

```powershell
.\build\Debug\hamt_tests.exe
```

Use the workspace [validation guide](../docs/validation.md) for Release validation, compiler flags, generated-output
locations, and coverage policy.

Run the Patricia executable directly when investigating integer-trie behavior:

```powershell
.\build\Debug\patricia_tests.exe
```

Its deterministic coverage includes 32-/64-bit signed ordering, persistent snapshots, no-op root
identity, fixed and callback-combining map algebra, set algebra, randomized model histories, and
retain/release accounting.
