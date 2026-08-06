(** Persistent rank-addressable sorted set with representative retention.

    Built on the lazy Hinze–Paterson measured core with an order-statistic measure (element count
    paired with a right-biased last element), replacing the immutable flat-array placeholder. The
    delivered bounds are the library's reference profile: O(log n) point writes by split-and-join,
    O(1) worst-case extremes read from the outer digits with no comparator call, O(log n) key
    seeks, rank select, and range restriction, and O(n log n) bulk construction.

    One deliberate regression, ruled acceptable in the 2026-08-05 complexity-parity census: the
    array placeholder answered {!nth} in O(1), and rank select is now O(log n). No persistent
    structure gives both O(log n) writes and O(1) select; the O(1) select was an artifact of the
    placeholder, and the reference profile — O(log n) select, O(1) extremes, O(log n) writes — is
    the contract. Extremes deliberately do {e not} regress: {!minimum} and {!maximum} stay O(1)
    worst case as digit reads. *)

type 'element t

val empty : 'element Common.Comparator.t -> 'element t
(** The empty set. *)

val of_list : 'element Common.Comparator.t -> 'element list -> 'element t
(** A set holding a list's elements, built in bulk rather than by repeated insertion: one stable
    sort — O(n log n) comparisons, with the first member of each equal run surviving — then an
    eager O(n) bottom-up build. *)

val comparator : 'element t -> 'element Common.Comparator.t
(** The retained ordering policy. *)

val count : 'element t -> int
(** Number of elements in the set. O(1). *)

val is_empty : 'element t -> bool
(** Whether the set holds no elements. O(1). *)

val minimum : 'element t -> 'element option
(** The smallest element, or the minimum-priority entry where the structure is a queue. O(1)
    worst case: a digit read that invokes no comparator. *)

val maximum : 'element t -> 'element option
(** The largest element. O(1) worst case: a digit read that invokes no comparator. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. O(log n), by size-directed descent with no comparator call —
    the census-ruled regression from the array placeholder's O(1). *)

val index_of : 'element -> 'element t -> int option
(** The rank of the given element. O(log n) comparator calls: one measure-directed descent. *)

val count_less_than : 'element -> 'element t -> int
(** How many elements order before the probe. O(log n). *)

val count_at_most : 'element -> 'element t -> int
(** How many elements order at or before the probe. O(log n). *)

val find : 'element -> 'element t -> 'element option
(** The element matching the probe. Raises when absent. O(log n). *)

val mem : 'element -> 'element t -> bool
(** Whether the element is present. O(log n). *)

val add : 'element -> 'element t -> bool * 'element t
(** A set containing the given element. The flag is [true] when it was newly added and [false] when
    an equal element was already present, in which case the first representative is kept. O(log n):
    a measure-directed seek and a split-and-join insertion. *)

val remove : 'element -> 'element t -> bool * 'element t
(** A set without that element. The flag is [true] when it was present; removing an absent element
    is not an error and returns the receiver. O(log n). *)

val floor : 'element -> 'element t -> 'element option
(** The largest element not greater than the probe. O(log n). *)

val ceiling : 'element -> 'element t -> 'element option
(** The smallest element not less than the probe. O(log n). *)

val lower : 'element -> 'element t -> 'element option
(** The largest element strictly less than the probe. O(log n). *)

val higher : 'element -> 'element t -> 'element option
(** The smallest element strictly greater than the probe. O(log n). *)

val union : 'element t -> 'element t -> 'element t
(** The elements of both sets, with this set's representative kept for every class present in
    both. O(m log(n + m)) for an m-element right operand: iterate-and-insert, replacing the
    placeholder's Theta(n·m) fold-with-copy. *)

val intersect : 'element t -> 'element t -> 'element t
(** The elements present in both sets. O(n log m): one ascending walk of this set with a membership
    seek into the other, building the result by endpoint pushes. *)

val difference : 'element t -> 'element t -> 'element t
(** This set's elements that are absent from the other. O(n log m). *)

val range : start:int -> count:int -> 'element t -> ('element t, string) result
(** The elements in the given range. O(log n): two splits sharing the selected subrange
    structurally. *)

val value_range : minimum:'element -> maximum:'element -> 'element t -> 'element t
(** The entries whose values fall in the given range. O(log n): two boundary seeks and a
    structurally shared restriction. An inverted range yields the empty set, decided by the
    retained comparator. *)

val to_list : 'element t -> 'element list
(** The elements, in the set's own order. Theta(n), with no comparator call. *)

(** A mutable accumulator over one set, for a run of edits that should cost one handle rather than
    one retained version per element. *)
module Builder : sig
  type 'element set = 'element t
  (** The set type this builder produces. *)

  type 'element t
  (** A mutable accumulator for building a collection in bulk. Deliberately not a snapshot: it fills
      a buffer and produces a persistent value only on demand, which is cheaper than one version per
      element. *)

  val create : 'element set -> 'element t
  (** A builder seeded with the given set, which is itself left untouched. *)

  val add : 'element -> 'element t -> bool
  (** Add the element. Returns [true] when it was newly added and [false] when an equal element was
      already present, in which case the first representative is kept. *)

  val remove : 'element -> 'element t -> bool
  (** Remove the element. Returns [true] when it was present. *)

  val freeze : 'element t -> 'element set
  (** The set staged so far. The builder stays usable, and because sets are immutable the returned
      value is unaffected by later edits. *)
end
