(** Persistent order-statistic sorted multiset.

    Built on the lazy Hinze–Paterson measured core with the sorted family's order-statistic
    measure, replacing the immutable flat-array placeholder: O(log n) point writes by
    split-and-join, O(1) worst-case comparator-free extremes from the digits, O(log n) seeks,
    counting queries, and range restriction, O(n log n) bulk construction. Rank select is O(log n)
    — the census-ruled regression from the placeholder's O(1); extremes deliberately stay O(1). *)

type 'element t

val empty : 'element Common.Comparator.t -> 'element t
(** The empty bag. *)

val of_list : 'element Common.Comparator.t -> 'element list -> 'element t
(** A bag holding a list's elements, built in bulk rather than by repeated insertion: one stable
    sort (O(n log n) comparisons, equal elements kept in arrival order) then an eager O(n)
    bottom-up build. *)

val comparator : 'element t -> 'element Common.Comparator.t
(** The retained ordering policy. *)

val count : 'element t -> int
(** Number of elements in the bag. O(1). *)

val is_empty : 'element t -> bool
(** Whether the bag holds no elements. O(1). *)

val minimum : 'element t -> 'element option
(** The smallest element, or the minimum-priority entry where the structure is a queue. O(1)
    worst case: a digit read that invokes no comparator. *)

val maximum : 'element t -> 'element option
(** The largest element. O(1) worst case: a digit read that invokes no comparator. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. O(log n), by size-directed descent with no comparator call —
    the census-ruled regression from the array placeholder's O(1). *)

val count_less_than : 'element -> 'element t -> int
(** How many elements order before the probe. O(log n). *)

val count_at_most : 'element -> 'element t -> int
(** How many elements order at or before the probe. O(log n). *)

val count_of : 'element -> 'element t -> int
(** How many times the element occurs. O(log n). *)

val mem : 'element -> 'element t -> bool
(** Whether the element is present. O(log n). *)

val add : 'element -> 'element t -> 'element t
(** A bag containing the given element, inserted after every equal element. O(log n): a
    measure-directed seek and a split-and-join insertion. *)

val remove : 'element -> 'element t -> 'element t
(** A bag without that element. O(log n). *)

val remove_at : int -> 'element t -> ('element t, string) result
(** A bag without the element at the position. O(log n). *)

val remove_all : 'element -> 'element t -> 'element t
(** A bag without any occurrence of the element. O(log n): two boundary seeks and one
    concatenation around the excised run, independent of how many occurrences it removes. *)

val range : start:int -> count:int -> 'element t -> ('element t, string) result
(** The elements in the given range. O(log n): two splits sharing the selected subrange
    structurally. *)

val value_range : minimum:'element -> maximum:'element -> 'element t -> 'element t
(** The entries whose values fall in the given range. O(log n): two boundary seeks and a
    structurally shared restriction. An inverted range yields the empty bag, decided by the
    retained comparator. *)

val to_list : 'element t -> 'element list
(** The elements, in the bag's own order. Theta(n), with no comparator call. *)
