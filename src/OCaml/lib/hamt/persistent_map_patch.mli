(** Strict presence-aware persistent map patches. *)

type 'value state = Absent | Present of 'value
type ('key, 'value) entry = { patch_key : 'key; before : 'value state; after : 'value state }

type ('key, 'value) conflict = {
  conflict_key : 'key;
  expected : 'value state;
  actual : 'value state;
}

type ('key, 'value) composition_conflict = {
  composition_key : 'key;
  left_after : 'value state;
  right_before : 'value state;
}

type ('key, 'value) t

val empty : ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) t

val of_list :
  ?value_equal:('value -> 'value -> bool) ->
  'key Common.Hash_policy.t ->
  ('key, 'value) entry list ->
  ('key, 'value) t

val between :
  ?value_equal:('value -> 'value -> bool) ->
  ('key, 'value) Persistent_hamt.map ->
  ('key, 'value) Persistent_hamt.map ->
  ('key, 'value) t

val count : ('key, 'value) t -> int
val to_list : ('key, 'value) t -> ('key, 'value) entry list

val apply :
  ('key, 'value) t ->
  ('key, 'value) Persistent_hamt.map ->
  (('key, 'value) Persistent_hamt.map, ('key, 'value) conflict) result

val invert : ('key, 'value) t -> ('key, 'value) t

val compose :
  ('key, 'value) t ->
  ('key, 'value) t ->
  (('key, 'value) t, ('key, 'value) composition_conflict) result
