(** Persistent deque with an O(1) logical reversal bit. *)

type 'element t
type 'element cursor

val empty : 'element t
val of_list : 'element list -> 'element t
val length : 'element t -> int
val is_empty : 'element t -> bool
val reverse : 'element t -> 'element t
val cons : 'element -> 'element t -> 'element t
val snoc : 'element t -> 'element -> 'element t
val first : 'element t -> 'element option
val last : 'element t -> 'element option
val pop_front : 'element t -> ('element * 'element t) option
val pop_back : 'element t -> ('element t * 'element) option
val nth : int -> 'element t -> 'element option
val to_list : 'element t -> 'element list
val concat : 'element t -> 'element t -> 'element t
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
val cursor_reverse : 'element cursor -> 'element cursor
val cursor_snapshot : 'element cursor -> 'element t
