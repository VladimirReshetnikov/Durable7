# Migration

- Created (UTC): 2026-07-02T20:54:23Z
- Repository HEAD: b9e6f523b27ae4769a3921f9a5d40df52fdc1da8
- Audience: Maintainers and AI agents auditing repository extraction and path-history provenance
- Scope: Migration records under `docs/migration`

This directory contains historical records for the extraction of the standalone DataStructures
repository from `C:\Tools0\src\DataStructures` / `VladimirReshetnikov/Tools`. These documents are
evidence, not active build or layout guidance. Use the root [README](../../README.md), the
[workspace map](../reference/workspace-map.md), and the
[build and validation guide](../guides/build-and-validation.md) for current paths and commands.

## Records

- [Extraction provenance](extraction-provenance.md) records the source repository, extracted path,
  filter command, standalone-repack validation, and post-extraction validation notes.
- [Filter-repo commit map](filter-repo-commit-map.tsv) preserves the old-to-new commit mapping
  produced by the history rewrite.

## Maintenance Rules

- Keep historical paths and commands when they are part of the record.
- Add current-path notes only when they help readers translate an old command or path into today's
  language-first layout.
- Do not rewrite `Repository HEAD` metadata in historical records unless the document is
  substantively revised; add an `Updated (UTC)` line or a dated note instead.
- Keep generated or bulky migration artifacts out of active workspace docs unless they are needed
  for ordinary maintenance.
