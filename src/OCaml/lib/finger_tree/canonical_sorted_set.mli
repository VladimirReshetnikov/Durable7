(** Persistent sorted set with policy-canonical deterministic ranks. *)

type rank = { geometric : int; secondary : int64; content : int64 }
(** An element's derived rank. [geometric] gives the tree level and decides shape; [secondary] and
    [content] break ties deterministically, so equal geometric ranks still yield one canonical
    topology rather than depending on insertion order. *)

type 'element rank_policy
(** How an element's rank is derived. The rank fixes where the element sits, which is what makes the
    shape depend only on contents. *)

type 'element t
(** A persistent sorted set whose shape depends only on its contents, not on its edit history. Each
    element's rank is derived from the element itself under the policy's seed, so two sets holding
    the same elements are the same tree however they were built. *)

type 'element lookup = Added of 'element t | Existing of 'element
(** A presence-safe lookup result, so a stored [None] stays distinct from absence. *)

type statistics = { count : int; canonical_height : int; structure_digest : int64 }
(** Shape measurements returned by a structural audit. *)

val stable_rank_hash_bytes : bytes -> int64
(** A rank hash over bytes, stable across runs so the resulting shape is reproducible. *)

val stable_rank_hash_string : string -> int64
(** A rank hash over a string, stable across runs. *)

val create_policy :
  comparator:'element Common.Comparator.t ->
  rank_hash:('element -> int64) ->
  seed:int64 ->
  'element rank_policy
(** A policy built from the supplied callbacks. *)

val comparator : 'element rank_policy -> 'element Common.Comparator.t
(** The retained ordering policy. *)

val rank : 'element rank_policy -> 'element -> rank
(** The rank the policy derives for an element. *)

val empty : 'element rank_policy -> 'element t
(** The empty set. *)

val of_list : 'element rank_policy -> 'element list -> 'element t
(** A set holding a list's elements, built in bulk rather than by repeated insertion. *)

val count : 'element t -> int
(** Number of elements in the set. *)

val is_empty : 'element t -> bool
(** Whether the set holds no elements. *)

val mem : 'element -> 'element t -> bool
(** Whether the element is present. *)

val find : 'element -> 'element t -> 'element option
(** The element matching the probe. Raises when absent. *)

val add : 'element -> 'element t -> 'element lookup
(** A set containing the given element. *)

val remove : 'element -> 'element t -> bool * 'element t
(** A set without that element. *)

val minimum : 'element t -> 'element option
(** The smallest element, or the minimum-priority entry where the structure is a queue. *)

val maximum : 'element t -> 'element option
(** The largest element. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. *)

val lower_bound : 'element -> 'element t -> int
(** The rank of the first key not less than the probe. *)

val upper_bound : 'element -> 'element t -> int
(** The rank after any key equal to the probe. *)

val to_list : 'element t -> 'element list
(** The elements, in the set's own order. *)

val statistics : 'element t -> statistics
(** Shape measurements from a structural audit. *)
