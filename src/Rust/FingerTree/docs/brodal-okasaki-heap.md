# Rust Brodal-Okasaki Heap

- Created (UTC): 2026-07-12T03:10:50Z
- Repository HEAD: ca5b911c14c4e4683fa2d675c47d441dd11ad43b
- Audience: Maintainers and reviewers of the Rust priority-core port
- Scope: `BrodalOkasakiHeap<T>` representation, policy, ownership, complexity, and evidence

`BrodalOkasakiHeap<T>` is a persistent min-heap implementing Brodal and Okasaki's bootstrapped
skew-binomial representation directly. It is separate from the stable measured
`PriorityQueue<T, P>` facade: the Brodal core exists specifically for worst-case O(1) insert and
meld as well as worst-case O(log n) delete-min.

## Representation and operations

The heap retains a rank-zero global tree whose value is the minimum. Its child forest is a skew
forest. Each ranked tree fuses the primitive child encoding with that child's embedded heap forest,
matching the C# semantic reference. Immutable trees, forest cons cells, and values are retained by
`Arc`; an update allocates only its changed representation path and leaves every prior snapshot
valid.

| Operation | Worst-case work | Result |
| --- | --- | --- |
| `minimum` | O(1), zero comparisons | `Option<&T>` |
| `insert` | O(1), at most a constant number of comparisons | New heap |
| `meld` | O(1), at most a constant number of comparisons | `Result<heap, BrodalMeldError>` |
| `delete_minimum` | O(log n) | `Option<heap>` |
| `minimum_view` | O(log n) | `Option<BrodalMinimumView<T>>` |
| `iter` | O(n), zero comparisons | Borrowed structural-order iterator |
| `validate_structure` | O(n) time and O(n) worst-case worklist | Statistics or invariant error |

Iteration is structural rather than sorted. Repeated minimum views provide a sorted drain.

## Ordering-policy compatibility

`OrderPolicy<T>` owns a `Send + Sync` `OrderComparer<T>`. A cloned custom policy preserves identity,
and heaps may meld only when those identities match. Independently constructed custom policies are
rejected even if their comparison functions happen to agree, because comparer identity is retained
representation policy in the C# contract.

Natural ordering is the deliberate Rust exception to literal object identity. `OrderPolicy::natural`
marks the policy canonical, allowing independently constructed `BrodalOkasakiHeap::new()` values to
meld just as independently obtained C# empty heaps share `Comparer<T>.Default`. Custom policies
remain identity-gated. All comparer implementations must define a stable total preorder.

## Ownership and representatives

The core does not require `T: Clone`. Values are stored once behind `Arc<T>`; new structural roots
clone handles, not payloads. `minimum` borrows the retained representative, while
`BrodalMinimumView<T>` owns an `Arc<T>` and the persistent remainder. The removed representative
therefore remains available after either heap value is dropped.

Every inserted concrete value remains a distinct heap occurrence even when the comparer reports it
equivalent to another value. `Option<T>` values are unambiguous: `Some(&None)` from `minimum` means
the stored minimum is `None`, while outer `None` means that the heap is empty.

## Invariants and diagnostics

`validate_structure` uses an explicit worklist and checks:

- the global rank-zero root and logical count;
- nondecreasing skew-forest ranks, with duplication permitted only for the first two ranks;
- every primitive-child/embedded-forest boundary in the fused encoding;
- linked and skew-linked child-rank equations;
- heap order on every stored edge;
- maximum rank and maximum root-to-tree depth.

`shares_root_with` identifies exact no-op/root reuse. `shared_tree_count` counts distinct tree nodes
retained across two versions and makes logarithmic path copying auditable without exposing mutable
representation access.

## Validation evidence

`tests/brodal_okasaki_heap.rs` covers:

- sorted drains from 4,096-element ascending, descending, equal-valued, and independently melded
  shapes;
- a 20,000-operation branching model with insertion, deletion, meld, and up to 256 retained
  multiset snapshots;
- retention of all comparer-equivalent concrete representatives;
- exact custom-policy identity rejection and canonical-natural interoperability;
- root reuse, off-path sharing, large delete-min sharing, and self-meld multiplicity;
- `Option` values and an element type that deliberately does not implement `Clone`;
- zero-comparison minimum/iteration, constant comparison ceilings for insert/meld, and logarithmic
  delete-min ceilings at 1,024, 4,096, 16,384, and 65,536 elements;
- `Send + Sync` type assertions and eight concurrent immutable readers.
