# C HAMT Tests

- Created (UTC): 2026-07-02T21:13:37Z
- Repository HEAD: 30159246f73321480596ee7d9971a951f939280d
- Audience: Maintainers validating the C HAMT port
- Scope: Native persistent/transient test executables and source organization under `src/C/Hamt/tests`

`hamt_tests.c` is the dependency-free native test executable for the C HAMT port. The workspace
[`build.ps1`](../build.ps1) script compiles it together with `src/hamt.c` into
`build/<Configuration>/hamt_tests.exe`.

`persistent_hash_bag_tests.c` is the focused persistent unordered-multiset executable. The build
script compiles it with `src/hamt.c` and `src/persistent_hash_bag.c` into
`build/<Configuration>/persistent_hash_bag_tests.exe` and runs it after the core HAMT suite.

`persistent_hash_multimap_tests.c` is the focused derived-collection executable and also exercises
the relation built from that core. It covers nonempty group normalization, checked pair accounting,
representative and snapshot retention, receiver-policy multimap algebra, inverse relation lookup,
failure-atomic two-index edits, and structural validation.

`persistent_derived_structures_tests.c` covers strict map-patch apply/invert/compose behavior,
directed-graph forward/reverse adjacency and incident-edge cleanup, indexed-map secondary-group
movement, retained snapshots, callback ownership, and independent cross-index validation.

`patricia_tests.c` is the companion dependency-free executable for the explicit-width Patricia
maps and sets. The build script compiles it with `src/patricia.c` into
`build/<Configuration>/patricia_tests.exe` and runs it after the hash-bag suite.

`merkle_search_tree_tests.c` is the focused executable for the ordered content-addressed map and
its complete persistence tier. It compiles with `src/merkle_search_tree.c` into
`build/<Configuration>/merkle_search_tree_tests.exe`, links CNG through `bcrypt.lib`, and runs after
the core HAMT, hash-bag, and Patricia suites.

Each runner keeps a static table of named test cases, prints `[PASS]` after each successful case,
and exits on the first failed check with file, line, and expression diagnostics. A successful run
ends with `<N> test(s) passed`.

## Test Cases

The persistent-bimap executable covers independent callback policies and contexts, strict key/value
conflicts, representatives, non-displacing replacement, symmetric nullable removal, clear,
two-root inversion, a 2,000-operation model, validation, and allocation-failure atomicity. It
prints `persistent bimap tests passed` on success.

The core HAMT executable registers these cases:

- `empty map has no entries`
- `set item adds replaces and preserves old versions`
- `add and try_add reject duplicates`
- `factories select once and preserve representatives`
- `factories cover collision bitmap and retained outputs`
- `factory failures leave sources and outputs unchanged`
- `remove and try_remove delete present keys`
- `set_many and clear preserve contracts`
- `create_range last wins and retains first equivalent key`
- `equal hash collision bucket preserves every key`
- `topology comparator rejects different collision keys`
- `deep shared hash prefixes lookup and remove correctly`
- `depth seven iterator traversal`
- `allocation failures unwind node_set and merge`
- `collision bucket splits and hash mismatch probes miss`
- `collision bucket equal value keeps root and key object`
- `structure root shape and sharing`
- `CHAMP independent histories and typed diff`
- `CHAMP collision runs compare and diff semantically`
- `CHAMP equality and diff prune shared descendants`
- `iterator copy advances independently`
- `random history matches model and preserves snapshots`
- `scripted collision snapshot story`
- `random history with colliding hashes matches model`
- `set add remove contains and persistence`
- `set custom comparer retains first item`
- `set algebra matches model`
- `set symmetric_except treats duplicates as one item`
- `concurrent retained snapshot reads`
- `map transient lifecycle reads and snapshot isolation`
- `set transient lifecycle representatives and clear`
- `map transient deterministic model history`
- `transient allocation failures are atomic`
- `transient retain failures are atomic and retryable`
- `set transient relations preserve policy and lifecycle`
- `set transient relation failures preserve output`

The CHAMP structure cases validate every stored hash prefix against its bitmap slot, including all
four reachable fragments at shift 30, and compare equal-hash collision key sets independently of
insertion order. The shared-descendant case restores one edited value through a distinct root, then
uses hash and equality callback counts to prove map equality and diff align bitmap slots directly
and prune pointer-identical off-path subtries.

The transient cases treat the C API as an explicit one-way edit-session lifecycle rather than an
in-place-performance claim. They cover policy and stored-representative preservation, clean root
identity, source isolation, active reads and iteration, changed/no-op iterator epochs, explicit
clone alias consumption, clear, publication retry, a deterministic model history, and allocation /
retaining-callback failure atomicity across map and set operations. Set-relation coverage exercises
all six predicates over duplicate-heavy arrays and cross-policy persistent-set operands, then sweeps
both allocation-bearing paths to prove boolean-output atomicity.

The hash-bag executable registers:

- `construction queries representatives and one-descent add`
- `copy-count validation overflow and root-sharing no-ops`
- `remove clear and aliasing preserve versions`
- `expanded distinct entry iterators and copy independence`
- `same-policy algebra and receiver representatives`
- `foreign-policy eager normalization and collapse overflow`
- `validation and allocation failures leave outputs and owners unchanged`
- `algebra allocation failure sweeps are atomic`
- `deterministic model history and retained snapshots`

These cases distinguish `d7_hamt_bag_distinct_count` from the checked expanded total, prove positive additions
select their multiplicity through one `add_or_update` descent, and cover maximum/minimum/saturated-
difference/checked-sum algebra. The cross-policy fixtures deliberately collapse exact classes under
the receiver's broader equality policy; a count-allocation failpoint proves that normalization runs
before even a logically unchanged intersection. Allocation sweeps and targeted retaining-callback
failures assert byte-for-byte output nonpublication, unchanged source roots, canonical survivors,
and balanced item ownership.

The Merkle executable registers:

- `digest and built-in codecs`
- `MST2 single-entry golden wire`
- `policy validation and typed compatibility`
- `history independence and structure`
- `persistence range diff and sharing`
- `allocation failure atomicity`
- `callback failure atomicity`
- `equivalent keys retain first representative`
- `streaming visitor failures`
- `randomized model and snapshots`
- `concurrent retained snapshot reads`
- `verified persistence store and iterative sync`
- `MSP2 proofs and budget preflight`
- `all budgets import closure and preflight`
- `MSP2 structural tamper and bomb precedence`
- `three-way merge results and policy identity`
- `merge present null is not deletion`
- `concurrent memory store puts and owned snapshots`
- `memory store never calls user code under lock`
- `persistence allocation failure sweeps`
- `persistence callback failures leave no result`

## Build And Run

From `src/C/Hamt`, build and run the Debug test executable:

```powershell
.\build.ps1 -RunTests
```

Run the built executable directly when changing runner diagnostics or investigating a local failure:

```powershell
.\build\Debug\hamt_tests.exe
```

Run the bag executable directly when investigating multiplicity, normalization, or bag ownership:

```powershell
.\build\Debug\persistent_hash_bag_tests.exe
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

Run the Merkle executable directly when investigating canonical block bytes, verified persistence,
proofs, sync, merge, codec/store failures, concurrency, history independence, or allocator unwind:

```powershell
.\build\Debug\merkle_search_tree_tests.exe
```
