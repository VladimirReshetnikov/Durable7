# Haskell HAMT Tests

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents validating the Haskell HAMT port
- Scope: `tools-data-structures-hamt` test executable

Run from `src/Haskell`:

```powershell
.\test.ps1 -Workspace Hamt
```

The dependency-free executable covers map last-wins construction, duplicate rejection, explicit
collision buckets, custom hash/equality policy behavior, original-key recovery, set algebra, and
`forkIO` concurrent readers over shared immutable snapshots. It also locks in collision-to-leaf
canonicalization, receiver-policy set relations, one-pass adjustment behavior, strict value mapping,
independent-history CHAMP equality, typed diff classification, and 100,000-entry bulk construction.
The Patricia tier covers explicit 32/64-bit signed extrema, ascending enumeration, a 10,000-step
map history, right-biased map algebra, left/right/key-aware combining algebra, cached-subtree
cardinality validation, and set union/intersection/difference.
