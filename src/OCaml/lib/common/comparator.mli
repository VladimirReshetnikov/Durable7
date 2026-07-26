(** Runtime ordering policy retained by sorted and ordered collections. *)

type 'a t

val create : ('a -> 'a -> int) -> 'a t
(** A policy ordering by the supplied comparison, which it retains. *)

val default : unit -> 'a t
(** The policy using the platform's own comparison. *)

val compare : 'a t -> 'a -> 'a -> int
(** Orders two values under this policy. *)

val reverse : 'a t -> 'a t
(** The policy ordering by the reverse of this one. *)

val same : 'a t -> 'a t -> bool
(** Whether both handles denote the same policy identity. Policy identity governs whether two
    collections may be combined at all. *)
