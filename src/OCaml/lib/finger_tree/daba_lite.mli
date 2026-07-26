(** Mutable FIFO monoid aggregator with failure-atomic publication. *)

type 'value t

type statistics = {
  count : int;
  block_count : int;
  allocated_slot_capacity : int;
  slack_slot_count : int;
(** Shape measurements returned by a structural audit. *)

}

val create : identity:'value -> combine:('value -> 'value -> 'value) -> unit -> 'value t
(** An empty window using the supplied policies, which it retains. *)

val count : 'value t -> int
(** Number of elements in the window. *)

val is_empty : 'value t -> bool
(** Whether the window holds no elements. *)

val aggregate : 'value t -> 'value
(** The combined measure of the window's elements, maintained without ever recomputing it from
    scratch. *)

val insert : 'value -> 'value t -> unit
(** A window containing the given element. *)

val try_evict : 'value t -> bool
(** Removes the oldest element, reporting whether there was one. *)

val evict : 'value t -> (unit, string) result
(** The window without its oldest element. *)

val clear : 'value t -> unit
(** The empty window, retaining the same policies. *)

val to_list : 'value t -> 'value list
(** The elements, in the window's own order. *)

val statistics : 'value t -> statistics
(** Shape measurements from a structural audit. *)
