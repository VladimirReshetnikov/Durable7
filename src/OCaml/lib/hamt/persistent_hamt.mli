(** Persistent 32-way hash-array mapped trie. *)

exception Duplicate_key
exception Transient_consumed
(** Raised when a spent mutable session is used again. A session is single-use, so this catches a
    stale handle rather than letting it read a version it does not own. *)

type ('key, 'value) entry = { key : 'key; value : 'value }
(** One stored pair as the collection presents it. *)

type difference_kind = Only_left | Only_right | Different
(** Which side of a diff an entry falls on. *)

type ('key, 'value) difference = {
  kind : difference_kind;
  difference_key : 'key;
  left_value : 'value option;
  right_value : 'value option;
(** One entry-level difference between two maps. *)

}

type ('key, 'value) map
(** A persistent hash map backed by a CHAMP trie. Branching is 32-way and bitmap-indexed with
    immutable collision buckets, so operations are constant time in expectation while versions share
    every unchanged node. *)

val empty :
  ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) map
(** The empty collection. *)

val singleton :
  ?value_equal:('value -> 'value -> bool) ->
  'key Common.Hash_policy.t ->
  'key ->
  'value ->
  ('key, 'value) map
(** A collection holding one element. *)

val policy : ('key, 'value) map -> 'key Common.Hash_policy.t
(** The policy the collection retains. *)

val entry_key : ('key, 'value) entry -> 'key
(** The entry's key. *)

val entry_value : ('key, 'value) entry -> 'value
(** The entry's value. *)

val count : ('key, 'value) map -> int
(** Number of elements in the collection. *)

val is_empty : ('key, 'value) map -> bool
(** Whether the collection holds no elements. *)

val find_entry_opt : 'key -> ('key, 'value) map -> ('key, 'value) entry option
(** The entry stored for the key, or [None] when absent. *)

val find_opt : 'key -> ('key, 'value) map -> 'value option
(** The value stored for the key, or [None] when absent. *)

val mem : 'key -> ('key, 'value) map -> bool
(** Whether the element is present. *)

val add : 'key -> 'value -> ('key, 'value) map -> ('key, 'value) map
(** A collection containing the given element. *)

val try_add : 'key -> 'value -> ('key, 'value) map -> ('key, 'value) map * bool
(** Adds the element unless an equivalent one is present, reporting which happened. *)

val set : 'key -> 'value -> ('key, 'value) map -> ('key, 'value) map
(** A collection with the key bound to the value, adding or replacing as needed. *)

val remove : 'key -> ('key, 'value) map -> ('key, 'value) map
(** A collection without that element. *)

val remove_entry : 'key -> ('key, 'value) map -> ('key, 'value) map * ('key, 'value) entry option
(** A collection without that entry. *)

val get_or_add :
  'key -> (unit -> 'value) -> ('key, 'value) map -> ('key, 'value) map * 'value * bool
(** The value stored for the key, adding the supplied one when absent. *)

val add_or_update :
  'key ->
  add:(unit -> 'value) ->
  update:('value -> 'value) ->
  ('key, 'value) map ->
  ('key, 'value) map * 'value * bool
(** A collection with the key bound to the value, adding or replacing as needed. *)

val fold :
  ('accumulator -> 'key -> 'value -> 'accumulator) ->
  'accumulator ->
  ('key, 'value) map ->
  'accumulator
(** Folds over the elements. *)

val to_list : ('key, 'value) map -> ('key * 'value) list
(** The elements, in the collection's own order. *)

val equal : ('key, 'value) map -> ('key, 'value) map -> bool
(** Whether both collections hold the same elements. *)

val diff : ('key, 'value) map -> ('key, 'value) map -> ('key, 'value) difference list
(** The entry-level differences between two collections. Subtrees the two already share are skipped
    whole. *)

module Bulk_builder : sig
  type ('key, 'value) t
(** A builder for constructing a map in bulk, cheaper than repeated insertion. *)

  val create : ('key, 'value) map -> ('key, 'value) t
  val count : ('key, 'value) t -> int
  val set : 'key -> 'value -> ('key, 'value) t -> unit
  val add : 'key -> 'value -> ('key, 'value) t -> unit
  val freeze : ('key, 'value) t -> ('key, 'value) map
end

module Transient : sig
  type ('key, 'value) t
(** A mutable session seeded from a map, for building a version in bulk. Its edits are visible only
    through it until it is persisted. *)

  val create : ('key, 'value) map -> ('key, 'value) t
  val count : ('key, 'value) t -> int
  val find_opt : 'key -> ('key, 'value) t -> 'value option

  val fold :
    ('accumulator -> 'key -> 'value -> 'accumulator) ->
    'accumulator ->
    ('key, 'value) t ->
    'accumulator

  val set : 'key -> 'value -> ('key, 'value) t -> unit
  val remove : 'key -> ('key, 'value) t -> unit
  val persistent : ('key, 'value) t -> ('key, 'value) map
end
