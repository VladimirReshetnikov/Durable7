(** Mutable FIFO monoid aggregator with failure-atomic publication. *)

type 'value t

type statistics = {
  count : int;
  block_count : int;
  allocated_slot_capacity : int;
  slack_slot_count : int;
}

val create : identity:'value -> combine:('value -> 'value -> 'value) -> unit -> 'value t
val count : 'value t -> int
val is_empty : 'value t -> bool
val aggregate : 'value t -> 'value
val insert : 'value -> 'value t -> unit
val try_evict : 'value t -> bool
val evict : 'value t -> (unit, string) result
val clear : 'value t -> unit
val to_list : 'value t -> 'value list
val statistics : 'value t -> statistics
