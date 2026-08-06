(** Fully persistent queue slice denoting one interval of an append ancestry path.

    A value names a sequence indirectly. Instead of owning nodes it retains an
    {!Incremental_ancestor} arena, a tail node, the absolute depth of its first visible element, and
    a redundant count cache. The visible elements are the labels of the ancestors of the tail at
    absolute depths [low_depth] through [depth tail], so [length = depth tail - low_depth + 1].
    Appending adds one child below the tail; removing and slicing move only the two interval
    boundaries. Because the arena is append-only and its published nodes are immutable, every
    retained value keeps denoting exactly the same sequence forever, and appending below an old
    value simply starts a sibling branch. That is what makes the collection fully persistent for its
    restricted algebra: any historical version can be extended, not only the newest one. It is not
    confluently persistent, because two unrelated versions cannot be merged.

    {2 The anchored-empty rule}

    An empty value is not nothing. It retains the node immediately {e before} its window as an
    anchor, so [low_depth = depth tail + 1] holds exactly when the queue is empty. Appending to any
    empty slice therefore yields exactly the new value and never resurrects the discarded prefix. A
    queue drained with {!remove_first} keeps its original tail as that anchor while one drained with
    {!remove_last} walks back to the node one level above its window — the arena's bottom node only
    when the window began at absolute depth zero, and an ordinary labelled node otherwise. Two empty
    values reached through different histories therefore have distinct provenance while denoting the
    same empty sequence. There is
    consequently no canonical empty value and no process-global empty arena.

    {2 Bounds}

    The bounds are parameterized by the arena. Let [M] be the number of labelled nodes the arena
    ever published, [U] its leaf-add cost, and [Q] its level-ancestor query cost. Then {!add_last}
    costs [U]; {!first}, {!nth}, {!take}, {!slice}, {!pop_front}, and a nontrivial {!split_at} cost
    [Q]; and {!length}, {!is_empty}, {!last}, {!remove_first}, {!remove_last}, {!pop_back}, and
    {!drop} are [O(1)]. Instantiating the arena with Alstrup--Holm incremental level ancestry would
    give [U = Q = O(1)] worst case in linear space, making every scalar operation above [O(1)] worst
    case; that is a reduction to a published result, not a property of anything shipped here. The
    shipped seam has [U = O(1)] amortized and [Q = O(log M)] worst case, so no amortized bound above
    may be read as a worst-case bound.

    Boundary-specialized calls reuse an existing or derived tail and make no ancestor query at all:
    {!take} of the whole length, {!drop} of zero, a {!slice} whose window ends at the retained tail,
    a {!split_at} at the length, {!nth} at the last visible index, and {!first} of a singleton.
    {!to_list} follows parent links rather than making one ancestor query per element, so it costs
    time and transient space linear in the visible length.

    {2 Deliberate limits}

    The algebra intentionally excludes prepending, point replacement, arbitrary middle edits, and
    concatenation of unrelated histories; omitting them is precisely why the indexed and slicing
    bounds are possible. Arena space is charged to {e all} successful historical appends, not to the
    visible length of one value, so dropping a prefix or releasing a value reclaims nothing. Two
    values may enumerate equal elements while holding different tails, anchors, or arenas, so this
    collection promises no [O(1)] equality, hashing, or canonical empty identity and offers none. *)

type 'element t
(** An immutable ancestry interval: one arena, one tail node, the absolute depth of the first
    visible element, and a cached count. Copying a value copies those four words; the arena is
    shared, never duplicated. *)

val of_arena : 'element Incremental_ancestor.t -> 'element t
(** The empty queue anchored at the arena's bottom node. This is the sharing seam: queues derived
    from one arena share its retained history and its serializing lock, so appends made through
    unrelated values become sibling branches of the same forest. [O(1)]. *)

val empty : unit -> 'element t
(** The empty queue on a fresh, privately owned arena. There is deliberately no single canonical
    empty value — each call owns its own arena, so unrelated workloads share neither retained
    history nor write contention — which is why this takes [unit] rather than being a constant.
    [O(1)]. *)

val of_list : 'element list -> ('element t, string) result
(** A queue holding a list's elements, appended in list order onto a fresh private arena. Publishes
    one arena node per element. Fails only when the arena rejects an addition, which requires the
    ancestry depth or the node count to be unable to grow further. *)

val length : 'element t -> int
(** Number of visible elements, read from the cached count in [O(1)]; the arena is not consulted. *)

val is_empty : 'element t -> bool
(** Whether this value denotes an empty ancestry interval. [O(1)]. *)

val first : 'element t -> 'element option
(** The first visible element, or [None] when the queue is empty. Costs one ancestor query [Q]: the
    front is an ancestor selected jointly by the tail and the low boundary, so it is not an [O(1)]
    endpoint read under a logarithmic arena. A singleton reuses its retained tail and makes no
    query. *)

val last : 'element t -> 'element option
(** The last visible element, or [None] when the queue is empty. [O(1)]: the tail is retained by the
    value itself. *)

val nth : int -> 'element t -> 'element option
(** The visible element at the given zero-based rank, or [None] when the rank is outside the queue.
    Costs one ancestor query [Q]; the last visible rank reuses the retained tail unqueried. *)

val add_last : 'element -> 'element t -> ('element t, string) result
(** A queue with one element appended below this value's tail or empty anchor. Costs [U] and
    publishes exactly one arena node; the source and every other retained version are unaffected,
    and appending below an old value starts a sibling branch. Fails when the visible count cannot
    grow further or when the arena rejects the addition, in which case nothing is published and the
    source stays usable. *)

val remove_first : 'element t -> 'element t option
(** The queue without its first visible element, or [None] when it is empty. [O(1)] and query-free:
    only the low boundary moves. Emptying a singleton this way leaves the old tail as the result's
    anchor. *)

val remove_last : 'element t -> 'element t option
(** The queue without its last visible element, or [None] when it is empty. [O(1)]: the new tail is
    the old tail's parent, which the arena reads in constant time. Emptying a singleton this way
    anchors the result immediately before the window. *)

val pop_front : 'element t -> ('element * 'element t) option
(** The first visible element together with the queue remaining, or [None] when the queue is empty.
    Costs [Q], because it also returns the front element. *)

val pop_back : 'element t -> ('element t * 'element) option
(** The queue remaining together with the last visible element, or [None] when the queue is empty.
    [O(1)]. *)

val take : int -> 'element t -> ('element t, string) result
(** The first n visible elements as an appendable ancestry prefix. Costs [Q]; taking the whole
    length returns the source value itself and makes no query. Taking zero from a non-empty queue is
    anchored immediately before the window and does cost a query. Fails when n is negative or
    exceeds the length. *)

val drop : int -> 'element t -> ('element t, string) result
(** The queue after omitting its first n visible elements. [O(1)] and query-free: only the low
    boundary moves, and dropping zero returns the source value itself. Dropping the whole length
    keeps the old tail as the result's anchor. Fails when n is negative or exceeds the length. *)

val slice : start:int -> length:int -> 'element t -> ('element t, string) result
(** One contiguous, appendable ancestry window. Costs [Q] in general; a window ending at the
    retained tail — the whole window and every suffix — reuses that tail and makes no query. A
    zero-length window selects the ancestor immediately before its start as an anchor, which is the
    arena's bottom node when the start is the very front of a queue grown from an empty one. Fails
    when either bound is negative or the window extends past the end. *)

val split_at : int -> 'element t -> ('element t * 'element t, string) result
(** The appendable prefix holding the first n visible elements and the appendable suffix holding the
    rest. Costs [Q]: the prefix performs the one ancestor-seeking query and the suffix only moves
    the low boundary. Only a split at the length is query-free, where the prefix is the source value
    itself. A split at zero of a non-empty queue still costs [Q] and cannot be specialized away,
    because the anchored-empty rule makes the empty prefix retain the node at [low_depth - 1] and
    only an ancestor query can name that node from the retained tail; on an {e empty} queue the two
    boundaries coincide, so its only split is query-free. Fails when n is negative or exceeds the
    length. *)

val to_list : 'element t -> 'element list
(** The visible elements, front to back. Follows parent links from the tail into one list rather
    than making one ancestor query per element, taking time and transient space linear in the
    visible length. A concurrent append cannot enter the captured path. *)

val validate : 'element t -> (unit, string) result
(** Audits the window against the arena: the low depth is non-negative, the tail is a node this
    arena published, its depth is exactly [low_depth + length - 1] — which is the anchored-empty
    rule when the queue is empty — and the tail's whole root path satisfies the arena's own
    invariants. [O(d)] for a tail at depth [d]. For tests and diagnostics. *)
