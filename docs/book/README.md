# The Durable7 Field Guide

- Created (UTC): 2026-07-26T00:00:00Z
- Repository HEAD: e7fcb3517991ee2c0915e28e19916aa2ccfd21d7
- Audience: Anyone choosing, using, or porting a Durable7 collection
- Scope: A single long-form document covering every data structure in the repository
- Format: LaTeX source plus the built PDF, both committed

## The document

[**Persistent Data Structures: The Durable7 Field Guide**](durable7-data-structures.pdf)
— 133 pages covering every collection family in the repository: what each one is,
how it is represented, why it exists, where its guarantees stop, and how it is
spelled in each of the nine ports.

| File | Role |
| --- | --- |
| [`durable7-data-structures.tex`](durable7-data-structures.tex) | The complete source. One self-contained file; no external assets. |
| [`durable7-data-structures.pdf`](durable7-data-structures.pdf) | The built document. Regenerate it whenever the source changes. |

## What it covers

- **Part I** — persistence, path copying, the version DAG, the branching-amortization
  hazard, and the shared cross-port contract (policies, representatives, no-op
  identity, failure atomicity, ordering).
- **Part II** — CHAMP and the composition-first families built on it, Patricia
  integer tries, the Ctrie, and the builder/session/frozen lifecycle.
- **Part III** — finger trees, the measure framework, RRB vectors, ropes and text,
  the range-update sequence, the reversible deque, and DABA Lite.
- **Part IV** — sorted collections, canonical zip-zip sets, the insertion-ordered
  family, the three priority structures, and interval trees and maps.
- **Part V** — Merkle search trees, blocks, packs, proofs, synchronization, and
  three-way merge.
- **Part VI** — cursors.
- **Reference** — a complexity table for the whole library, a nine-language name
  index, the recorded rejections and postponements, further reading, and a subject
  index.

## Authority

This guide is a companion to, not a replacement for, the workspace API
specifications and public headers. Those remain the normative source for
contracts, complexity, allocation behavior, and validation evidence. Where this
guide and a workspace specification disagree, the specification wins and the
guide has a bug.

See also the [data-structure catalog](../reference/data-structure-catalog.md) for
the per-language entry-point matrix and the
[semantic contracts reference](../reference/semantic-contracts.md) for the concise
normative obligations.

## Building

Requires a TeX distribution with `pdflatex`, `makeindex`, and the packages
`libertinus`, `inconsolata`, `tcolorbox`, `pgf`/`tikz`, `titlesec`, `booktabs`,
`enumitem`, `microtype`, and `listings`. All of these ship with a full MiKTeX or
TeX Live installation.

```bash
cd docs/book && latexmk -pdf durable7-data-structures.tex
```

Or run the passes explicitly, which is what the committed PDF was produced with:

```bash
pdflatex durable7-data-structures.tex && makeindex -q durable7-data-structures.idx && pdflatex durable7-data-structures.tex && pdflatex durable7-data-structures.tex
```

The index requires the `makeindex` pass; without it the Index chapter is empty.
Three `pdflatex` passes are needed for the table of contents, cross-references,
and index page numbers to settle. Commit the regenerated PDF alongside the source
so that readers do not need a TeX installation.

Build byproducts (`.aux`, `.toc`, `.idx`, `.ind`, `.ilg`, `.log`, `.out`,
`.fls`, `.fdb_latexmk`) are not tracked; delete them after a build.
