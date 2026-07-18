(** Persistent closed-interval collection ordered by low endpoint. *)

type 'endpoint interval
type 'endpoint t

val make_interval :
  'endpoint Common.Comparator.t -> 'endpoint -> 'endpoint -> ('endpoint interval, string) result

val low : 'endpoint interval -> 'endpoint
val high : 'endpoint interval -> 'endpoint
val overlaps : 'endpoint Common.Comparator.t -> 'endpoint interval -> 'endpoint interval -> bool
val contains_point : 'endpoint Common.Comparator.t -> 'endpoint -> 'endpoint interval -> bool
val empty : 'endpoint Common.Comparator.t -> 'endpoint t
val of_list : 'endpoint Common.Comparator.t -> 'endpoint interval list -> 'endpoint t
val comparator : 'endpoint t -> 'endpoint Common.Comparator.t
val count : 'endpoint t -> int
val is_empty : 'endpoint t -> bool
val maximum_high : 'endpoint t -> 'endpoint option
val nth : int -> 'endpoint t -> 'endpoint interval option
val lower_bound : 'endpoint -> 'endpoint t -> int
val upper_bound : 'endpoint -> 'endpoint t -> int
val insert : 'endpoint interval -> 'endpoint t -> 'endpoint t
val remove : 'endpoint interval -> 'endpoint t -> bool * 'endpoint t
val remove_at : int -> 'endpoint t -> ('endpoint t, string) result
val find_overlap : 'endpoint interval -> 'endpoint t -> 'endpoint interval option
val find_all_overlaps : 'endpoint interval -> 'endpoint t -> 'endpoint interval list
val query_point : 'endpoint -> 'endpoint t -> 'endpoint interval list
val to_list : 'endpoint t -> 'endpoint interval list
