# C Ordered Validation

- Status: Current validation guide
- Created (UTC): 2026-07-15T09:00:00Z
- Repository HEAD: 2d75a79feb424f4476ec32c2d6e4f19263441bf3
- Audience: Maintainers and reviewers validating `src/C/Ordered`
- Scope: Warning policy, serialized commands, tests, and dependency audit

## Commands

Run the workspace alone from `src/C`:

```powershell
.\build.ps1 -Workspace Ordered -RunTests
.\build.ps1 -Workspace Ordered -Configuration Release -RunTests
```

The wrapper configures Ninja through the checked-in presets, builds with `--parallel 1`, and runs
CTest with `--parallel 1`. MSVC uses C17, `/permissive-`, `/W4`, and `/WX`; GCC/Clang configurations
use `-Wall -Wextra -Wpedantic -Werror`. FingerTree samples, tests, and benchmarks are disabled in the
composite build. No benchmark belongs to this gate.

## Coverage

The ordered-set executable covers representative retention, duplicate structural no-ops, positional
edits and final-index movement, missing and boundary statuses, removal, range/take/drop, reversal,
stable sorting, every algebra/relation family, receiver-policy normalization across differently
configured sets, ownership balance, repeated gap exhaustion/relabel, retained-version persistence,
two-way invariant validation, and a deterministic 1,000-command independent ordered-list model.

The ordered-map executable covers append/prepend/positional insertion, explicit order, payload
replacement and retained snapshots, order-root sharing for replacement, value-root sharing for
movement, payload-based stable sort, range extraction, conditional duplicate insertion, removal,
and two-way cross-index validation. Focused ASan/UBSan validation additionally checks its shared
policy-context and type-erased ownership paths.

## Dependency audit

Ordered may include only its own header plus the public HAMT and FingerTree headers. Its CMake target
links only the corresponding general targets. A repository search for `Tungsten`, `tungsten`, or a
Tungsten target under `src/C/Ordered` must return only explanatory documentation; production source,
tests, and manifests must contain no such dependency or oracle.
