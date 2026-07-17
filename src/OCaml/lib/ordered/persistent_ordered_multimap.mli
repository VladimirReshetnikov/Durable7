(** Neutral grouped insertion-ordered persistent multimap. *)

type ('key, 'value) entry
type ('key, 'value) t

val empty :
  key_policy:'key Common.Hash_policy.t ->
  value_policy:'value Common.Hash_policy.t ->
  ('key, 'value) t

val of_list :
  key_policy:'key Common.Hash_policy.t ->
  value_policy:'value Common.Hash_policy.t ->
  ('key * 'value) list ->
  ('key, 'value) t

val key_policy : ('key, 'value) t -> 'key Common.Hash_policy.t
val value_policy : ('key, 'value) t -> 'value Common.Hash_policy.t
val key_count : ('key, 'value) t -> int
val pair_count : ('key, 'value) t -> int
val is_empty : ('key, 'value) t -> bool
val entry_key : ('key, 'value) entry -> 'key
val entry_value : ('key, 'value) entry -> 'value
val mem_key : 'key -> ('key, 'value) t -> bool
val mem : 'key -> 'value -> ('key, 'value) t -> bool
val find_key : 'key -> ('key, 'value) t -> 'key option
val find_value : 'key -> 'value -> ('key, 'value) t -> 'value option
val values : 'key -> ('key, 'value) t -> 'value Persistent_ordered_set.t
val add : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
val remove : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
val remove_key : 'key -> ('key, 'value) t -> int * ('key, 'value) t
val move_key_to : int -> 'key -> ('key, 'value) t -> (('key, 'value) t, string) result

val move_value_to :
  int -> key:'key -> value:'value -> ('key, 'value) t -> (('key, 'value) t, string) result

val reverse_keys : ('key, 'value) t -> ('key, 'value) t
val reverse_values : 'key -> ('key, 'value) t -> ('key, 'value) t
val entries : ('key, 'value) t -> ('key, 'value) entry list
