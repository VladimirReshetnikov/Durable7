(** Persistent meldable minimum heap surface. *)

type 'element t
type statistics = { count : int; root_forest_length : int; maximum_rank : int; maximum_depth : int }

val empty : 'element Common.Comparator.t -> 'element t
val of_list : 'element Common.Comparator.t -> 'element list -> 'element t
val comparator : 'element t -> 'element Common.Comparator.t
val count : 'element t -> int
val is_empty : 'element t -> bool
val insert : 'element -> 'element t -> 'element t
val meld : 'element t -> 'element t -> 'element t
val minimum : 'element t -> 'element option
val minimum_view : 'element t -> ('element * 'element t) option
val delete_minimum : 'element t -> 'element t option
val to_sorted_list : 'element t -> 'element list
val statistics : 'element t -> statistics
