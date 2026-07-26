(** Neutral grouped insertion-ordered persistent multimap. *)

type ('key, 'value) entry
type ('key, 'value) t
(** A persistent multimap that remembers insertion order, both across keys and within a key. *)

val empty :
  key_policy:'key Common.Hash_policy.t ->
  value_policy:'value Common.Hash_policy.t ->
  ('key, 'value) t
(** The empty collection. *)

val of_list :
  key_policy:'key Common.Hash_policy.t ->
  value_policy:'value Common.Hash_policy.t ->
  ('key * 'value) list ->
  ('key, 'value) t
(** A collection holding a list's elements, built in bulk rather than by repeated insertion. *)

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

val entry_key : ('key, 'value) entry -> 'key
(** The entry's key. *)

val entry_value : ('key, 'value) entry -> 'value
(** The entry's value. *)

val mem_key : 'key -> ('key, 'value) t -> bool
(** Whether the key is present. *)

val mem : 'key -> 'value -> ('key, 'value) t -> bool
(** Whether the element is present. *)

val find_key : 'key -> ('key, 'value) t -> 'key option
(** The key bound to the value. *)

val find_value : 'key -> 'value -> ('key, 'value) t -> 'value option
(** The value stored for the key. *)

val values : 'key -> ('key, 'value) t -> 'value Persistent_ordered_set.t
(** The values. *)

val add : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
(** A collection containing the given element. *)

val remove : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
(** A collection without that element. *)

val remove_key : 'key -> ('key, 'value) t -> int * ('key, 'value) t
(** A collection without that key. *)

val move_key_to : int -> 'key -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A map with the key moved to the given position in the insertion order. *)

val move_value_to :
  int -> key:'key -> value:'value -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A multimap with the value moved to the given position within its key's group. *)

val reverse_keys : ('key, 'value) t -> ('key, 'value) t
(** A map with its keys in the opposite insertion order. *)

val reverse_values : 'key -> ('key, 'value) t -> ('key, 'value) t
(** A multimap with each key's values in the opposite insertion order. *)

val entries : ('key, 'value) t -> ('key, 'value) entry list
(** The entries. *)
