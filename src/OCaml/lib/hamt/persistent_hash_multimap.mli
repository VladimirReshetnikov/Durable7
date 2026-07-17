(** Set-valued persistent hash multimap. *)

type ('key, 'value) entry = { key : 'key; value : 'value }
type ('key, 'value) t

val empty :
  key_policy:'key Common.Hash_policy.t ->
  value_policy:'value Common.Hash_policy.t ->
  ('key, 'value) t

val key_policy : ('key, 'value) t -> 'key Common.Hash_policy.t
val value_policy : ('key, 'value) t -> 'value Common.Hash_policy.t
val key_count : ('key, 'value) t -> int
val pair_count : ('key, 'value) t -> int
val is_empty : ('key, 'value) t -> bool
val contains_key : 'key -> ('key, 'value) t -> bool
val contains : 'key -> 'value -> ('key, 'value) t -> bool
val stored_key_opt : 'key -> ('key, 'value) t -> 'key option
val values : 'key -> ('key, 'value) t -> 'value Persistent_hash_set.t
val add : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
val remove : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
val remove_key : 'key -> ('key, 'value) t -> ('key, 'value) t
val to_list : ('key, 'value) t -> ('key, 'value) entry list
