# External reference material

- Created (UTC): 2026-06-14T23:22:54Z
- Repository HEAD: 23fccd385f094b749287ab7a435de0b9a57e6790
- Audience: Anyone studying or maintaining the FingerTree implementation
- Scope: Index for `src/DataStructures/FingerTree/docs/external`

> **Everything under this directory is external, pre-existing material that this project did *not* author.**
> It is bundled solely as convenient study and comparison references for the persistent finger-tree
> implementation. None of it is part of the library, its build, or its tests, and **none of it is covered by
> this repository's MIT-0 license** — each item retains its own copyright and license, noted below. The
> project's own documentation (the API specification, design notes, benchmarks, persistence/concurrency guide,
> and build script) lives one directory up, in `../`.

## Articles

- **Finger trees: a simple general-purpose data structure** — R. Hinze and R. Paterson, *Journal of
  Functional Programming* 16(2), 2006. The original measured-finger-tree paper (the source of the general
  measured tree, splitting, monoidal annotations, and ordered-sequence search). Bundled as
  [the published PDF](<Finger trees - a simple general-purpose data structure.pdf>) and, under the matching
  [directory](<Finger trees - a simple general-purpose data structure/>), a transcribed `.md`/`.tex` with
  figure images. © the authors / Cambridge University Press.
- **Finger Trees Explained Anew, and Slightly Simplified (Functional Pearl)** — K. Claessen, *Haskell
  Symposium*, 2020. The simplified re-explanation that the tuned deque follows (digits of one through three,
  nodes of two or three). Bundled as
  [`.md`](<Finger Trees Explained Anew, and Slightly Simplified.md>),
  [`.pdf`](<Finger Trees Explained Anew, and Slightly Simplified.pdf>), and
  [`.tex`](<Finger Trees Explained Anew, and Slightly Simplified.tex>). © the author / ACM.
- **Finger tree (Wikipedia)** — [`Finger tree (Wikipedia).wiki`](<Finger tree (Wikipedia).wiki>), the raw
  wikitext of the English Wikipedia article. © its contributors, licensed CC BY-SA.

## Code

- **Haskell `containers` 0.8** — [`containers-0.8/`](containers-0.8/), an upstream source snapshot of the
  Haskell `containers` library, kept as a production reference for `Data.Sequence`/`Data.FingerTree` strictness,
  splitting, indexing, and edge-case handling (it uses the original one-through-four digit representation rather
  than the simplified one-through-three). It carries its own [`LICENSE`](containers-0.8/LICENSE) (the Glasgow
  Haskell Compiler / BSD-style license, © the University of Glasgow and contributors).
