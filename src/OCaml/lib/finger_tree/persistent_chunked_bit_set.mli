(** Sparse persistent non-negative bit set with rank/select queries. *)

type t
(** A persistent set of non-negative integers, stored as sparse fixed-width chunks so that a set
    holding a few very large members costs proportionally to the members rather than to the largest
    one. *)

type statistics = { bit_count : int; chunk_count : int; highest_bit : int option }
(** Shape measurements returned by a structural audit. *)

val empty : t
(** The empty bit set. *)

val of_list : int list -> (t, string) result
(** A bit set holding a list's set bits, built in bulk rather than by repeated insertion. *)

val count : t -> int
(** Number of set bits in the bit set. *)

val is_empty : t -> bool
(** Whether the bit set holds no set bits. *)

val mem : int -> t -> bool
(** Whether the set bit is present. *)

val add : int -> t -> (bool * t, string) result
(** A bit set containing the given set bit. *)

val remove : int -> t -> bool * t
(** A bit set without that set bit. *)

val toggle : int -> t -> (bool * t, string) result
(** A bit set with that bit flipped. *)

val union : t -> t -> t
(** The set bits of both bit sets. Subtrees the operands already share are adopted whole rather than
    re-entered. *)

val intersect : t -> t -> t
(** The set bits present in both bit sets. *)

val difference : t -> t -> t
(** This bit set's set bits that are absent from the other. *)

val symmetric_difference : t -> t -> t
(** The set bits present in exactly one of the two bit sets. *)

val rank : int -> t -> int
(** [rank index set] counts present bits strictly below [index]. *)

val select : int -> t -> int option
(** The index of the nth set bit, counting from zero. Cached population counts make this a descent
    rather than a scan. *)

val next_set_bit : int -> t -> int option
(** The first set bit at or after the index, or [None] when none is. Cached population counts make
    this a descent rather than a scan. *)

val previous_set_bit : int -> t -> int option
(** The last set bit at or before the index, or [None] when none is. *)

val to_list : t -> int list
(** The set bits, in the bit set's own order. *)

val statistics : t -> statistics
(** Shape measurements from a structural audit. *)
