(** Internal immutable-array operations shared by sorted facades. *)

val lower_bound : ('value -> 'value -> int) -> 'value -> 'value array -> int
val upper_bound : ('value -> 'value -> int) -> 'value -> 'value array -> int
val insert : int -> 'value -> 'value array -> 'value array
val remove : int -> 'value array -> 'value array
val slice : int -> int -> 'value array -> 'value array
