(** Persistent key-ordered priority-search queue with a cached priority-then-key winner. *)

type ('key, 'priority, 'value) entry
type ('key, 'priority, 'value) t
type statistics = { count : int; estimated_height : int; winner_recomputations : int }

val empty :
  key_comparator:'key Common.Comparator.t ->
  priority_comparator:'priority Common.Comparator.t ->
  ('key, 'priority, 'value) t

val count : ('key, 'priority, 'value) t -> int
val is_empty : ('key, 'priority, 'value) t -> bool
val entry_key : ('key, 'priority, 'value) entry -> 'key
val entry_priority : ('key, 'priority, 'value) entry -> 'priority
val entry_value : ('key, 'priority, 'value) entry -> 'value
val find : 'key -> ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry option
val mem : 'key -> ('key, 'priority, 'value) t -> bool

val add :
  'key ->
  'priority ->
  'value ->
  ('key, 'priority, 'value) t ->
  (('key, 'priority, 'value) t, string) result

val set :
  'key -> 'priority -> 'value -> ('key, 'priority, 'value) t -> bool * ('key, 'priority, 'value) t

val remove :
  'key ->
  ('key, 'priority, 'value) t ->
  (('key, 'priority, 'value) entry * ('key, 'priority, 'value) t) option

val minimum : ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry option

val minimum_view :
  ('key, 'priority, 'value) t ->
  (('key, 'priority, 'value) entry * ('key, 'priority, 'value) t) option

val entries_by_key : ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry list
val statistics : ('key, 'priority, 'value) t -> statistics
