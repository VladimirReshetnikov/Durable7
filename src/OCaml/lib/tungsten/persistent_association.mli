(** Application-specific Tungsten insertion-ordered Association. *)

type ('key, 'value) entry
type ('key, 'value) t

val empty : ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) t

val of_list :
  ?value_equal:('value -> 'value -> bool) ->
  'key Common.Hash_policy.t ->
  ('key * 'value) list ->
  ('key, 'value) t

val policy : ('key, 'value) t -> 'key Common.Hash_policy.t
val count : ('key, 'value) t -> int
val is_empty : ('key, 'value) t -> bool
val entry_key : ('key, 'value) entry -> 'key
val entry_value : ('key, 'value) entry -> 'value
val mem : 'key -> ('key, 'value) t -> bool
val find_entry : 'key -> ('key, 'value) t -> ('key, 'value) entry option
val find_opt : 'key -> ('key, 'value) t -> 'value option
val index_of : 'key -> ('key, 'value) t -> int option
val nth : int -> ('key, 'value) t -> ('key, 'value) entry option
val first : ('key, 'value) t -> ('key, 'value) entry option
val last : ('key, 'value) t -> ('key, 'value) entry option
val set : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
val set_items : ('key * 'value) list -> ('key, 'value) t -> ('key, 'value) t
val join : ('key, 'value) t -> ('key, 'value) t -> ('key, 'value) t
val append : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
val prepend : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
val insert : int -> 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
val remove : 'key -> ('key, 'value) t -> ('value * ('key, 'value) t) option
val remove_at : int -> ('key, 'value) t -> (('key, 'value) t, string) result
val key_take : 'key list -> ('key, 'value) t -> ('key, 'value) t
val range : start:int -> count:int -> ('key, 'value) t -> (('key, 'value) t, string) result
val reverse : ('key, 'value) t -> ('key, 'value) t
val key_sort : 'key Common.Comparator.t -> ('key, 'value) t -> ('key, 'value) t
val value_sort : 'value Common.Comparator.t -> ('key, 'value) t -> ('key, 'value) t
val keys : ('key, 'value) t -> 'key list
val values : ('key, 'value) t -> 'value list
val to_list : ('key, 'value) t -> ('key * 'value) list
