(** Persistent primary map with one maintained nonunique secondary index. *)

type ('key, 'value) entry = { key : 'key; value : 'value }
type ('key, 'value, 'index) t

val empty :
  ?value_equal:('value -> 'value -> bool) ->
  key_policy:'key Common.Hash_policy.t ->
  index_policy:'index Common.Hash_policy.t ->
  index_selector:('key -> 'value -> 'index) ->
  unit ->
  ('key, 'value, 'index) t

val count : ('key, 'value, 'index) t -> int
val index_count : ('key, 'value, 'index) t -> int
val find_opt : 'key -> ('key, 'value, 'index) t -> 'value option
val find_index_opt : 'key -> ('key, 'value, 'index) t -> 'index option
val keys_for_index : 'index -> ('key, 'value, 'index) t -> 'key Persistent_hash_set.t
val add : 'key -> 'value -> ('key, 'value, 'index) t -> ('key, 'value, 'index) t
val try_add : 'key -> 'value -> ('key, 'value, 'index) t -> ('key, 'value, 'index) t * bool
val set : 'key -> 'value -> ('key, 'value, 'index) t -> ('key, 'value, 'index) t
val remove : 'key -> ('key, 'value, 'index) t -> ('key, 'value, 'index) t
val to_list : ('key, 'value, 'index) t -> ('key, 'value) entry list
