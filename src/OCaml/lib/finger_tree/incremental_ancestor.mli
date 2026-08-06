(** The append-only incremental level-ancestor seam shared by the ancestry-backed collections.

    An incremental level-ancestor arena is a monotone forest that grows one leaf at a time and
    answers "which ancestor of this node sits at absolute depth [d]?". Node handles are stable
    integers owned by one arena. The distinguished {!bottom} node has depth [-1] and no value; every
    labelled node has a depth of zero or more. A leaf may be added below any previously published
    node, not only below the most recently added one, so old consumer versions grow into independent
    branches.

    A successful {!add_leaf} publishes a fresh, never-recycled handle whose immutable parent is the
    supplied handle and whose depth is exactly one greater. {!depth_of}, {!parent_of}, {!value_at},
    and ancestor answers for a published node never change, and a failed addition publishes no
    partially initialized node.

    {!depth_of}, {!parent_of}, and {!value_at} are [O(1)]: the parameterized bounds documented by the
    consuming collections assume those primitive reads are constant time. Let [U] be leaf-add time
    and [Q] be level-ancestor query time after [M] published nodes. A backend with [U = Q = O(1)]
    worst case would give the consumers their optimal bound; the shipped Myers scheme gives
    [U = O(1)] amortized and [Q = O(log M)].

    Handles are arena-relative capabilities. Presenting a handle obtained from a different arena
    violates the precondition even when the same integer is valid locally, because range validation
    cannot identify an in-range integer copied from elsewhere.

    This is the one deliberately mutable core in the sequence families, in the sense the workspace
    README reserves for builders and streaming cores: an arena accumulates nodes in place while the
    collection values built on it stay immutable. Its operations are serialized by a private mutex,
    so one arena shared between collection values is safe for concurrent use; it makes no lock-free
    progress guarantee.

    {1 The backend seam is deliberately not exposed}

    The managed baseline makes the backend a public extension point — [IIncrementalAncestorArena<T>]
    — and the Rust, C++, C, and Kotlin ports each preserve it as a trait, concept, vtable, or
    interface, so a caller can supply an alternative arena. This port deliberately does not: there
    is no [module type] and no functor, the type above is concrete, and both consumers bind it
    directly. The seam exists in the baseline to admit a different level-ancestor scheme, and this
    workspace ships exactly one; adding a functor layer for a backend nobody supplies would
    complicate every consumer signature for no reachable benefit. The consequence is stated plainly
    so it is not mistaken for an oversight: the bounds the consumers document are parameterized by
    [U] and [Q] as a reduction to the published result, not as an instantiation point a caller can
    reach here. A future alternative backend is a change to this module, not a caller-side choice.
    Haskell drops the seam too, for the different reason that it has no arena object at all. *)

type 'value t
(** An append-only arena of labelled nodes. *)

type statistics = {
  published_node_count : int;
  block_count : int;
  allocated_slot_count : int;
  add_leaf_count : int;
  ancestor_query_count : int;
  last_ancestor_hop_count : int;
  maximum_ancestor_hop_count : int;
  total_ancestor_hop_count : int;
}
(** Representation and traversal counters from an arena snapshot. [block_count] and
    [allocated_slot_count] describe the odd-sized block store; the hop counters describe the
    parent/jump links that ancestor queries followed. *)

val create : unit -> 'value t
(** An arena containing only its unlabelled bottom node. *)

val bottom : int
(** The distinguished unlabelled node at depth [-1]. This read is [O(1)]. *)

val published_node_count : 'value t -> int
(** The number of labelled nodes published by this arena. This read is [O(1)]. *)

val add_leaf : 'value t -> parent:int -> 'value -> (int, string) result
(** [add_leaf arena ~parent value] publishes a labelled leaf below [parent] and returns its stable
    handle. This addition is [O(1)] amortized over the block allocations; a block-boundary call
    pays [Theta (sqrt M)] for the new odd block, the accepted contract of the shared odd-block
    representation. Fails when [parent] was
    never published by this arena, or when the depth, the node count, or a coalesced jump distance
    cannot grow further. *)

val depth_of : 'value t -> int -> (int, string) result
(** A node's immutable absolute depth; the bottom node has depth [-1]. This read is [O(1)]. Fails
    when the node was never published by this arena. *)

val parent_of : 'value t -> int -> (int, string) result
(** A labelled node's parent, or {!bottom} when the node has depth zero. This read is [O(1)]. Fails
    for the bottom node, which has no parent, and for a node this arena never published. *)

val value_at : 'value t -> int -> ('value, string) result
(** The immutable value associated with a labelled node. This read is [O(1)]. Fails for the bottom
    node, which carries no value, and for a node this arena never published. *)

val ancestor_at_depth : 'value t -> int -> depth:int -> (int, string) result
(** [ancestor_at_depth arena node ~depth] is the unique ancestor of [node] at absolute [depth]. It
    follows [O(log M)] parent/jump links in the worst case after [M] published nodes. Fails when the
    node was never published, or when [depth] is outside [-1] through the node's own depth. *)

val statistics : 'value t -> statistics
(** Deterministic representation and traversal counters, read in [O(1)]. *)

val validate : 'value t -> int -> (unit, string) result
(** Audits one node's whole root path against the Myers invariants: depths decrease by exactly one
    per parent link, the bottom node terminates the path at depth [-1], and every jump link lands
    exactly its recorded distance above its owner. [O(d)] for a node at depth [d]. *)
