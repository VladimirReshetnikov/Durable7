(** Persistent key-ordered priority-search queue with a cached priority-then-key winner. *)

type ('key, 'priority, 'value) entry
type ('key, 'priority, 'value) t
(** A persistent priority search queue: ordered by key, searchable by minimum priority. A pennant
    tournament tree carries both orders at once, so neither question is answered by scanning the
    other order. *)

type statistics = { count : int; estimated_height : int; winner_recomputations : int }
(** Shape measurements returned by a structural audit. *)

val empty :
  key_comparator:'key Common.Comparator.t ->
  priority_comparator:'priority Common.Comparator.t ->
  ('key, 'priority, 'value) t
(** The empty queue. *)

val count : ('key, 'priority, 'value) t -> int
(** Number of entries in the queue. *)

val is_empty : ('key, 'priority, 'value) t -> bool
(** Whether the queue holds no entries. *)

val entry_key : ('key, 'priority, 'value) entry -> 'key
(** The entry's key. *)

val entry_priority : ('key, 'priority, 'value) entry -> 'priority
(** The entry's priority. *)

val entry_value : ('key, 'priority, 'value) entry -> 'value
(** The entry's value. *)

val find : 'key -> ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry option
(** The entry matching the probe. Raises when absent. *)

val mem : 'key -> ('key, 'priority, 'value) t -> bool
(** Whether the entry is present. *)

val nth : int -> ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry option
(** The entry at the given rank. *)

val lower_bound : 'key -> ('key, 'priority, 'value) t -> int
(** The rank of the first key not less than the probe. *)

val upper_bound : 'key -> ('key, 'priority, 'value) t -> int
(** The rank after any key equal to the probe. *)

val add :
  'key ->
  'priority ->
  'value ->
  ('key, 'priority, 'value) t ->
  (('key, 'priority, 'value) t, string) result
(** A queue containing the given entry. *)

val set :
  'key -> 'priority -> 'value -> ('key, 'priority, 'value) t -> bool * ('key, 'priority, 'value) t
(** A queue with the key bound to the value, adding or replacing as needed. *)

val remove :
  'key ->
  ('key, 'priority, 'value) t ->
  (('key, 'priority, 'value) entry * ('key, 'priority, 'value) t) option
(** A queue without that entry. *)

val minimum : ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry option
(** The smallest element, or the minimum-priority entry where the structure is a queue. *)

val minimum_view :
  ('key, 'priority, 'value) t ->
  (('key, 'priority, 'value) entry * ('key, 'priority, 'value) t) option
(** The minimum-priority entry together with the queue remaining. *)

val entries_by_key : ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry list
(** The entries bound to the key, in order. *)

val statistics : ('key, 'priority, 'value) t -> statistics
(** Shape measurements from a structural audit. *)
