(** Persistent 32-way hash-array mapped trie. *)

exception Duplicate_key
exception Transient_consumed

type ('key, 'value) entry = { key : 'key; value : 'value }
type difference_kind = Only_left | Only_right | Different

type ('key, 'value) difference = {
  kind : difference_kind;
  difference_key : 'key;
  left_value : 'value option;
  right_value : 'value option;
}

type ('key, 'value) map

val empty :
  ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) map

val singleton :
  ?value_equal:('value -> 'value -> bool) ->
  'key Common.Hash_policy.t ->
  'key ->
  'value ->
  ('key, 'value) map

val policy : ('key, 'value) map -> 'key Common.Hash_policy.t
val entry_key : ('key, 'value) entry -> 'key
val entry_value : ('key, 'value) entry -> 'value
val count : ('key, 'value) map -> int
val is_empty : ('key, 'value) map -> bool
val find_entry_opt : 'key -> ('key, 'value) map -> ('key, 'value) entry option
val find_opt : 'key -> ('key, 'value) map -> 'value option
val mem : 'key -> ('key, 'value) map -> bool
val add : 'key -> 'value -> ('key, 'value) map -> ('key, 'value) map
val try_add : 'key -> 'value -> ('key, 'value) map -> ('key, 'value) map * bool
val set : 'key -> 'value -> ('key, 'value) map -> ('key, 'value) map
val remove : 'key -> ('key, 'value) map -> ('key, 'value) map
val remove_entry : 'key -> ('key, 'value) map -> ('key, 'value) map * ('key, 'value) entry option

val get_or_add :
  'key -> (unit -> 'value) -> ('key, 'value) map -> ('key, 'value) map * 'value * bool

val add_or_update :
  'key ->
  add:(unit -> 'value) ->
  update:('value -> 'value) ->
  ('key, 'value) map ->
  ('key, 'value) map * 'value * bool

val fold :
  ('accumulator -> 'key -> 'value -> 'accumulator) ->
  'accumulator ->
  ('key, 'value) map ->
  'accumulator

val to_list : ('key, 'value) map -> ('key * 'value) list
val equal : ('key, 'value) map -> ('key, 'value) map -> bool
val diff : ('key, 'value) map -> ('key, 'value) map -> ('key, 'value) difference list

module Bulk_builder : sig
  type ('key, 'value) t

  val create : ('key, 'value) map -> ('key, 'value) t
  val count : ('key, 'value) t -> int
  val set : 'key -> 'value -> ('key, 'value) t -> unit
  val add : 'key -> 'value -> ('key, 'value) t -> unit
  val freeze : ('key, 'value) t -> ('key, 'value) map
end

module Transient : sig
  type ('key, 'value) t

  val create : ('key, 'value) map -> ('key, 'value) t
  val count : ('key, 'value) t -> int
  val find_opt : 'key -> ('key, 'value) t -> 'value option
  val set : 'key -> 'value -> ('key, 'value) t -> unit
  val remove : 'key -> ('key, 'value) t -> unit
  val persistent : ('key, 'value) t -> ('key, 'value) map
end
