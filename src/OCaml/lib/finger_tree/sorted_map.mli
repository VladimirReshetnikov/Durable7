(** Persistent order-statistic sorted map with first-key representative retention.

    Built on the lazy Hinze–Paterson measured core with an order-statistic measure (entry count
    paired with the right-biased last key), replacing the immutable flat-array placeholder:
    O(log N) point writes by split-and-join, O(1) worst-case comparator-free extremes from the
    digits, O(log N) key seeks and neighbour queries, key ranges restricted by two boundary seeks
    into a structurally shared subrange, and O(m log m) bulk construction. Rank select is O(log N)
    — the census-ruled regression from the placeholder's O(1); extremes deliberately stay O(1). *)

type ('key, 'value) entry
type ('key, 'value) t
(** A persistent sorted map with rank and select over its keys. *)

val empty : 'key Common.Comparator.t -> ('key, 'value) t
(** The empty map. *)

val of_list : 'key Common.Comparator.t -> ('key * 'value) list -> (('key, 'value) t, string) result
(** A map holding a list's entries, built in bulk rather than by repeated insertion: one stable
    sort and duplicate check — O(m log m) comparisons — then an eager O(m) bottom-up build, where
    the placement-by-insertion build paid Theta(m^2) copying. *)

val comparator : ('key, 'value) t -> 'key Common.Comparator.t
(** The retained ordering policy. *)

val count : ('key, 'value) t -> int
(** Number of entries in the map. O(1). *)

val is_empty : ('key, 'value) t -> bool
(** Whether the map holds no entries. O(1). *)

val entry_key : ('key, 'value) entry -> 'key
(** The entry's key. *)

val entry_value : ('key, 'value) entry -> 'value
(** The entry's value. *)

val nth : int -> ('key, 'value) t -> ('key, 'value) entry option
(** The entry at the given rank. O(log N), by size-directed descent with no comparator call — the
    census-ruled regression from the array placeholder's O(1). *)

val lower_bound : 'key -> ('key, 'value) t -> int
(** The rank of the first key not less than the probe. O(log N) comparator calls: one
    measure-directed descent. *)

val upper_bound : 'key -> ('key, 'value) t -> int
(** The rank after any key equal to the probe. O(log N). *)

val find_entry : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The entry stored for the key. O(log N). *)

val find_opt : 'key -> ('key, 'value) t -> 'value option
(** The value stored for the key, or [None] when absent. O(log N). *)

val mem : 'key -> ('key, 'value) t -> bool
(** Whether the entry is present. O(log N). *)

val add : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A map containing the given entry. O(log N): a measure-directed seek and a split-and-join
    insertion. *)

val set : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
(** A map with the key bound to the value, adding or replacing as needed. The flag is [true] when
    the key was newly added and [false] when an existing binding was replaced, in which case the
    stored first-key representative is kept. O(log N). *)

val remove : 'key -> ('key, 'value) t -> ('value * ('key, 'value) t) option
(** A map without that entry. O(log N). *)

val minimum : ('key, 'value) t -> ('key, 'value) entry option
(** The smallest element, or the minimum-priority entry where the structure is a queue. O(1)
    worst case: a digit read that invokes no comparator. *)

val maximum : ('key, 'value) t -> ('key, 'value) entry option
(** The largest element. O(1) worst case: a digit read that invokes no comparator. *)

val floor : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The largest element not greater than the probe. O(log N). *)

val ceiling : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The smallest element not less than the probe. O(log N). *)

val lower : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The largest element strictly less than the probe. O(log N). *)

val higher : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The smallest element strictly greater than the probe. O(log N). *)

val range : start:int -> count:int -> ('key, 'value) t -> (('key, 'value) t, string) result
(** The entries in the given range. O(log N): two splits sharing the selected subrange
    structurally. *)

val key_range : minimum:'key -> maximum:'key -> ('key, 'value) t -> ('key, 'value) t
(** The entries whose keys fall in the given range. O(log N): two boundary seeks and a
    structurally shared restriction whose cost never depends on the excluded entries. An inverted
    range yields the empty map, decided by the retained comparator. *)

val to_list : ('key, 'value) t -> ('key * 'value) list
(** The entries, in the map's own order. Theta(N), with no comparator call. *)

(** A mutable accumulator over one map, for a run of edits that should cost one handle rather than
    one retained version per entry. *)
module Builder : sig
  type ('key, 'value) map = ('key, 'value) t
  (** The map type this builder produces. *)

  type ('key, 'value) t
  (** A mutable accumulator for building a collection in bulk. Deliberately not a snapshot: it fills
      a buffer and produces a persistent value only on demand, which is cheaper than one version per
      element. *)

  val create : ('key, 'value) map -> ('key, 'value) t
  (** A builder seeded with the given map, which is itself left untouched. *)

  val set : 'key -> 'value -> ('key, 'value) t -> bool
  (** Bind the key to the value, adding or replacing as needed. Returns [true] when the key was
      newly added and [false] when an existing binding was replaced. *)

  val remove : 'key -> ('key, 'value) t -> 'value option
  (** Remove the key and return the value it held, or [None] when it was absent. *)

  val freeze : ('key, 'value) t -> ('key, 'value) map
  (** The map staged so far. The builder stays usable, and because maps are immutable the returned
      value is unaffected by later edits. *)
end
