# C Ordered Tests

- Status: Current test map
- Created (UTC): 2026-07-15T09:00:00Z
- Repository HEAD: 2d75a79feb424f4476ec32c2d6e4f19263441bf3
- Audience: Maintainers and reviewers of the C ordered-set port
- Scope: `ordered_set_tests.c`

`ordered_c_tests` is a deterministic native executable registered as `ordered_c.core`. It uses
plain value arrays and a small comparer-aware ordered-list model; it does not call or include any
application collection as an oracle.

The scenario groups cover:

- first representative and first position under collision/equivalence-heavy policies;
- add/prepend/insert no-ops and explicit final-index movement;
- removal, ranges, take/drop, reversal, and stable comparison ties;
- receiver-ordered algebra and all six relations after eager receiver-policy normalization;
- clone persistence and foundation-root sharing for semantic no-ops;
- private sparse-label exhaustion followed by deterministic relabel;
- custom item copy/destroy balance across shared snapshots; and
- 1,000 generated add, insert, move, remove, reverse, and slice commands checked after every step.

The headless process helper suppresses interactive crash UI. CTest is fixed to one job by the
workspace preset.
