(** Bilateral ancestral deque: a persistent double-ended sequence held as two oppositely oriented
    intervals of an append-only ancestry arena.

    This is not a balanced sequence tree. A version is a constant-sized handle over one
    {!Incremental_ancestor.t}. It stores a {e left} interval and a {e right} interval, each running
    from an exclusive anchor down to an inclusive tail, and the handle's logical value is the
    reversal of the left interval followed by the right one. {!add_first} publishes one leaf below
    the left tail, {!add_last} publishes one below the right tail, and {!reverse} only exchanges the
    two interval descriptors, so reversal costs [O(1)] and needs neither a lazy flag nor a rebuild.

    The load-bearing property is {e slice closure}: every contiguous slice meets each interval at
    most once, so {!slice} and {!split_at} again produce bilateral handles rather than rebuilt
    trees. Each interval also caches the first physical node after its anchor. That cache is
    redundant but load-bearing: it keeps {!first}, {!last}, and the two logical endpoints of each
    interval readable with no level-ancestor query even when the opposite arm is empty.

    {1 Bounds}

    Two layers of claim must not be conflated.

    - {e The reduction.} Let [U] be arena leaf-add time and [Q] be arena level-ancestor query time
      after [M] published nodes. {!add_first} and {!add_last} cost one [U] and no query. {!count},
      {!is_empty}, {!first}, {!last}, {!reverse}, {!clear}, {!arena}, and {!shares_storage_with} are
      [O(1)] and make no query. {!nth} costs at most one query and none at the four cached logical
      endpoints. {!remove_first} and {!remove_last} cost none on their own nonempty arm and at most
      one when the removal crosses the centre. {!slice}, {!take}, {!drop}, and {!split_at} cost at
      most two {e in total}, including every case that spans both arms, however many values they
      select. {!validate} costs at most four, two per nonempty interval. {!to_list}, {!to_seq},
      {!iter}, and {!fold_left} are [Theta (count)] and make no query.
    - {e The optimal backend.} An Alstrup-Holm incremental level-ancestor backend would give
      [U = Q = O(1)] worst case in linear space, which would make every scalar operation of this
      restricted algebra [O(1)] worst case. No such backend ships here. Backed by the shipped Myers
      arena, insertion is [O(1)] amortized, the [O(1)] operations above stay [O(1)] worst case, and
      every query-bearing operation is [O(log M)] worst case after [M] historical insertions.
      Nothing here upgrades an amortized or logarithmic bound to a worst-case constant one.

    The algebra is deliberately restricted: add or remove at either end, read either end or an
    indexed value, reverse, take, drop, slice, split, and enumerate front to back. Arbitrary
    concatenation of unrelated versions, point replacement, and middle editing are intentionally
    absent, and no comparison here may treat an omitted operation as fast. Arena space is charged to
    every successful historical end insertion, not merely to the values one handle can see: a handle
    retains its arena, and the arena retains every node it ever published.

    There is no process-global empty value: an empty deque belongs to an arena, and retaining it
    retains that arena. Handles are immutable, so every retained version stays readable however its
    successors are edited. The arena underneath them is the one deliberately mutable core, and it
    serializes its own operations, so one arena shared between handles is safe for concurrent use.

    {1 Divergences from the C# baseline}

    - Absent lookups return [option] and fallible operations return [(_, string) result], per this
      workspace's error convention, rather than raising. {!remove_first} and {!remove_last} return
      [option] because emptiness is their only failure mode; {!add_first} and {!add_last} return
      [result] because arena exhaustion is a genuine error a caller cannot predict from the handle.
    - The [TryRemoveFirst] and [TryRemoveLast] out-parameter pairs become {!pop_first} and
      {!pop_last}, which return the removed value together with the remaining deque. C# needs its
      [MaybeNullWhen] annotation only because [null] inhabits every reference type; [option] draws
      that distinction directly, so an element that is itself [None] stays visible.
    - C#'s [Create] rejects an arena whose bottom node is not at depth [-1]. {!of_arena} keeps that
      check and reports it as an error, but this workspace's only arena is
      {!Incremental_ancestor.create}, whose bottom node is at depth [-1] by construction, so the
      rejection is unreachable here rather than merely untaken.
    - Enumeration is exposed as {!to_list}, {!to_seq}, {!iter}, and {!fold_left} instead of an
      enumerator object. The reversed left interval streams through parent links; the forward right
      interval is buffered when it is first reached, so enumeration can retain [O(count)] temporary
      values, exactly as the baseline's enumerator does.
    - The internal [ValidateInvariants] audit becomes {!validate}, which returns the interval counts
      on success. Handles are abstract and this module is their only producer, so the audit reports
      on well-formed input rather than defending against externally corrupted input.
    - {!shares_storage_with} is added for the representation observations the C# tests make with
      reference identity. An OCaml handle is a value, so the equivalent observation is that two
      handles carry the same arena and the same interval descriptors. *)

type 'value t
(** One immutable deque version. Every operation returns a new handle and leaves the receiver, and
    every other retained handle, readable. A handle retains the arena it publishes into. *)

type statistics = {
  count : int;  (** Number of visible values, which equals [left_count + right_count]. *)
  left_count : int;  (** Number of values held by the reversed left interval. *)
  right_count : int;  (** Number of values held by the forward right interval. *)
}
(** Interval counts returned by a successful {!validate}. *)

val create : unit -> 'value t
(** An empty deque backed by a fresh, privately owned arena. Every version derived from the result
    shares that arena, and it becomes collectible once the last of them does. [O(1)]. *)

val of_arena : 'value Incremental_ancestor.t -> ('value t, string) result
(** [of_arena arena] is an empty deque owned by an explicit arena, which retains and navigates every
    inserted node and therefore fixes the bounds this deque inherits. Sharing one arena between
    independent deque families is allowed; the arena is append-only, so their nodes interleave
    harmlessly. Fails when the arena's bottom node is not at depth [-1], because the interval
    arithmetic assumes it is. [O(1)]. *)

val of_list : 'value list -> ('value t, string) result
(** A deque holding a list's values in list order, appended one at a time at the back into a fresh
    private arena. [Theta (n)] leaf additions and no level-ancestor query. Fails when the arena
    cannot publish another node. *)

val arena : 'value t -> 'value Incremental_ancestor.t
(** The arena this handle and every version derived from it publishes into. [O(1)]. *)

val count : 'value t -> int
(** Number of visible values. [O(1)]. *)

val is_empty : 'value t -> bool
(** Whether this handle denotes an empty deque. [O(1)]. *)

val first : 'value t -> 'value option
(** The first visible value, or [None] when the deque is empty. Reads one cached endpoint and makes
    no level-ancestor query, so it is [O(1)] even when the left arm is empty. *)

val last : 'value t -> 'value option
(** The last visible value, or [None] when the deque is empty. Reads one cached endpoint and makes
    no level-ancestor query, so it is [O(1)] even when the right arm is empty. *)

val nth : int -> 'value t -> 'value option
(** [nth index deque] is the value at the zero-based visible [index], or [None] when [index] is
    negative or not below {!count}. Costs at most one level-ancestor query; the four cached logical
    endpoints, namely visible index zero, the end of the left interval, the first right value, and
    the last visible index, are answered with no query at all. *)

val add_first : 'value -> 'value t -> ('value t, string) result
(** A deque with the value added at the front. Publishes exactly one arena leaf below the left tail
    and makes no level-ancestor query, so it costs one [U]. Fails when the visible count or the
    arena's ancestry depth cannot grow further. *)

val add_last : 'value -> 'value t -> ('value t, string) result
(** A deque with the value added at the back. Publishes exactly one arena leaf below the right tail
    and makes no level-ancestor query, so it costs one [U]. Fails when the visible count or the
    arena's ancestry depth cannot grow further. *)

val remove_first : 'value t -> 'value t option
(** The deque without its first value, or [None] when the deque is empty. Makes no level-ancestor
    query while the left arm is nonempty, and at most one when the removal crosses the centre into
    the right arm: dropping the last value and dropping the second-to-last both answer from cached
    endpoints. *)

val remove_last : 'value t -> 'value t option
(** The deque without its last value, or [None] when the deque is empty. Makes no level-ancestor
    query while the right arm is nonempty, and at most one when the removal crosses the centre into
    the left arm, under the same two cached shortcuts as {!remove_first}. *)

val pop_first : 'value t -> ('value * 'value t) option
(** The first value together with the deque remaining, or [None] when the deque is empty. Costs one
    {!first} and one {!remove_first}. *)

val pop_last : 'value t -> ('value * 'value t) option
(** The last value together with the deque remaining, or [None] when the deque is empty. Costs one
    {!last} and one {!remove_last}. *)

val take : int -> 'value t -> ('value t, string) result
(** [take count deque] is the first [count] values. A prefix is a slice, so it costs at most two
    level-ancestor queries; taking the whole deque returns a handle sharing this one's storage and
    costs none. Fails when [count] is negative or above {!count}. *)

val drop : int -> 'value t -> ('value t, string) result
(** [drop count deque] is the deque after omitting its first [count] values. A suffix is a slice, so
    it costs at most two level-ancestor queries; omitting nothing returns a handle sharing this
    one's storage and costs none. Fails when [count] is negative or above {!count}. *)

val slice : index:int -> count:int -> 'value t -> ('value t, string) result
(** [slice ~index ~count deque] is the contiguous run of [count] values starting at the zero-based
    visible [index]. Every contiguous slice meets each interval at most once, so the result is
    another bilateral handle and the operation costs {e at most two} level-ancestor queries however
    many values it selects, including when the run spans both arms. The whole-deque slice returns a
    handle sharing this one's storage and an empty slice returns a fresh empty handle; neither costs
    a query. Fails when [index] or [count] is negative, or when the pair reaches past the deque. *)

val split_at : int -> 'value t -> ('value t * 'value t, string) result
(** [split_at index deque] is the prefix holding the first [index] values paired with the suffix
    holding the rest; together they enumerate the original value. Although it is expressed as a
    prefix plus a suffix, the pair costs at most two level-ancestor queries in total. Splitting at
    either endpoint costs none and returns the receiver's storage on the nonempty side. Fails when
    [index] is negative or above {!count}. *)

val reverse : 'value t -> 'value t
(** The logical reversal, obtained by exchanging the two oriented interval descriptors. [O(1)] with
    no level-ancestor query, no lazy flag, and no rebuild. At a count of zero or one the two
    orientations coincide and the receiver's own storage is returned. *)

val clear : 'value t -> 'value t
(** An empty, independently appendable handle over the same arena. [O(1)] with no query. Clearing an
    already-empty deque returns the receiver's own storage. The arena keeps every node it published,
    so clearing reclaims nothing. *)

val to_list : 'value t -> 'value list
(** The visible values in logical order. [Theta (count)] parent links and no level-ancestor
    query. *)

val to_seq : 'value t -> 'value Seq.t
(** The visible values in logical order as a sequence. The reversed left interval streams through
    parent links with constant state; the forward right interval is buffered when it is first
    reached, so a full traversal is [Theta (count)] with no query but can retain [O(count)]
    temporary values. Abandoning a traversal inside the left interval avoids that buffer
    entirely. *)

val iter : ('value -> unit) -> 'value t -> unit
(** Applies the function to each value in logical order, with the streaming and buffering behaviour
    of {!to_seq}. [Theta (count)] and no query. *)

val fold_left : ('state -> 'value -> 'state) -> 'state -> 'value t -> 'state
(** Folds over the values from the front, with the streaming and buffering behaviour of {!to_seq}.
    [Theta (count)] and no query. *)

val shares_storage_with : 'value t -> 'value t -> bool
(** Whether both handles carry the same arena and the same two interval descriptors, so neither can
    denote a different sequence than the other. A representation test, not an equality test: two
    extensionally equal deques built by different routes need not share storage. [O(1)]. *)

val validate : 'value t -> (statistics, string) result
(** Audits this handle's cached interval endpoints and counts, and reports the interval counts. It
    compares the cached counts against the interval endpoint depths and confirms that each cached
    base and anchor lies on its tail's ancestry path. Confirming that path costs two level-ancestor
    queries per nonempty interval, so the audit is [O(1 + Q)] with a ceiling of four queries and is
    {e not} [O(1)] work; an empty handle spends none. For tests and diagnostics. *)
