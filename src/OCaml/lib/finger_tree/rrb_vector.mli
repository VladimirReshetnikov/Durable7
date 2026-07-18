(** Persistent indexed vector facade over the shared balanced sequence. *)

type 'element t
type 'element cursor
type statistics = { count : int; estimated_leaves : int; depth : int }

val empty : 'element t
val singleton : 'element -> 'element t
val of_list : 'element list -> 'element t
val length : 'element t -> int
val is_empty : 'element t -> bool
val nth : int -> 'element t -> 'element option
val set : int -> 'element -> 'element t -> ('element t, string) result
val append : 'element -> 'element t -> 'element t
val prepend : 'element -> 'element t -> 'element t
val concat : 'element t -> 'element t -> 'element t
val insert : int -> 'element -> 'element t -> ('element t, string) result
val remove : int -> 'element t -> ('element * 'element t, string) result
val pop : 'element t -> ('element t * 'element) option
val split_at : int -> 'element t -> 'element t * 'element t
val slice : start:int -> length:int -> 'element t -> ('element t, string) result
val to_list : 'element t -> 'element list
val statistics : 'element t -> statistics
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
val cursor_insert_vector : 'element t -> 'element cursor -> 'element cursor
val cursor_delete_previous : 'element cursor -> 'element cursor option
val cursor_delete_next : 'element cursor -> 'element cursor option
val cursor_replace_next : 'element -> 'element cursor -> 'element cursor option
val cursor_snapshot : 'element cursor -> 'element t

module Builder : sig
  type 'element vector = 'element t
  type 'element t

  val create : 'element vector -> 'element t
  val length : 'element t -> int
  val append : 'element -> 'element t -> unit
  val set : int -> 'element -> 'element t -> (unit, string) result
  val remove : int -> 'element t -> ('element, string) result
  val freeze : 'element t -> 'element vector
end
