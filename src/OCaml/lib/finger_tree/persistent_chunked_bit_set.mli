(** Sparse persistent non-negative bit set with rank/select queries. *)

type t
type statistics = { bit_count : int; chunk_count : int; highest_bit : int option }

val empty : t
val of_list : int list -> (t, string) result
val count : t -> int
val is_empty : t -> bool
val mem : int -> t -> bool
val add : int -> t -> (bool * t, string) result
val remove : int -> t -> bool * t
val toggle : int -> t -> (bool * t, string) result
val union : t -> t -> t
val intersect : t -> t -> t
val difference : t -> t -> t
val symmetric_difference : t -> t -> t

val rank : int -> t -> int
(** [rank index set] counts present bits strictly below [index]. *)

val select : int -> t -> int option
val next_set_bit : int -> t -> int option
val previous_set_bit : int -> t -> int option
val to_list : t -> int list
val statistics : t -> statistics
