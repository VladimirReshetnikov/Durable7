(** Persistent order-statistic sorted multiset. *)

type 'element t

val empty : 'element Common.Comparator.t -> 'element t
val of_list : 'element Common.Comparator.t -> 'element list -> 'element t
val comparator : 'element t -> 'element Common.Comparator.t
val count : 'element t -> int
val is_empty : 'element t -> bool
val minimum : 'element t -> 'element option
val maximum : 'element t -> 'element option
val nth : int -> 'element t -> 'element option
val count_less_than : 'element -> 'element t -> int
val count_at_most : 'element -> 'element t -> int
val count_of : 'element -> 'element t -> int
val mem : 'element -> 'element t -> bool
val add : 'element -> 'element t -> 'element t
val remove : 'element -> 'element t -> 'element t
val remove_at : int -> 'element t -> ('element t, string) result
val remove_all : 'element -> 'element t -> 'element t
val range : start:int -> count:int -> 'element t -> ('element t, string) result
val value_range : minimum:'element -> maximum:'element -> 'element t -> 'element t
val to_list : 'element t -> 'element list
