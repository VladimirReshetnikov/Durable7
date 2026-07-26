(** Persistent order-statistic sorted multiset. *)

type 'element t

val empty : 'element Common.Comparator.t -> 'element t
(** The empty bag. *)

val of_list : 'element Common.Comparator.t -> 'element list -> 'element t
(** A bag holding a list's elements, built in bulk rather than by repeated insertion. *)

val comparator : 'element t -> 'element Common.Comparator.t
(** The retained ordering policy. *)

val count : 'element t -> int
(** Number of elements in the bag. *)

val is_empty : 'element t -> bool
(** Whether the bag holds no elements. *)

val minimum : 'element t -> 'element option
(** The smallest element, or the minimum-priority entry where the structure is a queue. *)

val maximum : 'element t -> 'element option
(** The largest element. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. *)

val count_less_than : 'element -> 'element t -> int
(** How many elements order before the probe. *)

val count_at_most : 'element -> 'element t -> int
(** How many elements order at or before the probe. *)

val count_of : 'element -> 'element t -> int
(** How many times the element occurs. *)

val mem : 'element -> 'element t -> bool
(** Whether the element is present. *)

val add : 'element -> 'element t -> 'element t
(** A bag containing the given element. *)

val remove : 'element -> 'element t -> 'element t
(** A bag without that element. *)

val remove_at : int -> 'element t -> ('element t, string) result
(** A bag without the element at the position. *)

val remove_all : 'element -> 'element t -> 'element t
(** A bag without any occurrence of the element. *)

val range : start:int -> count:int -> 'element t -> ('element t, string) result
(** The elements in the given range. *)

val value_range : minimum:'element -> maximum:'element -> 'element t -> 'element t
(** The entries whose values fall in the given range. *)

val to_list : 'element t -> 'element list
(** The elements, in the bag's own order. *)
