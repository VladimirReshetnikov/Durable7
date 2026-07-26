(** Stable persistent priority queue retaining insertion order among equal priorities. *)

type ('element, 'priority) entry
type ('element, 'priority) t
(** A persistent priority queue caching the minimum-priority entry as its measure, so peeking is a
    cached read and removal is a measure-directed descent. *)

val empty : 'priority Common.Comparator.t -> ('element, 'priority) t
(** The empty queue. *)

val comparator : ('element, 'priority) t -> 'priority Common.Comparator.t
(** The retained ordering policy. *)

val count : ('element, 'priority) t -> int
(** Number of entries in the queue. *)

val is_empty : ('element, 'priority) t -> bool
(** Whether the queue holds no entries. *)

val entry_value : ('element, 'priority) entry -> 'element
(** The entry's value. *)

val entry_priority : ('element, 'priority) entry -> 'priority
(** The entry's priority. *)

val enqueue : 'element -> 'priority -> ('element, 'priority) t -> ('element, 'priority) t
(** A queue with the entry added. *)

val meld : ('element, 'priority) t -> ('element, 'priority) t -> ('element, 'priority) t
(** The queue holding both operands' entries. *)

val peek : ('element, 'priority) t -> ('element, 'priority) entry option
(** The minimum-priority entry, without removing it. *)

val dequeue :
  ('element, 'priority) t -> (('element, 'priority) entry * ('element, 'priority) t) option
(** The minimum-priority entry together with the queue remaining. *)

val to_list : ('element, 'priority) t -> ('element, 'priority) entry list
(** The entries, in the queue's own order. *)
