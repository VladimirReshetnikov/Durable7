(** Persistent order-statistic sorted map with first-key representative retention. *)

type ('key, 'value) entry
type ('key, 'value) t

val empty : 'key Common.Comparator.t -> ('key, 'value) t
val of_list : 'key Common.Comparator.t -> ('key * 'value) list -> (('key, 'value) t, string) result
val comparator : ('key, 'value) t -> 'key Common.Comparator.t
val count : ('key, 'value) t -> int
val is_empty : ('key, 'value) t -> bool
val entry_key : ('key, 'value) entry -> 'key
val entry_value : ('key, 'value) entry -> 'value
val nth : int -> ('key, 'value) t -> ('key, 'value) entry option
val lower_bound : 'key -> ('key, 'value) t -> int
val upper_bound : 'key -> ('key, 'value) t -> int
val find_entry : 'key -> ('key, 'value) t -> ('key, 'value) entry option
val find_opt : 'key -> ('key, 'value) t -> 'value option
val mem : 'key -> ('key, 'value) t -> bool
val add : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
val set : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
val remove : 'key -> ('key, 'value) t -> ('value * ('key, 'value) t) option
val minimum : ('key, 'value) t -> ('key, 'value) entry option
val maximum : ('key, 'value) t -> ('key, 'value) entry option
val floor : 'key -> ('key, 'value) t -> ('key, 'value) entry option
val ceiling : 'key -> ('key, 'value) t -> ('key, 'value) entry option
val lower : 'key -> ('key, 'value) t -> ('key, 'value) entry option
val higher : 'key -> ('key, 'value) t -> ('key, 'value) entry option
val range : start:int -> count:int -> ('key, 'value) t -> (('key, 'value) t, string) result
val key_range : minimum:'key -> maximum:'key -> ('key, 'value) t -> ('key, 'value) t
val to_list : ('key, 'value) t -> ('key * 'value) list

module Builder : sig
  type ('key, 'value) map = ('key, 'value) t
  type ('key, 'value) t

  val create : ('key, 'value) map -> ('key, 'value) t
  val set : 'key -> 'value -> ('key, 'value) t -> bool
  val remove : 'key -> ('key, 'value) t -> 'value option
  val freeze : ('key, 'value) t -> ('key, 'value) map
end
