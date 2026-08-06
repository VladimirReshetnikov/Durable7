(** Persistent monoid-measured Hinze–Paterson finger tree with memoized lazy spines.

    1–4-item digits sit at both ends of every level, 2-3 nodes below them, and each level's middle
    subtree is held behind a memoized suspension — the paper's lazy tree realized in a strict
    language, as the reference workspaces realize it. Element measures and every 2-3 node's size,
    measure, and end elements are strict; only the digit-overflow push into the middle and the
    concatenation of two middles are deferred. The suspensions are defunctionalized and forced by
    an explicit-stack interpreter, because organic endpoint loops build Θ(n)-deep pending chains
    that any recursive force — the standard library's [Lazy.force] included — would overflow on.

    With O(1) policy operations: [first]/[last] are O(1) worst-case digit reads; [cons]/[snoc] are
    O(1) amortized and O(log n) worst-case; [concat] is O(log(min(n, m))) amortized; [reverse] is
    O(1); [split_at], [nth], [insert_at], [update_at], [remove_at], [measure_range], and [locate]
    are O(log n); iteration is O(n). The amortized bounds hold under fully persistent branching
    histories because a forced suspension memoizes its result in a cell shared by every version
    that references it.

    Concurrency posture: suspension memoization publishes by a plain mutable-field write, which is
    safe under OCaml 4.14's single runtime lock — and unlike the incremental-ancestor arena, which
    guards a genuinely mutable collection with a mutex, a suspension write is write-once and
    idempotent: racing forces would only ever install equivalent results, so no lock is needed. A
    deferred computation that raises publishes nothing and may be retried, so a failing measure
    policy keeps every retained version valid. *)

type ('element, 'measure) t
type ('element, 'measure) cursor
(** An immutable gap cursor over one version. It holds that exact version, so it stays readable
    however the collection is edited afterwards. *)

val empty : ('element, 'measure) Measures.policy -> ('element, 'measure) t
(** The empty tree. *)

val singleton : ('element, 'measure) Measures.policy -> 'element -> ('element, 'measure) t
(** A tree holding one element. *)

val of_list : ('element, 'measure) Measures.policy -> 'element list -> ('element, 'measure) t
(** A tree holding a list's elements, built in one eager bottom-up O(n) pass that defers nothing. *)

val policy : ('element, 'measure) t -> ('element, 'measure) Measures.policy
(** The policy the tree retains. *)

val is_empty : ('element, 'measure) t -> bool
(** Whether the tree holds no elements. *)

val length : ('element, 'measure) t -> int
(** Number of elements in the tree. O(1): sizes are strict at every node, so no count ever forces
    a suspension. *)

val measure : ('element, 'measure) t -> 'measure
(** The combined measure of every element. O(1) amortized: the total is memoized at each deep
    node, and the first read of a fresh spine may force a chain of suspended middles before
    memoizing. *)

val to_list : ('element, 'measure) t -> 'element list
(** The elements, in the tree's own order. *)

val iter : ('element -> unit) -> ('element, 'measure) t -> unit
(** Applies the function to each element, in the tree's own order. *)

val fold_left : ('state -> 'element -> 'state) -> 'state -> ('element, 'measure) t -> 'state
(** Folds over the elements from the left. *)

val fold_right : ('element -> 'state -> 'state) -> ('element, 'measure) t -> 'state -> 'state
(** Folds over the elements from the right. *)

val cons : 'element -> ('element, 'measure) t -> ('element, 'measure) t
(** A tree with the element added at the front. O(1) amortized, O(log n) worst-case: a full digit
    defers its overflow push into the middle. *)

val snoc : ('element, 'measure) t -> 'element -> ('element, 'measure) t
(** A tree with the element added at the back. O(1) amortized, O(log n) worst-case. *)

val concat :
  ('element, 'measure) t -> ('element, 'measure) t -> (('element, 'measure) t, string) result
(** The concatenation of two trees, sharing both operands' unchanged structure. O(log(min(n, m)))
    amortized: the recursion into the two middles is deferred, and the digits between them are
    regrouped into 2-3 nodes. *)

val reverse : ('element, 'measure) t -> ('element, 'measure) t
(** The tree in the opposite order, sharing storage lazily. O(1): digits mirror eagerly and the
    middle's reversal rides a suspension. Correct under every monoid, commutative or not — a
    mirrored node's summary is recombined in the mirrored order rather than reused, at O(1)
    combines per node paid only when the reversal suspension forces. *)

val first : ('element, 'measure) t -> 'element option
(** The first element. O(1) worst-case: a digit read that never forces the middle. *)

val last : ('element, 'measure) t -> 'element option
(** The last element. O(1) worst-case. *)

val nth : int -> ('element, 'measure) t -> 'element option
(** The element at the given rank. O(log n): descends by strict cached sizes, forcing only the
    middles it actually enters. *)

val split_at : int -> ('element, 'measure) t -> ('element, 'measure) t * ('element, 'measure) t
(** Splits into the elements before the position and those from it onward. O(log n). *)

val take : int -> ('element, 'measure) t -> ('element, 'measure) t
(** The first n elements. *)

val drop : int -> ('element, 'measure) t -> ('element, 'measure) t
(** The elements after the first n. *)

val insert_at : int -> 'element -> ('element, 'measure) t -> (('element, 'measure) t, string) result
(** A tree with the element inserted at the position. O(log n). *)

val update_at :
  int -> ('element -> 'element) -> ('element, 'measure) t -> (('element, 'measure) t, string) result
(** A tree with the element at the position replaced. O(log n). *)

val remove_at : int -> ('element, 'measure) t -> ('element * ('element, 'measure) t, string) result
(** A tree without the element at the position. O(log n). *)

val measure_range : int -> int -> ('element, 'measure) t -> ('measure, string) result
(** The combined measure of the elements in the range. O(log n). *)

val locate : ('measure -> bool) -> ('element, 'measure) t -> (int * 'measure * 'element) option
(** [locate predicate tree] returns the first element whose inclusive prefix measure satisfies
    [predicate], together with its index and the exclusive-prefix measure. O(log n): cached node
    measures let the descent skip the elements it passes over. The predicate is expected to be
    monotone; a non-monotone predicate gives an unspecified but still valid result. *)

val validate : ('element, 'measure) t -> (unit, string) result
(** Checks the tree's structural invariants: digit widths, 2-3 node arity, and every cached size
    and end-element handle. Forces every middle it meets — deferred structure hides inside pending
    suspensions where no non-forcing walk can check it. For tests and diagnostics. *)

val cursor_at_start : ('element, 'measure) t -> ('element, 'measure) cursor
(** Whether the gap precedes the first element. *)

val cursor_at_end : ('element, 'measure) t -> ('element, 'measure) cursor
(** Whether the gap follows the last element. *)

val cursor_by_measure :
  ('measure -> bool) -> ('element, 'measure) t -> bool * ('element, 'measure) cursor
(** A cursor at the first gap where the measure satisfies the predicate. *)

val cursor_is_at_start : ('element, 'measure) cursor -> bool
(** Whether the gap precedes the first element. *)

val cursor_is_at_end : ('element, 'measure) cursor -> bool
(** Whether the gap follows the last element. *)

val cursor_measure_before : ('element, 'measure) cursor -> 'measure
(** The combined measure of everything before the gap. *)

val cursor_measure_after : ('element, 'measure) cursor -> 'measure
(** The combined measure of everything after the gap. *)

val cursor_peek_previous : ('element, 'measure) cursor -> 'element option
(** The element immediately before the gap, or [None] at the start. *)

val cursor_peek_next : ('element, 'measure) cursor -> 'element option
(** The element immediately after the gap, or [None] at the end. *)

val cursor_move_previous : ('element, 'measure) cursor -> ('element, 'measure) cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val cursor_move_next : ('element, 'measure) cursor -> ('element, 'measure) cursor option
(** A cursor one position later. The receiver is unchanged. *)

val cursor_seek_by_measure :
  ('measure -> bool) -> ('element, 'measure) cursor -> bool * ('element, 'measure) cursor
(** A cursor at the first gap where the measure satisfies the predicate. *)

val cursor_insert : 'element -> ('element, 'measure) cursor -> ('element, 'measure) cursor
(** A tree containing the given element. *)

val cursor_delete_previous : ('element, 'measure) cursor -> ('element, 'measure) cursor option
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_delete_next : ('element, 'measure) cursor -> ('element, 'measure) cursor option
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_replace_next :
  'element -> ('element, 'measure) cursor -> ('element, 'measure) cursor option
(** Replaces the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_snapshot : ('element, 'measure) cursor -> ('element, 'measure) t
(** The tree version this cursor is positioned in. *)
