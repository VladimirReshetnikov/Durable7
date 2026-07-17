(** Persistent multiset with positive per-class multiplicities. *)

type 'element entry = { element : 'element; multiplicity : int }
type 'element t

val empty : 'element Common.Hash_policy.t -> 'element t
val distinct_count : 'element t -> int
val expanded_count : 'element t -> int64
val multiplicity : 'element -> 'element t -> int
val add : ?count:int -> 'element -> 'element t -> 'element t
val remove : ?count:int -> 'element -> 'element t -> 'element t
val to_list : 'element t -> 'element entry list
val sum : 'element t -> 'element t -> 'element t
val union : 'element t -> 'element t -> 'element t
val inter : 'element t -> 'element t -> 'element t
val diff : 'element t -> 'element t -> 'element t
val equal : 'element t -> 'element t -> bool
