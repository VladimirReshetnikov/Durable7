(** Internal immutable-array operations shared by sorted facades. *)

val lower_bound : ('value -> 'value -> int) -> 'value -> 'value array -> int
val upper_bound : ('value -> 'value -> int) -> 'value -> 'value array -> int
(** The rank after any key equal to the probe. *)

val insert : int -> 'value -> 'value array -> 'value array
(** A collection containing the given element. *)

val remove : int -> 'value array -> 'value array
(** A collection without that element. *)

val slice : int -> int -> 'value array -> 'value array
(** The elements in the given range. *)
