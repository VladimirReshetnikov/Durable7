(** Persistent meldable minimum heap surface. *)

type 'element t
type statistics = { count : int; root_forest_length : int; maximum_rank : int; maximum_depth : int }
(** Shape measurements returned by a structural audit. *)

val empty : 'element Common.Comparator.t -> 'element t
(** The empty heap. *)

val of_list : 'element Common.Comparator.t -> 'element list -> 'element t
(** A heap holding a list's elements, built in bulk rather than by repeated insertion. *)

val comparator : 'element t -> 'element Common.Comparator.t
(** The retained ordering policy. *)

val count : 'element t -> int
(** Number of elements in the heap. *)

val is_empty : 'element t -> bool
(** Whether the heap holds no elements. *)

val insert : 'element -> 'element t -> 'element t
(** A heap containing the given element. *)

val meld : 'element t -> 'element t -> 'element t
(** The heap holding both operands' elements. *)

val minimum : 'element t -> 'element option
(** The smallest element, or the minimum-priority entry where the structure is a queue. *)

val minimum_view : 'element t -> ('element * 'element t) option
(** The minimum-priority entry together with the heap remaining. *)

val delete_minimum : 'element t -> 'element t option
(** A heap without its minimum-priority entry. *)

val to_sorted_list : 'element t -> 'element list
(** The elements in ascending order. *)

val statistics : 'element t -> statistics
(** Shape measurements from a structural audit. *)
