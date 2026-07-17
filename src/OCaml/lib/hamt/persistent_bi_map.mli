(** Strict persistent bidirectional map. *)

type ('key, 'value) conflict = Key_conflict | Value_conflict
type ('key, 'value) t

val empty :
  key_policy:'key Common.Hash_policy.t ->
  value_policy:'value Common.Hash_policy.t ->
  ('key, 'value) t

val count : ('key, 'value) t -> int
val find_value_opt : 'key -> ('key, 'value) t -> 'value option
val find_key_opt : 'value -> ('key, 'value) t -> 'key option

val try_add :
  'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, ('key, 'value) conflict) result

val add : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
val replace : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
val remove_key : 'key -> ('key, 'value) t -> ('key, 'value) t
val remove_value : 'value -> ('key, 'value) t -> ('key, 'value) t
val to_list : ('key, 'value) t -> ('key * 'value) list
val inverse_list : ('key, 'value) t -> ('value * 'key) list
