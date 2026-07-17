(** Persistent set over the shared HAMT map core. *)

type 'element t

val empty : 'element Common.Hash_policy.t -> 'element t
val singleton : 'element Common.Hash_policy.t -> 'element -> 'element t
val of_list : 'element Common.Hash_policy.t -> 'element list -> 'element t
val policy : 'element t -> 'element Common.Hash_policy.t
val count : 'element t -> int
val is_empty : 'element t -> bool
val mem : 'element -> 'element t -> bool
val find_opt : 'element -> 'element t -> 'element option
val add : 'element -> 'element t -> 'element t
val remove : 'element -> 'element t -> 'element t
val to_list : 'element t -> 'element list
val union : 'element t -> 'element t -> 'element t
val inter : 'element t -> 'element t -> 'element t
val diff : 'element t -> 'element t -> 'element t
val symmetric_diff : 'element t -> 'element t -> 'element t
val subset : 'element t -> 'element t -> bool
val proper_subset : 'element t -> 'element t -> bool
val disjoint : 'element t -> 'element t -> bool
val equal : 'element t -> 'element t -> bool

module Transient : sig
  type 'element session

  val create : 'element t -> 'element session
  val count : 'element session -> int
  val mem : 'element -> 'element session -> bool
  val subset : 'element session -> 'element t -> bool
  val proper_subset : 'element session -> 'element t -> bool
  val superset : 'element session -> 'element t -> bool
  val proper_superset : 'element session -> 'element t -> bool
  val overlaps : 'element session -> 'element t -> bool
  val equal : 'element session -> 'element t -> bool
  val add : 'element -> 'element session -> unit
  val remove : 'element -> 'element session -> unit
  val persistent : 'element session -> 'element t
end
