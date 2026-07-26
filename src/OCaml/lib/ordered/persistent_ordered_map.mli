(** Neutral persistent insertion-ordered map with retained key representatives. *)

type ('key, 'value) entry
type ('key, 'value) t
(** A persistent map that remembers insertion order. Replacing an entry's value leaves its position
    alone. *)

val empty : ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) t
(** The empty collection. *)

val of_list :
  ?value_equal:('value -> 'value -> bool) ->
  'key Common.Hash_policy.t ->
  ('key * 'value) list ->
  ('key, 'value) t
(** A collection holding a list's elements, built in bulk rather than by repeated insertion. *)

val key_policy : ('key, 'value) t -> 'key Common.Hash_policy.t
(** The retained key policy. *)

val count : ('key, 'value) t -> int
(** Number of elements in the collection. *)

val is_empty : ('key, 'value) t -> bool
(** Whether the collection holds no elements. *)

val entry_key : ('key, 'value) entry -> 'key
(** The entry's key. *)

val entry_value : ('key, 'value) entry -> 'value
(** The entry's value. *)

val find_entry : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The entry stored for the key. *)

val find_opt : 'key -> ('key, 'value) t -> 'value option
(** The value stored for the key, or [None] when absent. *)

val mem : 'key -> ('key, 'value) t -> bool
(** Whether the element is present. *)

val nth : int -> ('key, 'value) t -> ('key, 'value) entry option
(** The element at the given rank. *)

val index_of : 'key -> ('key, 'value) t -> int option
(** The rank of the given element. *)

val first : ('key, 'value) t -> ('key, 'value) entry option
(** The first element. *)

val last : ('key, 'value) t -> ('key, 'value) entry option
(** The last element. *)

val add : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A collection containing the given element. *)

val insert : int -> 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A collection containing the given element. *)

val set : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
(** A collection with the key bound to the value, adding or replacing as needed. *)

val remove : 'key -> ('key, 'value) t -> (('key, 'value) entry * ('key, 'value) t) option
(** A collection without that element. *)

val remove_at : int -> ('key, 'value) t -> (('key, 'value) entry * ('key, 'value) t, string) result
(** A collection without the element at the position. *)

val move_to : int -> 'key -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A collection with the element moved to the given position in the insertion order. *)

val move_to_first : 'key -> ('key, 'value) t -> ('key, 'value) t
(** A collection with the element moved to the front of the insertion order. *)

val move_to_last : 'key -> ('key, 'value) t -> ('key, 'value) t
(** A collection with the element moved to the end of the insertion order. *)

val range : start:int -> count:int -> ('key, 'value) t -> (('key, 'value) t, string) result
(** The elements in the given range. *)

val reverse : ('key, 'value) t -> ('key, 'value) t
(** The collection in the opposite order. *)

val sort_by_key : 'key Common.Comparator.t -> ('key, 'value) t -> ('key, 'value) t
(** The entries ordered by key. Stable, so equal keys keep their insertion order. *)

val to_list : ('key, 'value) t -> ('key * 'value) list
(** The elements, in the collection's own order. *)
