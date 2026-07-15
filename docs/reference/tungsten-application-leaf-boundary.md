# Tungsten Application-Leaf Dependency Boundary

- Status: Normative repository ownership and dependency policy
- Created (UTC): 2026-07-14T21:14:47Z
- Repository HEAD: faf53286375109fc598e40d5e6da7d1bff7e7415
- Audience: Maintainers, reviewers, porters, and AI agents designing or moving repository-owned data structures
- Scope: Dependency direction, semantic authority, reuse by independent fork, validation, and extraction rules for every Tungsten collection workspace

## Executive Rule

Tungsten collection workspaces are application-specific leaf consumers for the Tungsten project,
an alternative Wolfram Language interpreter. They are not foundations for repository-general data
structures.

Their contracts may change when Wolfram-kernel behavior is newly discovered, reinterpreted, or
changed. The workspaces may eventually move from this repository into the Tungsten repository.
Dependency direction must therefore remain one-way:

At the architectural layer, general mechanisms may flow into Tungsten. A Tungsten mechanism may
flow outward only by an independent fork, never by making general code depend on the application
implementation. In ordinary project-reference notation, where the arrow points from a consumer to
its dependency, the allowed graph is:

```text
Tools.DataStructures.Tungsten
├── Tools.DataStructures.Hamt
└── Tools.DataStructures.FingerTree

Tools.DataStructures.Hamt         (must not reference Tungsten)
Tools.DataStructures.FingerTree   (must not reference Tungsten)
Any other general library         (must not reference Tungsten)
```

No general-purpose data structure may depend on a Tungsten assembly, package, crate, module, header,
namespace, type, internal API, source file, or behavioral contract. Likewise, no library, sample,
benchmark, or test outside the Tungsten family may take such a dependency. If a mechanism found in
Tungsten deserves general use, fork it into an independently owned implementation with its own API,
invariants, tests, documentation, dependency graph, and evolution policy.

## Why The Boundary Exists

### Tungsten has an application authority outside this repository

`PersistentList` and `PersistentAssociation` exist to represent the operation vocabulary and
ordering behavior of Tungsten Language `List` and `Association`. Their semantic authority is
kernel observation and the needs of the Tungsten interpreter, not the preferences normally used to
design a general .NET, C++, Rust, or other host-language collection.

A new kernel observation can legitimately require Tungsten to change duplicate handling,
representative retention, ordering, absence behavior, positional interpretation, or another
contract. Such a change must not silently alter a general collection whose users chose it for a
different reason.

### Physical relocation must stay cheap

The Tungsten workspaces may move to the interpreter's repository. A clean leaf boundary means that
relocation removes a consumer and its sibling ports; it does not require moving, cloning, or
rewriting foundational HAMT/FingerTree libraries or unrelated general collections.

### Generic syntax does not imply general ownership

A type can be parameterized by `T`, `TKey`, or `TValue` and still be application-specific. Generic
host-language types describe representation flexibility. They do not erase the provenance or
change authority of an operation surface deliberately shaped around kernel behavior.

### Algorithmic provenance is not semantic authority

Tungsten contains attractive mechanics: stable sparse stamps, a hashed index paired with a
persistent ordered sequence, relabeling after gap exhaustion, stable rebuilds, and explicit
branched-history caveats. Those ideas can inform a general implementation. They do not make
Tungsten's API names, edge cases, exact constants, tests, or future changes authoritative for it.

## Terminology

| Term | Meaning in this policy |
| --- | --- |
| Application-specific leaf | A workspace that consumes general libraries but is neither a code dependency nor a semantic baseline for general structures. |
| General library | A repository-owned collection or core whose contract is selected for reusable data-structure value rather than Tungsten-kernel fidelity. |
| Sibling Tungsten port | A C, C++, Haskell, Kotlin, Rust, TypeScript, or Python implementation whose purpose is to reproduce the C# Tungsten family in its language-local ownership model. |
| Independent fork | A separately named and owned implementation that may reuse an algorithmic idea or copied logic but has no code, type, test-oracle, or evolution dependency on Tungsten. |
| Provenance | A citation explaining where a design idea, adversarial case, or implementation lesson came from. |
| Semantic baseline | The contract against which an API or test suite decides what behavior is correct. Tungsten may be the baseline only for sibling Tungsten ports. |
| Live oracle | A test or generation process that derives expected behavior by calling Tungsten code or automatically copying its outputs. General collections may not use one. |

## Normative Dependency Rules

### Allowed dependencies

Tungsten workspaces may:

- reference general HAMT, FingerTree, numerics, or future repository-general packages;
- use those packages' public APIs and documented contracts;
- port Tungsten behavior among the sibling Tungsten workspaces;
- cite general algorithms, papers, and repository catalogs;
- change their own behavior when kernel evidence or interpreter design requires it; and
- move out of this repository while continuing to consume separately packaged general libraries.

Repository-general work may:

- cite Tungsten as historical design provenance or a consumer case study;
- translate an adversarial scenario into an independently specified test;
- fork a useful mechanism into new source owned entirely by a general workspace;
- preserve, change, or relax a Tungsten guarantee after explicitly deciding the general contract;
  and
- document that two independent implementations happen to use related algorithms.

### Forbidden dependencies

A non-Tungsten or general workspace must not:

- add a project, package, crate, module, header, or link dependency on Tungsten;
- import a Tungsten namespace or bind to a Tungsten type as a compiled production-code symbol;
- wrap, subclass, delegate to, or expose a Tungsten type as the representation of a general type;
- link or share a Tungsten source file, generated source artifact, or internal helper;
- rely on privileged access granted specifically to Tungsten or require such a grant as part of a
  general collection's implementation;
- use Tungsten tests, snapshots, or runtime output as a live behavioral oracle;
- state that a general API “inherits Association semantics” or “matches Tungsten” instead of
  specifying its own contract;
- inherit complexity claims merely because Tungsten currently uses a similar representation; or
- automatically propagate a kernel-driven Tungsten change into a general implementation.

Conversely, a general provider must not grant Tungsten privileged internal access through
`InternalsVisibleTo`, C++ friendship, package-internal visibility, or an equivalent mechanism, and
Tungsten must not require such a grant.

### References that are not dependencies

The following are permitted when their purpose is explicit:

| Reference | Why it is allowed |
| --- | --- |
| Root solution/workspace entries that aggregate Tungsten for building | The aggregator does not become a runtime or semantic consumer. |
| Repository catalog and navigation links | They make the application workspace discoverable without importing its code or contract. |
| Historical proposal and review citations | They preserve provenance. Current disposition must clearly state the leaf boundary. |
| A general design note saying “inspired by Tungsten's sparse stamps” | It attributes the idea while the new document defines an independent contract. |
| A non-normative source comment recording the origin of copied or adapted logic | Attribution is not a compiled dependency or semantic appeal; the owning general contract still controls behavior. |
| Sibling Tungsten tests transcribed from the C# fidelity suite | The C# implementation is intentionally authoritative within the Tungsten port family. |

## What An Independent Fork Requires

Calling a copy “independent” is not enough. All of the following must be true.

### 1. Independent ownership and placement

Choose a neutral project, package, namespace, module, or crate owned by the general collection
family. Do not place a general type in a Tungsten workspace merely because both designs compose the
same foundations.

When a composite needs both HAMT and FingerTree, a separate composition project is usually cleaner
than making either foundation depend on the other. The new project may reference both foundations;
neither foundation references the composite. If a primitive later proves broadly reusable, extract
it into an independently owned lower-level general package and review that dependency graph
separately.

### 2. Independent source

The fork owns every mechanism-specific component it needs, except components deliberately consumed
through documented public APIs of general dependencies. In the ordered-set example, the fork owns
its entry record, stamp comparer, stamp allocation, relabeling, stable rebuild, slicing strategy,
and invariant checks while HAMT and FingerTree continue to own their public cores. Source may
initially be copied with provenance, but it must not remain linked, generated from, or compiled out
of the Tungsten source tree.

Private duplication is acceptable when it protects ownership boundaries. Extract a shared general
core only when its abstraction and contract are independently justified—not merely to eliminate
similar-looking code. Multiple general consumers are strong evidence, not a prerequisite.

### 3. Independent semantic decisions

Specify every observable behavior without treating Tungsten as semantic authority; non-normative
provenance citations remain welcome:

- equality and hash-policy source;
- representative retention and replacement;
- duplicate construction behavior;
- insertion, movement, and positional-index interpretation;
- ordering of set/map algebra;
- stable versus maintained sorting;
- missing and empty-state failures;
- null handling;
- no-op reference identity;
- failure atomicity;
- concurrency/publication behavior; and
- the exact scope of structural-sharing promises.

The fork may reach the same answer as Tungsten, but the answer must be justified and test-locked by
the general collection's own contract.

### 4. Independent complexity and constants

State bounds derived from the new representation and its public substrate contracts. Do not expose
Tungsten's exact stamp gap, relabel threshold, one-descent implementation detail, or other constant
unless the general API independently needs that promise.

Branched persistence deserves special care: a relabel cost paid on one branch cannot generally be
amortized across siblings that branch from the earlier version. State a per-produced-version worst
case wherever a linear-history amortization would mislead.

### 5. Independent tests and oracle

Among repository collection projects, the new test project references only the new general project
and its general dependencies; ordinary test infrastructure such as xUnit and CsCheck remains
allowed. Its normative oracle is an independent model—for example, a comparer-aware `List<T>` plus
explicit equivalence-class lookup—not `PersistentAssociation` or another Tungsten type.

Tests adapted from Tungsten must record provenance and then be reviewed against the new contract.
Discard or rewrite cases whose expected result encodes an application-specific rule. Future
Tungsten test changes do not automatically update the fork's suite.

### 6. Independent documentation and evolution

Create an owning overview, usage guide, API specification, validation guide, catalog entry, and XML
or language-local API documentation. Record which Tungsten-inspired ideas were retained, altered,
or deliberately relaxed. State explicitly that Tungsten is provenance, not authority.

Subsequent changes are reviewed against the new general contract. A kernel-driven Tungsten change
is only input to a separate general design decision, never an automatic parity requirement.

### 7. Dependency audit

Before shipment, inspect project manifests, imports/includes, source generation, test references,
friend/internal-access grants, and documentation language. The audit must prove both:

1. no general source or test depends on a Tungsten artifact; and
2. no foundational provider names Tungsten merely to grant it privileged implementation access.

An aggregator may list both projects. That is not a reverse dependency, but the distinction should
be clear in the audit record.

At minimum, repeat the audit with repository searches over:

- C# `ProjectReference`/`PackageReference`, `using`, linked-source, source-generator, and
  `InternalsVisibleTo` entries;
- C/C++ includes, linked CMake targets, copied sources, and friend declarations;
- Haskell `build-depends` and imports;
- Kotlin imports and build-script source-root composition;
- Rust Cargo dependencies and `use` paths;
- test, sample, and benchmark manifests in every language;
- generated-source inputs and shared-file mappings; and
- current documentation that might name Tungsten as a general semantic baseline.

Allowlist repository aggregators only after verifying that they merely build or index the leaf.
An architecture-policy script or test may inspect paths, manifests, and textual Tungsten names to
enforce this rule; such inspection is allowed because it neither links to nor invokes a Tungsten
artifact. Record the commands and results in validation evidence until a permanent guard is checked
in.

## Worked Example: A General Persistent Ordered Set

### Rejected design

The original benchmark-independent proposal placed `PersistentOrderedSet<T>` in
`Tools.DataStructures.Tungsten` and represented it as `PersistentAssociation<T, Unit>`. That design
was mechanically compact but violated this policy in four ways:

1. the general public type lived in an application-owned assembly;
2. its representation depended directly on a Tungsten type;
3. its insertion and movement rules were inherited from Association; and
4. its complexity and tests treated Association as the authority.

Renaming the wrapper or moving only its namespace would not fix those dependencies.

### Correct project topology

A general ordered set belongs in a neutral composition workspace:

```text
Tools.DataStructures.Ordered
├── Tools.DataStructures.Hamt
└── Tools.DataStructures.FingerTree

Tools.DataStructures.Tungsten
├── Tools.DataStructures.Hamt
└── Tools.DataStructures.FingerTree
```

Neither consumer references the other. The first version should not refactor Tungsten to consume
Ordered merely because they share a design idea; their contracts evolve independently.

**Shipment (2026-07-15).** This topology now exists in C#, and the corresponding neutral
`ordered` modules have shipped in TypeScript and Python. All three `PersistentOrderedSet` ports
compose their language-local public HAMT and FingerTree substrates without importing, wrapping,
testing against, or granting privileged access to a Tungsten artifact. The worked design below is
therefore an implemented boundary example, not merely a hypothetical recommendation.

### Independently owned representation

One suitable representation is:

```csharp
public sealed class PersistentOrderedSet<T> : IReadOnlySet<T>
{
    private readonly FingerTreeDeque<Entry> _order;
    private readonly PersistentHashMap<T, long> _stamps;

    private readonly record struct Entry(long Stamp, T Item);
}
```

The Ordered project, not Tungsten, owns these invariants:

- the sequence and index have equal cardinality;
- the sequence contains exactly one representative of every equality class;
- sequence stamps strictly ascend;
- every index entry names exactly one sequence entry and vice versa;
- map and sequence agree on the stored representative;
- every derived version retains the receiver's equality comparer; and
- prior versions remain immutable and concurrently readable.

Sparse labels, midpoint insertion, relabeling, and stable rebuild may be forked as private Ordered
mechanics. The exact gap remains private. No `PersistentAssociation<T, Unit>` field, Tungsten source
link, or Tungsten friend grant is present.

### Deliberately independent set semantics

A general first contract can choose:

- `CreateRange`: first equivalent occurrence fixes both position and representative;
- `Add`: append only when absent; an equivalent present item is an identity-preserving no-op;
- `AddFirst` and `Insert`: add only when absent, without implicit movement or representative
  replacement;
- `MoveToFirst`, `MoveToLast`, and `MoveTo`: explicit movement of an existing class while retaining
  its stored representative;
- movement indexes describe the final result position, not Association's pre-removal position;
- no `Join` or Tungsten operation correspondence;
- stable one-shot `Sort` that retains the equality comparer but does not create a maintained sorted
  set; and
- set algebra ordered by the receiver's independently specified comparer and order rules.

Those decisions deliberately do not preserve Association's move-on-`Append`/`Prepend`, supplied-key
replacement, pre-removal `Insert` index, exact stamp gap, or kernel vocabulary.

### Independent validation

Among repository collection projects, the test project should reference
`Tools.DataStructures.Ordered` only; ordinary test infrastructure remains allowed. A comparer-aware
ordered-list model supplies expected behavior. Collision-heavy comparers, object-distinct equivalent
values, retained branches, relabel histories, stable sorting, no-op identity, exception paths, and
concurrent readers are still valuable scenarios, but their expected results come from the Ordered
contract.

The shipped C#, TypeScript, and Python suites follow that rule with language-local models and
dependency audits; none uses Tungsten output as a live oracle.

## Porting And Change Propagation

The propagation rule differs by family:

```text
new kernel evidence
        │
        ▼
C# Tungsten contract
        │
        ├────► C Tungsten
        ├────► C++ Tungsten
        ├────► Haskell Tungsten
        ├────► Kotlin Tungsten
        ├────► Rust Tungsten
        ├────► TypeScript Tungsten
        └────► Python Tungsten

        no automatic edge
        ╳
        └────► general ordered collections or foundational cores
```

General forks may initially remain C#-only while their contract settles. Cross-language parity is a
separate decision based on the general family's value and port economics, not on Tungsten already
having sibling ports. That separate decision has now produced neutral TypeScript and Python
`PersistentOrderedSet` ports; it creates no change-propagation edge from either Tungsten family.

## Future Extraction Protocol

If the Tungsten family moves to its application repository:

1. Select and record a handoff commit, destination, and path-history strategy before moving files.
2. Move the application-owned Tungsten sources, tests, documentation, and sibling ports as coherent
   units. Preserve their family-local C# semantic reference and kernel-evidence provenance.
3. Leave HAMT, FingerTree, and every other general provider in this repository. The moved family
   consumes them through the normal package, repository, or submodule mechanism chosen for the
   destination; do not clone their ownership into Tungsten merely for convenience.
4. Remove Tungsten entries from local solutions, language workspaces, build matrices, and test
   aggregators. These should be the only changes required for the remaining general builds.
5. Replace active catalog/navigation links with a concise handoff pointer. Preserve historical
   proposals, review reports, commit references, and provenance rather than rewriting history as if
   Tungsten had never lived here.
6. Validate this repository after removing Tungsten and validate the moved Tungsten family against
   the general providers it now consumes. Both sides must build and test independently.
7. Repeat the dependency audit on both repositories. Relocation must not introduce a reverse
   dependency, shared source owner, or privileged friend grant.

Relocation does not promote Tungsten into semantic authority for any general fork. Existing general
collections continue under their independently documented contracts, and later kernel-driven
changes remain application-local unless separately accepted through a general design review.

## Current Repository State

As of the repository HEAD recorded above:

- every Tungsten build graph points from Tungsten to its language-local general substrates;
- no general workspace has a project/package/link dependency on Tungsten;
- C# HAMT and FingerTree no longer grant `InternalsVisibleTo` access to Tungsten;
- C# `PersistentAssociation` composes public HAMT/FingerTree APIs; and
- the C#, TypeScript, and Python neutral Ordered packages compose public HAMT/FingerTree APIs and
  have no source, package, test-oracle, or privileged-access dependency on Tungsten; and
- canonical repository and workspace documentation identifies Tungsten as an application-specific
  leaf and C# as authoritative only within the Tungsten port lineage.

Future validation should repeat this audit rather than treating this snapshot as permanent proof.

## Review Checklist

Use this checklist for a new structure, project move, refactor, or parity change:

- [ ] Is the proposed owner general-purpose or Tungsten-specific?
- [ ] Do all code dependency arrows point from Tungsten toward general providers?
- [ ] Does any general project, test, sample, or benchmark reference a Tungsten artifact?
- [ ] Does any provider name Tungsten in a friend/internal-access grant?
- [ ] Is Tungsten cited only as provenance rather than the new type's semantic baseline?
- [ ] Does a fork own its complete mechanism-specific source instead of sharing or generating from
      Tungsten code?
- [ ] Are equality, representatives, ordering, failures, nulls, identity, and concurrency specified
      independently?
- [ ] Are complexity claims derived from the fork and its public dependencies?
- [ ] Does the fork have an independent model and expected results?
- [ ] Are retained, changed, and relaxed Tungsten guarantees documented explicitly?
- [ ] Would moving every Tungsten workspace out of this repository leave the general build and tests
      intact after removing only aggregator entries?
- [ ] Would a future kernel-driven semantic change remain confined to the Tungsten port family unless
      a separate general design decision accepts it?

## Relationship To Other Documents

- The root [`README.md`](../../README.md) states the short canonical rule for agents and maintainers.
- The [workspace map](workspace-map.md) owns current dependency direction and workspace roles.
- The [semantic contracts](semantic-contracts.md) define the application-specific-leaf vocabulary.
- The [porting guide](../guides/porting-and-semantic-parity.md) defines family-local Tungsten parity.
- The [data-structure catalog](data-structure-catalog.md) inventories the shipped Tungsten surfaces
  without presenting them as general foundations.
- The [benchmark-independent implementation proposal](../proposals/benchmark-independent-next-structures-2026-07-14.md)
  records how this policy was applied to the now-shipped general ordered set.

When a shorter summary conflicts with this document on ownership, dependency, or fork
independence, this detailed boundary is the controlling repository policy.
