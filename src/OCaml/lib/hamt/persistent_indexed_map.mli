(** Persistent primary map with one maintained nonunique secondary index. *)

type ('key, 'value) entry = { key : 'key; value : 'value }
type ('key, 'value, 'index) t
(** A persistent map carrying a secondary index derived from its entries, so looking up by the
    derived key is a lookup rather than a scan. *)

val empty :
  ?value_equal:('value -> 'value -> bool) ->
  key_policy:'key Common.Hash_policy.t ->
  index_policy:'index Common.Hash_policy.t ->
  index_selector:('key -> 'value -> 'index) ->
  unit ->
  ('key, 'value, 'index) t
(** The empty collection. *)

val count : ('key, 'value, 'index) t -> int
(** Number of elements in the collection. *)

val index_count : ('key, 'value, 'index) t -> int
(** How many distinct secondary index keys are present. *)

val find_opt : 'key -> ('key, 'value, 'index) t -> 'value option
(** The value stored for the key, or [None] when absent. *)

val find_index_opt : 'key -> ('key, 'value, 'index) t -> 'index option
(** The secondary index key derived for a primary key, or [None] when absent. *)

val keys_for_index : 'index -> ('key, 'value, 'index) t -> 'key Persistent_hash_set.t
(** The primary keys carrying the given secondary index key. *)

val add : 'key -> 'value -> ('key, 'value, 'index) t -> ('key, 'value, 'index) t
(** A collection containing the given element. *)

val try_add : 'key -> 'value -> ('key, 'value, 'index) t -> ('key, 'value, 'index) t * bool
(** Adds the element unless an equivalent one is present, reporting which happened. *)

val set : 'key -> 'value -> ('key, 'value, 'index) t -> ('key, 'value, 'index) t
(** A collection with the key bound to the value, adding or replacing as needed. *)

val remove : 'key -> ('key, 'value, 'index) t -> ('key, 'value, 'index) t
(** A collection without that element. *)

val to_list : ('key, 'value, 'index) t -> ('key, 'value) entry list
(** The elements, in the collection's own order. *)
