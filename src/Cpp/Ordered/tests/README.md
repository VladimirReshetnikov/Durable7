# C++ Ordered Test Map

- Created (UTC): 2026-07-15T09:20:15Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Maintainers and reviewers of the neutral C++ ordered-set port
- Scope: `persistent_ordered_set_tests.cpp`

The independent executable test suite uses no Tungsten production code, test oracle, linked target,
or source. Expected results come from direct ordered unique-list models and the neutral Ordered API
contract.

Coverage includes:

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
