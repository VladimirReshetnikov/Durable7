(** Runtime ordering policy retained by sorted and ordered collections. *)

type 'a t

val create : ('a -> 'a -> int) -> 'a t
val default : unit -> 'a t
val compare : 'a t -> 'a -> 'a -> int
val reverse : 'a t -> 'a t
val same : 'a t -> 'a t -> bool
