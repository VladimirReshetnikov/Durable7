(** Persistent many-to-many relation with forward and reverse indexes. *)

type ('left, 'right) entry = { left : 'left; right : 'right }
type ('left, 'right) t
(** A persistent many-to-many relation indexed from both sides, so inverting it reuses both existing
    indexes rather than rebuilding. *)

val empty :
  left_policy:'left Common.Hash_policy.t ->
  right_policy:'right Common.Hash_policy.t ->
  ('left, 'right) t
(** The empty collection. *)

val pair_count : ('left, 'right) t -> int
(** How many pairs are present in total. *)

val left_count : ('left, 'right) t -> int
(** How many distinct left values are present. *)

val right_count : ('left, 'right) t -> int
(** How many distinct right values are present. *)

val contains : 'left -> 'right -> ('left, 'right) t -> bool
(** Whether the element is present. *)

val rights : 'left -> ('left, 'right) t -> 'right Persistent_hash_set.t
(** The right values related to the given left value. *)

val lefts : 'right -> ('left, 'right) t -> 'left Persistent_hash_set.t
(** The left values related to the given right value. Both directions are indexed, so neither is a
    scan. *)

val add : 'left -> 'right -> ('left, 'right) t -> ('left, 'right) t
(** A collection containing the given element. *)

val remove : 'left -> 'right -> ('left, 'right) t -> ('left, 'right) t
(** A collection without that element. *)

val remove_left : 'left -> ('left, 'right) t -> ('left, 'right) t
(** A relation without any pair holding that left value. *)

val remove_right : 'right -> ('left, 'right) t -> ('left, 'right) t
(** A relation without any pair holding that right value. *)

val inverse : ('left, 'right) t -> ('right, 'left) t
(** The relation with the two sides exchanged, reusing both existing indexes. *)

val to_list : ('left, 'right) t -> ('left, 'right) entry list
(** The elements, in the collection's own order. *)
