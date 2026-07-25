# DataStructures Extraction Provenance

- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: Maintainers auditing the split from Tools
- Scope: Local extraction of `src/DataStructures` into `C:\DataStructures`

## Source

- Source repository: `C:\Tools0`
- Source upstream: the former Tools monorepo
- Source HEAD before extraction: `944498b9adcccaf0993663c53f963aac006429e9`
- Extracted path: `src/DataStructures/`
- Destination repository: `C:\DataStructures`

## Method

The extraction used a shared local clone to avoid copying the full Tools object database up front:

```powershell
git clone --shared --no-checkout C:\Tools0 C:\DataStructures
cd C:\DataStructures
git remote remove origin
python -m git_filter_repo --force --path src/DataStructures/ --path-rename src/DataStructures/:
git repack -a -d --no-local
Remove-Item .git\objects\info\alternates
git fsck --full
```

The filtered history contains 70 commits. Commit hashes necessarily changed because path names and parent links were rewritten. Author names, author dates, commit messages, file contents, and path-local diffs were preserved as precisely as the path filter allows.

The filtered HEAD immediately after history extraction was:

```text
d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
```

## Follow-up bootstrap

After filtering, the standalone repository received a normal bootstrap commit adding:

- root hygiene files copied from Tools: `.editorconfig`, `.gitattributes`, `.gitignore`, and `LICENSE`;
- standalone root guidance: `README.md`, `AGENTS.md`, `CLAUDE.md`, and `PREFERENCES.md`;
- compact repository-level docs under `docs/`;
- a retained-commit map at `docs/migration/filter-repo-commit-map.tsv`;
- path-reference normalization from `src/DataStructures/FingerTree` to `FingerTree`.

The large Tools documentation standards, `TECHNICAL_DOCUMENTATION_STANDARD.md` and `XML_DOCUMENTATION_STANDARD.md`, were intentionally excluded.

## Validation

The extraction was made standalone by repacking with `--no-local`, removing `.git/objects/info/alternates`, and running:

```powershell
git fsck --full
```

Build and test validation was run after standalone guidance and path normalization:

```powershell
cd C:\DataStructures\FingerTree
dotnet test .\DataStructures.sln
```

Result: passed 346 tests, 0 failed, 0 skipped.

After the 2026-07 language-first repository reorganization and the subsequent Durable7 rebrand,
which renamed the solution from `DataStructures.sln` to `Durable7.sln`, the equivalent validation
command is:

```powershell
cd C:\DataStructures\src\CSharp
dotnet test .\Durable7.sln
```
