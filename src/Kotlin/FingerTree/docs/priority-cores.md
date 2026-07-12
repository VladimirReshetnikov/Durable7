# Kotlin priority-core notes

- Created (UTC): 2026-07-12T02:31:20Z
- Repository HEAD: b13ead2e581004cd994909575ee4c0b6f4022d10
- Audience: Callers and maintainers of the Kotlin/JVM priority structures
- Scope: `BrodalOkasakiHeap` and `PrioritySearchQueue`

The FingerTree workspace contains two direct persistent priority cores in addition to its measured-
tree `PriorityQueue` facade. They preserve the C# semantic reference and occupy narrower algorithmic
niches: `BrodalOkasakiHeap<T>` provides worst-case constant insert and meld, while
`PrioritySearchQueue<K, P, V>` combines keyed lookup with priority and key-range filtering.

## Brodal-Okasaki heap

`BrodalOkasakiHeap<T>` is the bootstrapped skew-binomial representation, not a wrapper around a JVM
mutable heap. Its rank-zero global root stores the minimum. The root's child list fuses the primitive
children of a skew-binomial tree with the embedded heap forest described by Brodal and Okasaki.
Insertion and meld perform one root comparison plus at most one skew link. Minimum reads the global
root without comparing. Minimum deletion decodes the selected tree's fused boundary, uniquely links
its ranked forests, and restores zero-rank children.

The resulting bounds match the audited managed implementation:

- `minimum`, `insert`, and `meld` are O(1) worst-case;
- `deleteMinimum` and `minimumView` are O(log n) worst-case;
- bulk `from` is O(n) through repeated constant-time insertion;
- structural-order iteration is O(n), stack-safe, and comparator-free.

The heap is a multiset. Comparator-equivalent values remain distinct stored representatives; ties
choose deterministically during linking but iteration order is intentionally unspecified. Custom
comparator identity is part of the representation policy. `meld` requires both operands to retain
the exact same `Comparator` object (`===`), because JVM comparators have no useful structural
equality. Natural-order factories use Kotlin's shared singleton comparator and interoperate.
Melding either side with a compatible empty heap returns the existing nonempty operand by identity.

Every tree and forest cell is immutable. Constant-time operations allocate only their bounded new
frontier and retain prior cells; logarithmic deletion copies only the affected ranked forest.
`sharedTreeCount` and `rootIdentityForTesting` are internal executable-audit diagnostics rather than
semantic API. `validateStructure` uses an explicit worklist to check the global rank, every fused
primitive/embedded boundary, skew-forest rank ordering, heap order, logical occurrence count, and
maximum depth. Its traversal deliberately counts repeated logical occurrences when self-meld shares
the same immutable subtree twice.

## Winner-cached priority search queue

`PrioritySearchQueue<K, P, V>` is a persistent AVL map with one entry per key-comparator equivalence
class. Every node stores its own entry, subtree count and height, and the winning entry of its whole
subtree. Lower priority wins; priorities equal under the retained comparator break by the retained
key comparator. Consequently `minimum` is O(1), keyed lookup/update/removal and minimum deletion are
O(log n), and iteration is in key order.

`setItem` has last-wins priority/payload behavior while retaining the first concrete key
representative. It returns the same queue object only when all of these agree with storage:

1. priority comparison returns zero;
2. the priority values are equal by Kotlin `==`;
3. the payload values are equal by Kotlin `==`.

`tryAdd` rejects an equivalent key and returns the receiver; absent `remove` does likewise.
`tryRemove` returns the stored key representative. These distinctions matter when ordering equality
is coarser than ordinary object equality. Entry/result wrappers keep nullable keys, priorities, and
payloads unambiguous: a stored all-null entry is still distinct from a missing entry or failed
removal, and an exact all-null replacement remains an identity no-op under explicit null-aware
comparators.

`enumerateAtMost(minimumKey, maximumKey, maximumPriority)` returns a lazy, key-ordered sequence.
The range is inclusive and validated eagerly. A subtree is skipped before any key comparisons when
its cached winner exceeds the inclusive priority threshold. The bound is O(log n + v), where `v` is
the number of nodes whose winner cannot be pruned and can be n; this direct AVL core does not claim
the output-sensitive bound of Hinze's pennant representation.

Validation uses an explicit stack and independently checks strict key bounds, AVL balance, cached
count/height, and the exact stored winner at every node. Update recursion is bounded by the audited
AVL height, so even ascending 50,000-key construction remains JVM-stack-safe. The type deliberately
does not define cross-policy equality or algebra: the C# reference exposes neither, and pretending
that arbitrary comparator objects are structurally comparable would be misleading.

## JVM audit boundary

The executable suite ports the managed operation-count audit rather than wall-clock assertions.
Brodal minimum performs zero comparisons; insert and meld allow one through five; delete-min is
bounded by `32 * (floor(log2(n)) + 1) + 8` through 65,536 elements. PSQ tests pin the exact key and
priority comparison equations for whole-tree pruning and a one-key range. Both cores have retained-
history models, adversarial shapes, identity-sharing checks, representative/tie cases, and races
among concurrent readers of immutable snapshots.

The C# audit additionally bounds allocated bytes per deletion. Kotlin intentionally does not copy
that numerical assertion: HotSpot tiered compilation, escape analysis, object headers, and collector
selection make a portable byte ceiling meaningless. The JVM gate instead measures exact comparator
work and structural identity retention, while the representation itself remains the same persistent
algorithm.
