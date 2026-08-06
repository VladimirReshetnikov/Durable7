(** Persistent sequence that ranks and selects events whose occurrence depends on finite left
    context.

    A sum-measured sequence can rank elements carrying a fixed local weight. It cannot rank a comma
    that separates fields in one prefix context and is quoted data in another, because no single
    weight is correct for both. A streaming automaton keeps that context but has to replay a prefix
    after every edit.

    This module resolves that by lifting a finite deterministic machine into the measure of the
    workspace's measured tree. Every subtree caches, {e for every possible incoming state}, the
    outgoing state and the number of events emitted while consuming that subtree. Ordered
    composition feeds the left subtree's outgoing state into the right subtree's summary, which
    turns a context-dependent scan into an associative measure:

    {[
      (a * b).next (q)   = b.next (a.next q)
      (a * b).events (q) = a.events q + b.events (a.next q)
      (a * b).count      = a.count + b.count
    ]}

    Composition is {b not} commutative: the left operand chooses which column of the right operand's
    table is read. The identity maps every state to itself, emits no events, and holds no elements.
    Because event counts are nonnegative, the select predicate [prefix.events q0 > r] is monotone,
    so one measure-directed descent finds the element that emitted event [r] — no binary search and
    no prefix replay. Every cached table is an eagerly filled pair of arrays; nothing is deferred
    behind a lazy indirection.

    {2 Bounds on this substrate}

    Let [n] be the element count and [s] the machine's state count. These figures are worst case per
    operation, not amortized averages: {!Measured_tree} is strict and does no lazy amortization.

    - Whole-sequence evaluation is [O(1)]: it reads the cached root summary.
    - Element access by index is [O(log n)] and composes no summary.
    - Prefix evaluation, contextual event rank and select, insertion, replacement, removal,
      splitting, and slicing are [O(s log n)]: a measure-directed descent over [O(log n)] levels,
      each level composing one [O(s)] effect table.
    - Enumeration is [O(n)], and {!of_list} is [O(s n)].
    - The structure costs [O(s n)] summary storage; a retained changed spine adds [O(s log n)] while
      untouched structure stays shared.

    Two bounds are deliberately {b weaker} than the C# reference states, because this workspace's
    substrate is not the same structure. {!Measured_tree} is a weight-balanced join tree of leaves
    (Hirai and Yamamoto, delta = 3, gamma = 2) with no digits and no lazy middle, so it has no
    finger:

    - Endpoint updates ({!cons}, {!snoc}) are [Theta (s log n)], not [O(s)]. Pushing one element
      joins a singleton against a tree whose leftmost or rightmost spine has [Theta (log n)] levels,
      and every node rebuilt on that spine recomposes an [O(s)] effect table. The C# reference sits
      on a lazy Hinze-Paterson finger tree whose digits give amortized [O(1)] node work at either
      end, which is the only reason it can claim [O(s)].
    - Concatenation is [Theta (s (1 + log (max (n, m) / min (n, m))))], proportional to the
      operands' weight ratio: the join descends the heavier operand's spine until the two weights
      come within the balance factor and rebuilds exactly those nodes. That is [O(s)] for operands
      of comparable size but degrades to [O(s log (max (n, m)))] when they are asymmetric, so the
      [O(s log (min (n, m)))] form the C# reference claims does {b not} hold here.

    When the machine is fixed, [s] is a constant and the contextual queries are [O(log n)] instead
    of the [Theta (n)] replay a state-free persistent sequence needs. Treating [s] as an input, this
    structure does not dominate a plain persistent sequence: its edits and its storage carry the [s]
    factor explicitly.

    {2 Deliberate limits}

    Context must be finite and must flow left to right. Emitted event counts must be nonnegative so
    select stays monotone. One input element occupies one measured leaf, so this is not a chunked
    rope and makes no cache-density claim. There is no way to change the machine of an existing
    sequence; rebuilding under another machine costs [O(s n)].

    Every value is immutable and persistent: a rejected successor publishes nothing and leaves every
    retained version readable and unchanged. *)

type transition = {
  next_state : int;
  emitted_count : int;
}
(** One transition of a deterministic additive event machine. [next_state] is the state after
    consuming the element and must lie in [0 .. state_count - 1]; [emitted_count] is the
    nonnegative number of logical events the transition emits, and a single transition may emit more
    than one. It is named [emitted_count] rather than [event_count] so it never collides with
    {!prefix_summary}'s field of that name: wherever the record's type is not already known, a
    shared label is warning 41 (ambiguous-name), which this workspace treats as an error. *)

type 'element machine
(** A finite deterministic machine whose transitions emit nonnegative event counts.

    This is a runtime policy value rather than the C# reference's static-abstract phantom type: it
    carries the state count, the transition function, and an identity string. A sequence retains its
    machine, so an empty result of an edit stays usable. *)

type prefix_summary = {
  final_state : int;
  event_count : int;
}
(** The result of running a machine over a prefix: the state after the prefix and the number of
    events the prefix emitted. *)

type event_location = {
  element_index : int;
  event_index_in_element : int;
  state_before : int;
  state_after : int;
  element_event_count : int;
}
(** Identifies one contextual event and the input element that emitted it: the emitting element's
    zero-based index, the event's zero-based ordinal among the events that one transition emitted,
    the machine state on either side of the element, and that transition's total event count. *)

type 'element t
(** An immutable sequence indexed by events whose occurrence depends on finite left context. *)

val create_machine :
  id:string ->
  state_count:int ->
  transition:(int -> 'element -> transition) ->
  unit ->
  ('element machine, string) result
(** [create_machine ~id ~state_count ~transition ()] validates and retains a machine. Fails when
    [id] is blank or when [state_count] is not positive, which is the C# reference's faulted static
    initializer expressed as an ordinary error.

    [transition] must be deterministic and free of side effects, must accept every state in
    [0 .. state_count - 1], and must answer with a state in the same range and a nonnegative
    [emitted_count]. A violation is not raised: it is recorded in the affected subtree's summary and
    surfaces as an [Error] from whichever operation introduced the element, which then publishes
    nothing.

    [id] is the machine's identity for {!concat}, exactly as a measure policy's id governs whether
    two measured trees may be combined. Two machines sharing an id are treated as the same machine,
    so distinct machines must be given distinct ids. *)

val machine_id : 'element machine -> string
(** The machine's identity string, which decides whether two sequences may be concatenated. *)

val machine_state_count : 'element machine -> int
(** The machine's finite state count, which is positive. *)

val empty : 'element machine -> 'element t
(** The empty sequence under a machine. Its summary maps every state to itself with no events. *)

val of_list : 'element machine -> 'element list -> ('element t, string) result
(** A sequence holding a list's elements in input order, built in bulk rather than by repeated
    insertion. [O(s n)]. Fails when the machine violates its contract on some element. *)

val machine : 'element t -> 'element machine
(** The machine this sequence was built under and still retains. *)

val state_count : 'element t -> int
(** The state count of the sequence's machine. *)

val length : 'element t -> int
(** Number of stored elements, read from the cached root. [O(1)]. *)

val is_empty : 'element t -> bool
(** Whether the sequence holds no elements. [O(1)]. *)

val nth : int -> 'element t -> 'element option
(** The element at a zero-based index, or [None] when the index is outside the sequence. [O(log n)],
    composing no summary. *)

val to_list : 'element t -> 'element list
(** The elements, in sequence order. [O(n)]. *)

val iter : ('element -> unit) -> 'element t -> unit
(** Applies the function to each element, in sequence order. [O(n)]. *)

val evaluate : initial_state:int -> 'element t -> prefix_summary option
(** Runs the machine over the whole sequence from [initial_state], reading the cached root summary.
    [O(1)]. [None] when [initial_state] is outside [0 .. state_count - 1]. *)

val evaluate_prefix : element_count:int -> initial_state:int -> 'element t -> prefix_summary option
(** Runs the machine over the first [element_count] elements from [initial_state]. [O(s log n)], and
    [O(1)] at either endpoint, where the answer is the identity or the cached root summary. [None]
    when [initial_state] is outside the machine's range or [element_count] is outside
    [0 .. length]. *)

val event_rank : element_count:int -> initial_state:int -> 'element t -> int option
(** The number of contextual events strictly before an element boundary. Misses on the same inputs
    as {!evaluate_prefix}. [O(s log n)]. *)

val select_event : event_index:int -> initial_state:int -> 'element t -> event_location option
(** [select_event ~event_index ~initial_state sequence] locates the zero-based contextual event
    [event_index], counting from [initial_state].

    [None] when [initial_state] is outside the machine's range, or when [event_index] is negative or
    at or past the sequence's total event count from that state.

    [O(s log n)]: one measure-directed descent over a monotone predicate, then exactly one machine
    transition for the located element. There is no binary search and no prefix replay. *)

val cons : 'element -> 'element t -> ('element t, string) result
(** A sequence with the element added at the front. [Theta (s log n)] on this substrate, not [O(s)];
    see the module's bounds. Fails when the machine violates its contract on the element. *)

val snoc : 'element t -> 'element -> ('element t, string) result
(** A sequence with the element added at the back, with the cost and failure mode of {!cons}. *)

val concat : 'element t -> 'element t -> ('element t, string) result
(** The concatenation of two sequences, sharing both operands' unchanged structure and returning the
    nonempty operand itself when the other is empty.

    Fails when the two sequences were built under machines with different {!machine_id}s or
    different state counts: the id is the identity gate, following the same precedent as a measure
    policy's id and a range-update algebra's id. Also fails when the composed event total would
    overflow.

    [Theta (s (1 + log (max (n, m) / min (n, m))))]; see the module's bounds. *)

val insert_at : int -> 'element -> 'element t -> ('element t, string) result
(** A sequence with the element inserted so that [index] old elements precede it. [O(s log n)].
    Fails when [index] is outside [0 .. length] or the machine violates its contract. *)

val set_item : int -> 'element -> 'element t -> ('element t, string) result
(** A sequence with the element at the index replaced. [O(s log n)]. Fails when the index is outside
    the sequence or the machine violates its contract. *)

val remove_at : int -> 'element t -> ('element t, string) result
(** A sequence without the element at the index. [O(s log n)]. Fails when the index is outside the
    sequence. *)

val split_at : int -> 'element t -> ('element t * 'element t, string) result
(** Splits into the [index] elements before the boundary and those from it onward, sharing the
    receiver with the nonempty half at either endpoint. [O(s log n)]. Fails when [index] is outside
    [0 .. length], and — because an interior split re-joins subtrees the source never had to compose
    against each other — when either half's recomposed event total would overflow. Either half
    faulting fails the whole call, so no partial result escapes. *)

val sub : start:int -> length:int -> 'element t -> ('element t, string) result
(** The [length] elements starting at [start], sharing the receiver when the range covers the whole
    sequence. [O(s log n)]. Fails when the range falls outside the sequence, and — for the same
    recomposition reason as {!split_at} — when the selected window's event total would overflow. *)

val validate : 'element t -> (unit, string) result
(** Audits the tree's shape and recomputes every cached root summary by direct machine scan, for
    every initial state, comparing it with the cached tables. A defensive check for tests and
    diagnostics; ordinary operations maintain these invariants. [O(s n)]. *)
