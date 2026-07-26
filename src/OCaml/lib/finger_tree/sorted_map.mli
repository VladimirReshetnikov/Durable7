(** Persistent order-statistic sorted map with first-key representative retention. *)

type ('key, 'value) entry
type ('key, 'value) t
(** A persistent sorted map with rank and select over its keys. *)

val empty : 'key Common.Comparator.t -> ('key, 'value) t
(** The empty map. *)

val of_list : 'key Common.Comparator.t -> ('key * 'value) list -> (('key, 'value) t, string) result
(** A map holding a list's entries, built in bulk rather than by repeated insertion. *)

val comparator : ('key, 'value) t -> 'key Common.Comparator.t
(** The retained ordering policy. *)

val count : ('key, 'value) t -> int
(** Number of entries in the map. *)

val is_empty : ('key, 'value) t -> bool
(** Whether the map holds no entries. *)

val entry_key : ('key, 'value) entry -> 'key
(** The entry's key. *)

val entry_value : ('key, 'value) entry -> 'value
(** The entry's value. *)

val nth : int -> ('key, 'value) t -> ('key, 'value) entry option
(** The entry at the given rank. *)

val lower_bound : 'key -> ('key, 'value) t -> int
(** The rank of the first key not less than the probe. *)

val upper_bound : 'key -> ('key, 'value) t -> int
(** The rank after any key equal to the probe. *)

val find_entry : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The entry stored for the key. *)

val find_opt : 'key -> ('key, 'value) t -> 'value option
(** The value stored for the key, or [None] when absent. *)

val mem : 'key -> ('key, 'value) t -> bool
(** Whether the entry is present. *)

val add : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A map containing the given entry. *)

val set : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
(** A map with the key bound to the value, adding or replacing as needed. *)

val remove : 'key -> ('key, 'value) t -> ('value * ('key, 'value) t) option
(** A map without that entry. *)

val minimum : ('key, 'value) t -> ('key, 'value) entry option
(** The smallest element, or the minimum-priority entry where the structure is a queue. *)

val maximum : ('key, 'value) t -> ('key, 'value) entry option
(** The largest element. *)

val floor : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The largest element not greater than the probe. *)

val ceiling : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The smallest element not less than the probe. *)

val lower : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The largest element strictly less than the probe. *)

val higher : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The smallest element strictly greater than the probe. *)

val range : start:int -> count:int -> ('key, 'value) t -> (('key, 'value) t, string) result
(** The entries in the given range. *)

val key_range : minimum:'key -> maximum:'key -> ('key, 'value) t -> ('key, 'value) t
(** The entries whose keys fall in the given range. *)

val to_list : ('key, 'value) t -> ('key * 'value) list
(** The entries, in the map's own order. *)

module Builder : sig
  type ('key, 'value) map = ('key, 'value) t
  type ('key, 'value) t
(** A mutable accumulator for building a collection in bulk. Deliberately not a snapshot: it fills a
    buffer and produces a persistent value only on demand, which is cheaper than one version per
    element. *)

  val create : ('key, 'value) map -> ('key, 'value) t
  val set : 'key -> 'value -> ('key, 'value) t -> bool
  val remove : 'key -> ('key, 'value) t -> 'value option
  val freeze : ('key, 'value) t -> ('key, 'value) map
end
