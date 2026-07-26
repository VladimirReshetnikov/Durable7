(** Monoid-measured persistent rope and snapshot cursor. *)

type ('element, 'measure) t
type ('element, 'measure) cursor
(** An immutable gap cursor over one version. It holds that exact version, so it stays readable
    however the collection is edited afterwards. *)

val empty : ('element, 'measure) Measures.policy -> ('element, 'measure) t
(** The empty rope. *)

val of_list : ('element, 'measure) Measures.policy -> 'element list -> ('element, 'measure) t
(** A rope holding a list's elements, built in bulk rather than by repeated insertion. *)

val length : ('element, 'measure) t -> int
(** Number of elements in the rope. *)

val measure : ('element, 'measure) t -> 'measure
(** The combined measure of every element, read from the cached root measure. *)

val nth : int -> ('element, 'measure) t -> 'element option
(** The element at the given rank. *)

val to_list : ('element, 'measure) t -> 'element list
(** The elements, in the rope's own order. *)

val concat :
  ('element, 'measure) t -> ('element, 'measure) t -> (('element, 'measure) t, string) result
(** The concatenation of two ropes, sharing both operands' unchanged structure. *)

val split_at : int -> ('element, 'measure) t -> ('element, 'measure) t * ('element, 'measure) t
(** Splits into the elements before the position and those from it onward. *)

val locate : ('measure -> bool) -> ('element, 'measure) t -> (int * 'measure * 'element) option
(** Where a measured search lands. *)

val create_cursor :
  ?position:int -> ('element, 'measure) t -> (('element, 'measure) cursor, string) result
(** A cursor over the given rope version. *)

val cursor_rope : ('element, 'measure) cursor -> ('element, 'measure) t
(** The rope version this cursor is positioned in. *)

val cursor_position : ('element, 'measure) cursor -> int
(** The cursor's gap position. *)

val cursor_move_to :
  int -> ('element, 'measure) cursor -> (('element, 'measure) cursor, string) result
(** A rope with the element moved to the given position in the insertion order. *)

val cursor_measure_before : ('element, 'measure) cursor -> 'measure
(** The combined measure of everything before the gap. *)

val cursor_insert : 'element -> ('element, 'measure) cursor -> ('element, 'measure) cursor
(** A rope containing the given element. *)

val cursor_delete_after :
  int -> ('element, 'measure) cursor -> (('element, 'measure) cursor, string) result
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)
