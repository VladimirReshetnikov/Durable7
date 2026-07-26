(** Structurally shared persistent sequence with cached monoidal measurements. *)

type ('element, 'measure) t
type ('element, 'measure) cursor
(** An immutable gap cursor over one version. It holds that exact version, so it stays readable
    however the collection is edited afterwards. *)

val empty : ('element, 'measure) Measures.policy -> ('element, 'measure) t
(** The empty tree. *)

val singleton : ('element, 'measure) Measures.policy -> 'element -> ('element, 'measure) t
(** A tree holding one element. *)

val of_list : ('element, 'measure) Measures.policy -> 'element list -> ('element, 'measure) t
(** A tree holding a list's elements, built in bulk rather than by repeated insertion. *)

val policy : ('element, 'measure) t -> ('element, 'measure) Measures.policy
(** The policy the tree retains. *)

val is_empty : ('element, 'measure) t -> bool
(** Whether the tree holds no elements. *)

val length : ('element, 'measure) t -> int
(** Number of elements in the tree. *)

val measure : ('element, 'measure) t -> 'measure
(** The combined measure of every element, read from the cached root measure. *)

val to_list : ('element, 'measure) t -> 'element list
(** The elements, in the tree's own order. *)

val iter : ('element -> unit) -> ('element, 'measure) t -> unit
(** Applies the function to each element, in the tree's own order. *)

val fold_left : ('state -> 'element -> 'state) -> 'state -> ('element, 'measure) t -> 'state
(** Folds over the elements from the left. *)

val fold_right : ('element -> 'state -> 'state) -> ('element, 'measure) t -> 'state -> 'state
(** Folds over the elements from the right. *)

val cons : 'element -> ('element, 'measure) t -> ('element, 'measure) t
(** A tree with the element added at the front. *)

val snoc : ('element, 'measure) t -> 'element -> ('element, 'measure) t
(** A tree with the element added at the back. *)

val concat :
  ('element, 'measure) t -> ('element, 'measure) t -> (('element, 'measure) t, string) result
(** The concatenation of two trees, sharing both operands' unchanged structure. *)

val first : ('element, 'measure) t -> 'element option
(** The first element. *)

val last : ('element, 'measure) t -> 'element option
(** The last element. *)

val nth : int -> ('element, 'measure) t -> 'element option
(** The element at the given rank. *)

val split_at : int -> ('element, 'measure) t -> ('element, 'measure) t * ('element, 'measure) t
(** Splits into the elements before the position and those from it onward. *)

val take : int -> ('element, 'measure) t -> ('element, 'measure) t
(** The first n elements. *)

val drop : int -> ('element, 'measure) t -> ('element, 'measure) t
(** The elements after the first n. *)

val insert_at : int -> 'element -> ('element, 'measure) t -> (('element, 'measure) t, string) result
(** A tree with the element inserted at the position. *)

val update_at :
  int -> ('element -> 'element) -> ('element, 'measure) t -> (('element, 'measure) t, string) result
(** A tree with the element at the position replaced. *)

val remove_at : int -> ('element, 'measure) t -> ('element * ('element, 'measure) t, string) result
(** A tree without the element at the position. *)

val measure_range : int -> int -> ('element, 'measure) t -> ('measure, string) result
(** The combined measure of the elements in the range. *)

val locate : ('measure -> bool) -> ('element, 'measure) t -> (int * 'measure * 'element) option
(** [locate predicate tree] returns the first element whose inclusive prefix measure satisfies
    [predicate], together with its index and the exclusive-prefix measure. *)

val validate : ('element, 'measure) t -> (unit, string) result
(** Checks the tree's structural invariants. For tests and diagnostics. *)

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
