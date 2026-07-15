# Haskell Persistent Ordered Set

- Created (UTC): 2026-07-15T09:12:49Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: users and maintainers of the neutral Haskell ordered-set port
- Scope: ownership, representation, public behavior, and validation of `tools-data-structures-ordered`

This package is the independently owned Haskell port of the repository's neutral insertion-ordered
set. It composes the public CHAMP map from `tools-data-structures-hamt` with the public persistent
deque from `tools-data-structures-fingertree`; it has no dependency on the application-specific
Tungsten package and does not use Tungsten behavior as an oracle.

`Data.Structures.Ordered.PersistentOrderedSet` stores one ordered `Entry` per comparer equivalence
class and one CHAMP entry from that representative to its private signed 64-bit order label. Labels
are sparsely allocated and deterministically rebuilt when a local gap is exhausted. The spacing and
relabel cadence are private implementation details.

The observable contract matches the C# semantic baseline where Haskell has corresponding language
shapes:

- construction keeps the first representative and its first-occurrence position;
- `add`, `addFirst`, and `insertAt` never replace or implicitly move an existing class;
- `moveToFirst`, `moveToLast`, and `moveTo` retain the stored representative;
- invalid positions, absent movement, and empty endpoint removal use `Maybe` instead of exceptions;
- range extraction, reversal, and stable one-shot sorting retain the membership policy;
- algebra and relations eagerly normalize their list argument under the receiver's policy, retaining
  the first argument representative of each collapsed class; and
- logical no-ops reuse the receiver's CHAMP root, observable through `sharesIndexWith`.

The dual-index invariant requires equal cardinalities, strictly ascending labels, one order entry per
policy equivalence class, matching labels on both sides, and agreement on the stored representative.
`validStructure` checks this contract together with the CHAMP substrate's canonical-shape validator.
Values are immutable and therefore safe for concurrent evaluation when caller-supplied hash/equality
callbacks are themselves safe.

Run the focused, single-job gate from `src/Haskell`:

```powershell
.\test.ps1 -Workspace Ordered
```

The gate builds with `-Wall -Wcompat`, runs examples plus deterministic model, collision/relabel,
policy, failure-atomicity, sharing, and concurrent-read tests, and is not a benchmark.
