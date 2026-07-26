(** Persistent multiset with positive per-class multiplicities. *)

type 'element entry = { element : 'element; multiplicity : int }
type 'element t
(** A persistent multiset: hashed elements with multiplicities stored per distinct element, so a
    large count costs no more than a small one. *)

val empty : 'element Common.Hash_policy.t -> 'element t
(** The empty collection. *)

val distinct_count : 'element t -> int
(** How many distinct elements are present, ignoring multiplicity. *)

val expanded_count : 'element t -> int64
(** The total multiplicity across all elements. *)

val multiplicity : 'element -> 'element t -> int
(** How many times the element occurs. *)

val add : ?count:int -> 'element -> 'element t -> 'element t
(** A collection containing the given element. *)

val remove : ?count:int -> 'element -> 'element t -> 'element t
(** A collection without that element. *)

val to_list : 'element t -> 'element entry list
(** The elements, in the collection's own order. *)

val sum : 'element t -> 'element t -> 'element t
(** The bag whose multiplicity for each element is the sum of the two operands'. *)

val union : 'element t -> 'element t -> 'element t
(** The elements of both collections. Subtrees the operands already share are adopted whole rather
    than re-entered. *)

val inter : 'element t -> 'element t -> 'element t
(** The elements present in both collections. *)

val diff : 'element t -> 'element t -> 'element t
(** The entry-level differences between two collections. Subtrees the two already share are skipped
    whole. *)

val equal : 'element t -> 'element t -> bool
(** Whether both collections hold the same elements. *)
