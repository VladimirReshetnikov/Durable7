(** Immutable snapshot-plus-position cursor for a generic rope. *)

type 'element t

val create : ?position:int -> 'element Rope.t -> ('element t, string) result
val rope : 'element t -> 'element Rope.t
val position : 'element t -> int
val move_to : int -> 'element t -> ('element t, string) result
val peek_before : 'element t -> 'element option
val peek_after : 'element t -> 'element option
val insert : 'element -> 'element t -> 'element t
val insert_many : 'element list -> 'element t -> 'element t
val delete_before : int -> 'element t -> ('element t, string) result
val delete_after : int -> 'element t -> ('element t, string) result
