# Measured rope cursor C2 shipment decision

- Created (UTC): 2026-07-14T00:22:18Z
- Repository HEAD: 3ef85b4fe10705f7ac3fab50b3b463237fe72ca9
- Status: Accepted and shipped in C#
- Audience: Maintainers evaluating the Axis 2 measured/text cursor
- Scope: C2 representation, contracts, validation, and locked benchmark gates

## Decision

Ship `MeasuredRopeCursor<T, TMeasure, TMeasureOps>` and the corresponding
`MeasuredRope.GetCursor` / `TryGetCursorByMeasure` surface. The cursor mirrors the public positional
`RopeCursor<T>` edit vocabulary, adds O(1) ordered `MeasureBefore` and `MeasureAfter`, and supports
absolute measure seek through both delegate and closure-free struct predicates. The existing
`MeasuredRope<char, int, NewlineMeasure>` remains the text representation; C2 does not create a
second text-rope family.

C2 clears its separately locked local-edit, measure-seek, positional-seek, line/column, callback,
allocation, and freshly dirty query gates. C3 sample adoption may therefore proceed. This decision
does not authorize C4 adapters for RRB, deque, raw finger-tree, reversible-deque, or Tungsten
families, and it does not create a sibling-language parity obligation.

## Selected representation

The selected representation extends C1's readonly-struct zipper-as-version design:

- a 16-element active focus and sub-256-element carries keep local copying bounded;
- the underlying measured finger tree now has an allocation-free two-sided locate that returns the
  ordered measure before the selected chunk and after it without splitting the tree;
- an absolute measure seek initially retains the immutable source, selected ordinary chunk, exact
  gap, and ordered before/after measures as a deferred focus; peeks and clean snapshots do not force
  zipper construction;
- one-shot `TryGetCursorByMeasure` scans at most one 2,048-element ordinary chunk without retaining
  a full element-measure array, preserving the source-factory latency gate;
- a seek on an existing cursor lineage prepares a selected fragment once and shares that successful
  preparation across descendant edit versions. Element measures are retained, while directional
  prefix/suffix tables are published lazily with compare/exchange. Cached seeks stop at the boundary
  and obtain the ordered remainder from the suffix table in O(1);
- failed preparation is not installed. Racing preparation may duplicate bounded work but cannot
  publish partial arrays; snapshot publication retains C1's winner-returning compare/exchange rule;
- movement or editing materializes a deferred focus only when required. A clean snapshot remains the
  exact source reference, while a dirty first snapshot packs prepared buffers and joins the tree
  without invoking the element `Measure` callback.

No inverse, commutativity, element equality, or default-value identity is assumed. All aggregate
composition is in source order.

## Locked policy

The constants in `Axis2BenchmarkPolicy` were fixed before collecting C2 results:

| Lane | Acceptance threshold |
| --- | --- |
| 64K measured-text local edit, 256 replacements, locality 8, snapshot cadence 16 | at least `max(noise, 10%)` time and allocation improvement over indexed `MeasuredRope` |
| Positional seek | mean ratio <= 1.25; allocation ratio <= 1.10 |
| Source/prepared absolute measure seek | mean ratio <= 2.00; allocation <= 16 KiB |
| Measure callbacks per first selected fragment | <= 2,064 (`2,048 + 16`) |
| Line/column | mean ratio <= 1.25; allocation <= 64 B |
| Freshly dirty queries | mean ratio <= 1.25; allocation ratio <= 1.10 |

Both newline distributions are predeclared: sparse means one newline per 256 UTF-16 code units;
dense means one per 8. Ratios below are candidate mean divided by the paired baseline mean. The
primary struct-measure gate uses a normal out-of-process BenchmarkDotNet job; secondary paired
guardrails use the documented serial in-process toolchain. Every run disables tiered compilation,
pins affinity mask 1, and builds with one MSBuild node and shared compilation disabled.

## Evidence

### Local editing

The cursor uses 64K characters, 256 replacements, locality eight, and snapshot cadence sixteen. To
avoid overstating the win, the comparison below uses the fastest completed indexed-control repeat.

| Newline density | Indexed baseline mean | Cursor mean | Mean improvement | Baseline allocation | Cursor allocation | Allocation improvement |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sparse | 1.084 ms | 275.7 us | 74.6% | 1.56 MB | 287.24 KB | 82.0% |
| Dense | 1.139 ms | 265.9 us | 76.7% | 1.56 MB | 287.24 KB | 82.0% |

The cursor confidence intervals were 263.440-287.862 us (sparse) and 255.512-276.236 us (dense),
so even their upper bounds remain far below the fastest control means. The result decisively clears
the 10% practical threshold.

### Clean query guardrails

| Category | Density | Baseline | Source cursor | Prepared cursor | Allocation |
| --- | --- | ---: | ---: | ---: | ---: |
| Struct measure seek | Sparse | 2.572 us | 4.623 us (1.80x) | 3.630 us (1.41x) | 664 B / 0 B |
| Struct measure seek | Dense | 3.208 us | 5.250 us (1.64x) | 3.363 us (1.05x) | 664 B / 0 B |
| Delegate measure seek | Sparse | 7.247 us | 8.389 us (1.16x) | 10.091 us (1.39x) | 160 B / 0 B |
| Delegate measure seek | Dense | 7.818 us | 8.591 us (1.10x) | 8.319 us (1.06x) | 160 B / 0 B |
| Positional seek | Sparse | 9.192 us | - | 9.349 us (1.02x) | 0.99x baseline |
| Positional seek | Dense | 9.740 us | - | 11.772 us (1.21x) | 0.99x baseline |
| Line/column | Sparse | 3.454 us | - | 3.152 us (0.91x) | 0 B |
| Line/column | Dense | 3.394 us | - | 2.738 us (0.81x) | 0 B |

The final source and prepared struct-measure artifact is
`axis2-c2-query-final-splitpolicy-struct-outprocess-notier-gate`. Its machine-readable callback
line reports 2,048 `Measure` calls and 2,067 `Combine` calls for each first source/prepared seek;
snapshot publication reports zero element-measure calls. The delegate, position, and line/column
table comes from `axis2-c2-query-clean-guardrails-inprocess-notier-gate`.

### Freshly dirty queries

Dirty lanes apply the same fresh insertion before both sides. The baseline explicitly snapshots and
then queries; the candidate queries through the dirty cursor convenience surface.

| Category | Sparse mean ratio | Dense mean ratio | Allocation ratio |
| --- | ---: | ---: | ---: |
| Line/column | 0.71x | 0.94x | 1.00x |
| Delegate measure seek | 1.17x | 1.23x | 1.00x |
| Struct measure seek, final shared-suffix cache | 1.14x | 1.09x | 1.00x |
| Positional seek | 1.02x | 0.94x | 0.99x |

The final struct lane is recorded by
`axis2-c2-query-dirty-struct-suffix-inprocess-notier-gate`; the other lanes are recorded by
`axis2-c2-query-dirty-guardrails-inprocess-notier-gate`.

## Validation

The Release test project passes 630/630 tests with one MSBuild node and one test-process CPU slot.
C2-specific coverage includes:

- public API shape, default-value rejection, bounds, neighbor failures, and identity-preserving
  no-ops;
- deterministic `List<T>` gap-model histories with retained ancestors and independent branches;
- noncommutative measures proving `Combine(MeasureBefore, MeasureAfter)` equals the total in order;
- true-at-empty, hit, miss, empty, chunk-boundary, delegate/struct, source/prepared, and dirty measure
  seek parity;
- exact fragment callback ceilings, same-fragment reuse, failed preparation, and racing preparation;
- clean and dirty snapshot identity, failed construction, winner-returning races, and zero element
  remeasurement during publication; and
- existing newline/text helper compatibility and UTF-16 line/column behavior.

The snapshot-race test uses dedicated long-running tasks so its deliberately blocked callback still
overlaps the winning candidate when the serial full-suite host has a saturated worker pool.

## C3 follow-on status

C3 subsequently updated the Editor and Tour to retain measured cursor versions and materialize
snapshots only at explicit display/commit boundaries. The samples use the benchmark cadence of
sixteen local edits and do not redefine snapshot-every-edit as the target workload. The
[C3 integration record](cursor-c3-sample-integration.md) owns the sample histories and transcript
evidence.
