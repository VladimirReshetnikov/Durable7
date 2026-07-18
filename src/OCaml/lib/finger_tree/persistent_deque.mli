(** Size-measured persistent double-ended sequence. *)

type 'element t
type 'element cursor

val empty : 'element t
val singleton : 'element -> 'element t
val of_list : 'element list -> 'element t
val is_empty : 'element t -> bool
val length : 'element t -> int
val to_list : 'element t -> 'element list
val cons : 'element -> 'element t -> 'element t
val snoc : 'element t -> 'element -> 'element t
val concat : 'element t -> 'element t -> 'element t
val first : 'element t -> 'element option
val last : 'element t -> 'element option
val pop_front : 'element t -> ('element * 'element t) option
val pop_back : 'element t -> ('element t * 'element) option
val nth : int -> 'element t -> 'element option
val split_at : int -> 'element t -> 'element t * 'element t
val take : int -> 'element t -> 'element t
val drop : int -> 'element t -> 'element t
val slice : start:int -> length:int -> 'element t -> ('element t, string) result
val insert_at : int -> 'element -> 'element t -> ('element t, string) result
val update_at : int -> ('element -> 'element) -> 'element t -> ('element t, string) result
val remove_at : int -> 'element t -> ('element * 'element t, string) result
val iter : ('element -> unit) -> 'element t -> unit
val fold_left : ('state -> 'element -> 'state) -> 'state -> 'element t -> 'state
val validate : 'element t -> (unit, string) result
val cursor : 'element t -> 'element cursor
val cursor_at : int -> 'element t -> 'element cursor option
val cursor_position : 'element cursor -> int
val cursor_length : 'element cursor -> int
val cursor_is_at_start : 'element cursor -> bool
val cursor_is_at_end : 'element cursor -> bool
val cursor_peek_previous : 'element cursor -> 'element option
val cursor_peek_next : 'element cursor -> 'element option
val cursor_move_previous : 'element cursor -> 'element cursor option
val cursor_move_next : 'element cursor -> 'element cursor option
val cursor_seek : int -> 'element cursor -> 'element cursor option
val cursor_insert : 'element -> 'element cursor -> 'element cursor
val cursor_insert_many : 'element list -> 'element cursor -> 'element cursor
val cursor_delete_previous : 'element cursor -> 'element cursor option
val cursor_delete_next : 'element cursor -> 'element cursor option
val cursor_replace_next : 'element -> 'element cursor -> 'element cursor option
val cursor_snapshot : 'element cursor -> 'element t
