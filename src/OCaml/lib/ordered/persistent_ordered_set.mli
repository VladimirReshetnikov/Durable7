(** Neutral persistent insertion-ordered set with explicit positional movement. *)

type 'element t

val empty : 'element Common.Hash_policy.t -> 'element t
val of_list : 'element Common.Hash_policy.t -> 'element list -> 'element t
val policy : 'element t -> 'element Common.Hash_policy.t
val count : 'element t -> int
val is_empty : 'element t -> bool
val mem : 'element -> 'element t -> bool
val find : 'element -> 'element t -> 'element option
val nth : int -> 'element t -> 'element option
val index_of : 'element -> 'element t -> int option
val first : 'element t -> 'element option
val last : 'element t -> 'element option
val add : 'element -> 'element t -> bool * 'element t
val add_first : 'element -> 'element t -> bool * 'element t
val insert : int -> 'element -> 'element t -> (bool * 'element t, string) result
val move_to : int -> 'element -> 'element t -> ('element t, string) result
val move_to_first : 'element -> 'element t -> 'element t
val move_to_last : 'element -> 'element t -> 'element t
val remove : 'element -> 'element t -> bool * 'element t
val remove_at : int -> 'element t -> ('element * 'element t, string) result
val range : start:int -> count:int -> 'element t -> ('element t, string) result
val take : int -> 'element t -> ('element t, string) result
val drop : int -> 'element t -> ('element t, string) result
val reverse : 'element t -> 'element t
val sort : 'element Common.Comparator.t -> 'element t -> 'element t
val union : 'element t -> 'element list -> 'element t
val intersect : 'element t -> 'element list -> 'element t
val difference : 'element t -> 'element list -> 'element t
val symmetric_difference : 'element t -> 'element list -> 'element t
val to_list : 'element t -> 'element list
