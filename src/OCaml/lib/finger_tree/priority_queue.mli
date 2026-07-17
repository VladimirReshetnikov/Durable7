(** Stable persistent priority queue retaining insertion order among equal priorities. *)

type ('element, 'priority) entry
type ('element, 'priority) t

val empty : 'priority Common.Comparator.t -> ('element, 'priority) t
val comparator : ('element, 'priority) t -> 'priority Common.Comparator.t
val count : ('element, 'priority) t -> int
val is_empty : ('element, 'priority) t -> bool
val entry_value : ('element, 'priority) entry -> 'element
val entry_priority : ('element, 'priority) entry -> 'priority
val enqueue : 'element -> 'priority -> ('element, 'priority) t -> ('element, 'priority) t
val meld : ('element, 'priority) t -> ('element, 'priority) t -> ('element, 'priority) t
val peek : ('element, 'priority) t -> ('element, 'priority) entry option

val dequeue :
  ('element, 'priority) t -> (('element, 'priority) entry * ('element, 'priority) t) option

val to_list : ('element, 'priority) t -> ('element, 'priority) entry list
