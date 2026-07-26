(** Strict persistent bidirectional map. *)

type ('key, 'value) conflict = Key_conflict | Value_conflict
type ('key, 'value) t
(** A persistent bidirectional map: a one-to-one correspondence indexed from both sides. An
    insertion that would break the correspondence is rejected rather than silently displacing a
    pair. *)

val empty :
  key_policy:'key Common.Hash_policy.t ->
  value_policy:'value Common.Hash_policy.t ->
  ('key, 'value) t
(** The empty collection. *)

val count : ('key, 'value) t -> int
(** Number of elements in the collection. *)

val find_value_opt : 'key -> ('key, 'value) t -> 'value option
(** The value stored for the key, or [None] when absent. *)

val find_key_opt : 'value -> ('key, 'value) t -> 'key option
(** The key bound to the value, or [None] when absent. *)

val try_add :
  'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, ('key, 'value) conflict) result
(** Adds the element unless an equivalent one is present, reporting which happened. *)

val add : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
(** A collection containing the given element. *)

val replace : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
(** A collection with the element replaced. *)

val remove_key : 'key -> ('key, 'value) t -> ('key, 'value) t
(** A collection without that key. *)

val remove_value : 'value -> ('key, 'value) t -> ('key, 'value) t
(** A collection without the pair holding that value. *)

val to_list : ('key, 'value) t -> ('key * 'value) list
(** The elements, in the collection's own order. *)

val inverse_list : ('key, 'value) t -> ('value * 'key) list
(** The pairs with their two sides exchanged. *)
