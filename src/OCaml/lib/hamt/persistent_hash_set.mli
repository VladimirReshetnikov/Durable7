(** Persistent set over the shared HAMT map core. *)

type 'element t

val empty : 'element Common.Hash_policy.t -> 'element t
(** The empty collection. *)

val singleton : 'element Common.Hash_policy.t -> 'element -> 'element t
(** A collection holding one element. *)

val of_list : 'element Common.Hash_policy.t -> 'element list -> 'element t
(** A collection holding a list's elements, built in bulk rather than by repeated insertion. *)

val policy : 'element t -> 'element Common.Hash_policy.t
(** The policy the collection retains. *)

val count : 'element t -> int
(** Number of elements in the collection. *)

val is_empty : 'element t -> bool
(** Whether the collection holds no elements. *)

val mem : 'element -> 'element t -> bool
(** Whether the element is present. *)

val find_opt : 'element -> 'element t -> 'element option
(** The value stored for the key, or [None] when absent. *)

val add : 'element -> 'element t -> 'element t
(** A collection containing the given element. *)

val remove : 'element -> 'element t -> 'element t
(** A collection without that element. *)

val to_list : 'element t -> 'element list
(** The elements, in the collection's own order. *)

val union : 'element t -> 'element t -> 'element t
(** The elements of both collections. Subtrees the operands already share are adopted whole rather
    than re-entered. *)

val inter : 'element t -> 'element t -> 'element t
(** The elements present in both collections. *)

val diff : 'element t -> 'element t -> 'element t
(** The entry-level differences between two collections. Subtrees the two already share are skipped
    whole. *)

val symmetric_diff : 'element t -> 'element t -> 'element t
(** The elements present in exactly one of the two collections. *)

val subset : 'element t -> 'element t -> bool
(** Whether every element of this collection also occurs in the other. *)

val proper_subset : 'element t -> 'element t -> bool
(** Whether this collection is a subset of the other and the other holds an element it lacks. *)

val disjoint : 'element t -> 'element t -> bool
(** Whether the two collections share no element. *)

val equal : 'element t -> 'element t -> bool
(** Whether both collections hold the same elements. *)

module Transient : sig
  type 'element session
(** A mutable session seeded from a set, for building a version in bulk. *)

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
