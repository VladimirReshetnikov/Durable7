(** Persistent many-to-many relation with forward and reverse indexes. *)

type ('left, 'right) entry = { left : 'left; right : 'right }
type ('left, 'right) t

val empty :
  left_policy:'left Common.Hash_policy.t ->
  right_policy:'right Common.Hash_policy.t ->
  ('left, 'right) t

val pair_count : ('left, 'right) t -> int
val left_count : ('left, 'right) t -> int
val right_count : ('left, 'right) t -> int
val contains : 'left -> 'right -> ('left, 'right) t -> bool
val rights : 'left -> ('left, 'right) t -> 'right Persistent_hash_set.t
val lefts : 'right -> ('left, 'right) t -> 'left Persistent_hash_set.t
val add : 'left -> 'right -> ('left, 'right) t -> ('left, 'right) t
val remove : 'left -> 'right -> ('left, 'right) t -> ('left, 'right) t
val remove_left : 'left -> ('left, 'right) t -> ('left, 'right) t
val remove_right : 'right -> ('left, 'right) t -> ('left, 'right) t
val inverse : ('left, 'right) t -> ('right, 'left) t
val to_list : ('left, 'right) t -> ('left, 'right) entry list
