(** Set-valued persistent hash multimap. *)

type ('key, 'value) entry = { key : 'key; value : 'value }
type ('key, 'value) t
(** A persistent multimap: each key bound to a persistent set of values, so keys whose values did
    not change are shared outright between versions. *)

val empty :
  key_policy:'key Common.Hash_policy.t ->
  value_policy:'value Common.Hash_policy.t ->
  ('key, 'value) t
(** The empty collection. *)

val key_policy : ('key, 'value) t -> 'key Common.Hash_policy.t
(** The retained key policy. *)

val value_policy : ('key, 'value) t -> 'value Common.Hash_policy.t
(** The retained value policy. *)

val key_count : ('key, 'value) t -> int
(** How many distinct keys are present. *)

val pair_count : ('key, 'value) t -> int
(** How many pairs are present in total. *)

val is_empty : ('key, 'value) t -> bool
(** Whether the collection holds no elements. *)

val contains_key : 'key -> ('key, 'value) t -> bool
(** Whether the key is present. *)

val contains : 'key -> 'value -> ('key, 'value) t -> bool
(** Whether the element is present. *)

val stored_key_opt : 'key -> ('key, 'value) t -> 'key option
(** The stored key representative, or [None] when absent. It need not be the key passed in. *)

val values : 'key -> ('key, 'value) t -> 'value Persistent_hash_set.t
(** The values. *)

val add : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
(** A collection containing the given element. *)

val remove : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
(** A collection without that element. *)

val remove_key : 'key -> ('key, 'value) t -> ('key, 'value) t
(** A collection without that key. *)

val to_list : ('key, 'value) t -> ('key, 'value) entry list
(** The elements, in the collection's own order. *)
