# Ordered Test Suite

- Created (UTC): 2026-07-15T09:12:49Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: maintainers validating the Haskell persistent ordered set
- Scope: executable coverage in `test/Main.hs`

The `ordered-test` executable covers first-representative and runtime-policy behavior, positional
insertion and movement, sparse-label exhaustion, removals and ranges, stable order transforms,
receiver-policy algebra and relations, logical no-op root sharing, callback-failure isolation,
structural validation, concurrent immutable reads, and a 1,000-command list-model history.

Run it through the repository wrapper so Cabal remains limited to one build job:

```powershell
..\..\test.ps1 -Workspace Ordered
```

No benchmark is part of this test suite or its routine validation gate.
