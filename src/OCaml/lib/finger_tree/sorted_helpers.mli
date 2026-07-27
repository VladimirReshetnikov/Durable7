(** Internal immutable-array operations shared by sorted facades. *)

val lower_bound : ('value -> 'value -> int) -> 'value -> 'value array -> int
(** The rank of the first entry not ordered before the probe, by binary search over a sorted array:
    the probe's own rank when it is present, and its insertion point when it is not. *)

val upper_bound : ('value -> 'value -> int) -> 'value -> 'value array -> int
(** The rank after any key equal to the probe. *)

val insert : int -> 'value -> 'value array -> 'value array
(** A new array with the value inserted at the index, leaving the source array untouched. Raises
    [Invalid_argument] when the index lies outside [0..length]. *)

val remove : int -> 'value array -> 'value array
(** A new array without the entry at the index, leaving the source array untouched. Raises
    [Invalid_argument] when the index lies outside [0..length-1]. *)

val slice : int -> int -> 'value array -> 'value array
(** A new array holding [count] entries from [start]. Raises [Invalid_argument] when that range does
    not lie inside the source. *)
