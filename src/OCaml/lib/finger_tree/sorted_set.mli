(** Persistent rank-addressable sorted set with representative retention. *)

type 'element t

val empty : 'element Common.Comparator.t -> 'element t
(** The empty set. *)

val of_list : 'element Common.Comparator.t -> 'element list -> 'element t
(** A set holding a list's elements, built in bulk rather than by repeated insertion. *)

val comparator : 'element t -> 'element Common.Comparator.t
(** The retained ordering policy. *)

val count : 'element t -> int
(** Number of elements in the set. *)

val is_empty : 'element t -> bool
(** Whether the set holds no elements. *)

val minimum : 'element t -> 'element option
(** The smallest element, or the minimum-priority entry where the structure is a queue. *)

val maximum : 'element t -> 'element option
(** The largest element. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. *)

val index_of : 'element -> 'element t -> int option
(** The rank of the given element. *)

val count_less_than : 'element -> 'element t -> int
(** How many elements order before the probe. *)

val count_at_most : 'element -> 'element t -> int
(** How many elements order at or before the probe. *)

val find : 'element -> 'element t -> 'element option
(** The element matching the probe. Raises when absent. *)

val mem : 'element -> 'element t -> bool
(** Whether the element is present. *)

val add : 'element -> 'element t -> bool * 'element t
(** A set containing the given element. The flag is [true] when it was newly added and [false] when
    an equal element was already present, in which case the first representative is kept. *)

val remove : 'element -> 'element t -> bool * 'element t
(** A set without that element. The flag is [true] when it was present; removing an absent element
    is not an error and returns the receiver. *)

val floor : 'element -> 'element t -> 'element option
(** The largest element not greater than the probe. *)

val ceiling : 'element -> 'element t -> 'element option
(** The smallest element not less than the probe. *)

val lower : 'element -> 'element t -> 'element option
(** The largest element strictly less than the probe. *)

val higher : 'element -> 'element t -> 'element option
(** The smallest element strictly greater than the probe. *)

val union : 'element t -> 'element t -> 'element t
(** The elements of both sets. Subtrees the operands already share are adopted whole rather than re-
    entered. *)

val intersect : 'element t -> 'element t -> 'element t
(** The elements present in both sets. *)

val difference : 'element t -> 'element t -> 'element t
(** This set's elements that are absent from the other. *)

val range : start:int -> count:int -> 'element t -> ('element t, string) result
(** The elements in the given range. *)

val value_range : minimum:'element -> maximum:'element -> 'element t -> 'element t
(** The entries whose values fall in the given range. *)

val to_list : 'element t -> 'element list
(** The elements, in the set's own order. *)

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
