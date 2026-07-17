(** Persistent sorted set with policy-canonical deterministic ranks. *)

type rank = { geometric : int; secondary : int64; content : int64 }
type 'element rank_policy
type 'element t
type 'element lookup = Added of 'element t | Existing of 'element
type statistics = { count : int; canonical_height : int; structure_digest : int64 }

val stable_rank_hash_bytes : bytes -> int64
val stable_rank_hash_string : string -> int64

val create_policy :
  comparator:'element Common.Comparator.t ->
  rank_hash:('element -> int64) ->
  seed:int64 ->
  'element rank_policy

val comparator : 'element rank_policy -> 'element Common.Comparator.t
val rank : 'element rank_policy -> 'element -> rank
val empty : 'element rank_policy -> 'element t
val of_list : 'element rank_policy -> 'element list -> 'element t
val count : 'element t -> int
val is_empty : 'element t -> bool
val mem : 'element -> 'element t -> bool
val find : 'element -> 'element t -> 'element option
val add : 'element -> 'element t -> 'element lookup
val remove : 'element -> 'element t -> bool * 'element t
val minimum : 'element t -> 'element option
val maximum : 'element t -> 'element option
val nth : int -> 'element t -> 'element option
val to_list : 'element t -> 'element list
val statistics : 'element t -> statistics
