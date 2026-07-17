(** Immutable non-negative arbitrary-precision integers. *)

type t

val zero : t
val one : t
val of_int : int -> t
val of_z : Z.t -> t
val to_z : t -> Z.t
val parse : string -> t
val to_string : t -> string
val equal : t -> t -> bool
val compare : t -> t -> int
val is_zero : t -> bool
val add : t -> t -> t
val mul : t -> t -> t
val power_of_two : t -> t
val power_of_two_int : int -> t
val exact_log2 : t -> t
