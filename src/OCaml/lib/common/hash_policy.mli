(** Runtime hashing and equality policy retained by persistent hash collections. *)

type 'a t

val create : hash:('a -> int) -> equal:('a -> 'a -> bool) -> 'a t
(** [create ~hash ~equal] constructs a policy. Equal values must have equal hashes. *)

val default : unit -> 'a t
(** [default ()] uses the standard structural hash and structural equality. *)

val hash : 'a t -> 'a -> int
val equal : 'a t -> 'a -> 'a -> bool

val same : 'a t -> 'a t -> bool
(** [same left right] reports physical policy identity. *)
