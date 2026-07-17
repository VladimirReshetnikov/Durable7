(** Monoid-measured persistent rope and snapshot cursor. *)

type ('element, 'measure) t
type ('element, 'measure) cursor

val empty : ('element, 'measure) Measures.policy -> ('element, 'measure) t
val of_list : ('element, 'measure) Measures.policy -> 'element list -> ('element, 'measure) t
val length : ('element, 'measure) t -> int
val measure : ('element, 'measure) t -> 'measure
val nth : int -> ('element, 'measure) t -> 'element option
val to_list : ('element, 'measure) t -> 'element list

val concat :
  ('element, 'measure) t -> ('element, 'measure) t -> (('element, 'measure) t, string) result

val split_at : int -> ('element, 'measure) t -> ('element, 'measure) t * ('element, 'measure) t
val locate : ('measure -> bool) -> ('element, 'measure) t -> (int * 'measure * 'element) option

val create_cursor :
  ?position:int -> ('element, 'measure) t -> (('element, 'measure) cursor, string) result

val cursor_rope : ('element, 'measure) cursor -> ('element, 'measure) t
val cursor_position : ('element, 'measure) cursor -> int

val cursor_move_to :
  int -> ('element, 'measure) cursor -> (('element, 'measure) cursor, string) result

val cursor_measure_before : ('element, 'measure) cursor -> 'measure
val cursor_insert : 'element -> ('element, 'measure) cursor -> ('element, 'measure) cursor

val cursor_delete_after :
  int -> ('element, 'measure) cursor -> (('element, 'measure) cursor, string) result
