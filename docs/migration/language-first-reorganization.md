# Language-First Reorganization

- Created (UTC): 2026-07-02T21:30:51Z
- Repository HEAD: e442ec12005f19bb559fe9820b835a7d134f2816
- Audience: Maintainers and AI agents translating pre-reorganization paths
- Scope: Historical record of the move to `src/<Language>/<DataStructure>`

This record covers the 2026-07-02 repository reorganization that grouped workspaces by programming
language first, then by data-structure family. It is historical evidence and a path-translation aid,
not current layout guidance. Use the [workspace map](../reference/workspace-map.md), the
[source index](../../src/README.md), and the [build and validation guide](../guides/build-and-validation.md)
for current commands and entry points.

## Reorganization Commit

The structural move landed in commit `9bf68f498405e2dce44cb08fad08ea2bbe97d97c`, dated
2026-07-02T12:22:01-07:00, with the subject:

```text
Reorganize workspaces under language source roots
```

The commit moved the C, C++, and C# workspaces under `src/` while preserving each data-structure
workspace below its language root. It also updated repository guidance, documentation links, and
validation commands for the new layout. Git detected the change primarily as renames: 282 files
changed, with 351 insertions and 349 deletions.

## Path Translation

| Before reorganization | Current path | Notes |
| --- | --- | --- |
| `C/FingerTree` | [`src/C/FingerTree`](../../src/C/FingerTree/README.md) | C11 FingerTree port, CMake/CTest build |
| `HamtC` | [`src/C/Hamt`](../../src/C/Hamt/README.md) | C17 HAMT port, PowerShell build script |
| `Cpp/FingerTree` | [`src/Cpp/FingerTree`](../../src/Cpp/FingerTree/README.md) | C++23 FingerTree port, CMake/CTest build |
| `HamtCpp` | [`src/Cpp/Hamt`](../../src/Cpp/Hamt/README.md) | C++20 HAMT port, PowerShell build script |
| `FingerTree` | [`src/CSharp/FingerTree`](../../src/CSharp/docs/FingerTree/overview.md) | .NET FingerTree workspace, canonical semantic source |
| `Hamt` | [`src/CSharp/Hamt`](../../src/CSharp/docs/Hamt/overview.md) | .NET HAMT workspace, canonical semantic source |

Historical references to these old paths are valid only in migration records, review reports, and
commit-history discussions. Active documentation should use the current paths.

## Follow-up Documentation Shape

The language-root move was followed by documentation organization commits that made the layout
directly discoverable:

- `333ae7f0e811f642228b538e235a420b1dc6405f` organized repository-level docs into `docs/guides`
  and `docs/reference`, added the workspace map, and added repository-wide build and validation guidance.
- `36c44223de468e83a0b7265b11837c73bc053f75` added the `docs/migration` index so extraction and
  path-history records stay separate from current-state guidance.
- `e375d5f1b031745ac97cf2ae81e0d91cf03ec22e` added `src/README.md` and language-level source
  indexes for `src/C`, `src/Cpp`, and `src/CSharp`.

The current documentation layers are:

| Layer | Owns |
| --- | --- |
| Root `README.md` | High-level repository orientation, workspace summaries, and quick commands |
| `src/README.md` plus language indexes | Language-first browsing and source placement rules |
| `docs/reference/workspace-map.md` | Durable layout, workspace roles, port lineage, and documentation placement |
| `docs/reference/data-structure-catalog.md` | Cross-language public data-structure surfaces |
| `docs/guides/build-and-validation.md` | Repository-wide validation commands |
| `docs/migration` | Extraction and path-history provenance, including this document |

## Maintenance Notes

- Keep old paths in this document when they are part of the historical record.
- Use current `src/C`, `src/Cpp`, and `src/CSharp` paths everywhere else unless explicitly discussing
  pre-reorganization history.
- When adding a long-lived workspace, update the root README, `src/README.md`, the affected language
  index, the workspace map, the data-structure catalog, and validation/test maps as applicable.
