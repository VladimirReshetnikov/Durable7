(** Neutral persistent insertion-ordered map with retained key representatives. *)

type ('key, 'value) entry
type ('key, 'value) t

val empty : ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) t

val of_list :
  ?value_equal:('value -> 'value -> bool) ->
  'key Common.Hash_policy.t ->
  ('key * 'value) list ->
  ('key, 'value) t

val key_policy : ('key, 'value) t -> 'key Common.Hash_policy.t
val count : ('key, 'value) t -> int
val is_empty : ('key, 'value) t -> bool
val entry_key : ('key, 'value) entry -> 'key
val entry_value : ('key, 'value) entry -> 'value
val find_entry : 'key -> ('key, 'value) t -> ('key, 'value) entry option
val find_opt : 'key -> ('key, 'value) t -> 'value option
val mem : 'key -> ('key, 'value) t -> bool
val nth : int -> ('key, 'value) t -> ('key, 'value) entry option
val index_of : 'key -> ('key, 'value) t -> int option
val first : ('key, 'value) t -> ('key, 'value) entry option
val last : ('key, 'value) t -> ('key, 'value) entry option
val add : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
val insert : int -> 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
val set : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
val remove : 'key -> ('key, 'value) t -> (('key, 'value) entry * ('key, 'value) t) option
val remove_at : int -> ('key, 'value) t -> (('key, 'value) entry * ('key, 'value) t, string) result
val move_to : int -> 'key -> ('key, 'value) t -> (('key, 'value) t, string) result
val move_to_first : 'key -> ('key, 'value) t -> ('key, 'value) t
val move_to_last : 'key -> ('key, 'value) t -> ('key, 'value) t
val range : start:int -> count:int -> ('key, 'value) t -> (('key, 'value) t, string) result
val reverse : ('key, 'value) t -> ('key, 'value) t
val sort_by_key : 'key Common.Comparator.t -> ('key, 'value) t -> ('key, 'value) t
val to_list : ('key, 'value) t -> ('key * 'value) list
