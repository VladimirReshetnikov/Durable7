(** Persistent deque with an O(1) logical reversal bit. *)

type 'element t

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
