(** Application-specific immutable Tungsten [List] vocabulary. *)

type 'element t

val empty : 'element t
val of_list : 'element list -> 'element t
val length : 'element t -> int
val is_empty : 'element t -> bool
val first : 'element t -> 'element option
val last : 'element t -> 'element option
val nth : int -> 'element t -> 'element option
val append : 'element -> 'element t -> 'element t
val prepend : 'element -> 'element t -> 'element t
val join : 'element t -> 'element t -> 'element t
val insert : int -> 'element -> 'element t -> ('element t, string) result
val insert_range : int -> 'element list -> 'element t -> ('element t, string) result
val remove_at : int -> 'element t -> ('element t, string) result
val remove_range : start:int -> count:int -> 'element t -> ('element t, string) result
val set : int -> 'element -> 'element t -> ('element t, string) result
val update : int -> ('element -> 'element) -> 'element t -> ('element t, string) result
val range : start:int -> count:int -> 'element t -> ('element t, string) result
val take : int -> 'element t -> ('element t, string) result
val take_last : int -> 'element t -> ('element t, string) result
val drop : int -> 'element t -> ('element t, string) result
val drop_last : int -> 'element t -> ('element t, string) result
val split_at : int -> 'element t -> ('element t * 'element t, string) result
val reverse : 'element t -> 'element t
val map : ('element -> int -> 'result) -> 'element t -> 'result t
val index_of : ?equal:('element -> 'element -> bool) -> 'element -> 'element t -> int option
val mem : ?equal:('element -> 'element -> bool) -> 'element -> 'element t -> bool
val to_list : 'element t -> 'element list
