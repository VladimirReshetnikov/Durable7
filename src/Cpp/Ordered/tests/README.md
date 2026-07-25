# C++ Ordered Test Map

- Created (UTC): 2026-07-15T09:20:15Z
- Repository HEAD: 88164edb086096800b2fb32eeaa7e7a1e556e183
- Updated (UTC): 2026-07-16T22:52:15Z
- Audience: Maintainers and reviewers of the neutral C++ ordered collections
- Scope: ordered-set, ordered-map, and ordered-multimap native tests

The independent executable test suite uses
or source. Expected results come from direct ordered unique-list models and the neutral Ordered API
contract.

Coverage includes:

- ordered-multimap independent policies, first key/value representatives, grouped iteration,
  duplicate no-ops, pair/group removal, retained snapshots, counts, and nested validation;

- ordered-map first-key/first-position/last-value construction, strict and conditional insertion,
  value-only root-sharing replacement, explicit movement, removal, ranges, reverse, stable sort,
  custom value equality, retained snapshots, and forced sparse-label relabeling;

- default, custom-policy, and equal-hash-collision construction, duplicate collapse,
  first-representative retention, lookup, position, endpoints, forward iteration, and invalid
  positions;
- no-op addition identity, explicit final-position movement, all removal forms, comparer-preserving
  clear, ranges, reversal, stable one-shot sort, and deterministic relabel stress;
- receiver-policy union, intersection, difference, symmetric difference, all six relations,
  cross-policy argument normalization, and late-failure eager-normalization checks;
- injected hash, relabel-rebuild, and ordering failures with source-version verification;
- the `std::copyable` template boundary and iterator lifetime after facade destruction;
- 900 deterministic generated edits checked after every operation against an ordered unique-list
  model, plus retained-snapshot rechecks; and
- concurrent readers repeatedly exercising traversal, endpoints, and positional access on one
  published immutable snapshot.

Run it through the serialized commands in the [validation guide](../docs/validation.md).
