(** UTF-8 text rope indexed by Unicode scalar value. *)

type t

val empty : t
val of_utf8 : string -> (t, string) result
val to_utf8 : t -> string
val to_uchars : t -> Uchar.t list
val length : t -> int
val is_empty : t -> bool
val concat : t -> t -> t
val nth : int -> t -> Uchar.t option
val split_at : int -> t -> t * t
val substring : start:int -> length:int -> t -> (t, string) result
val insert_utf8 : int -> string -> t -> (t, string) result
val remove : start:int -> length:int -> t -> (t, string) result
val line_count : t -> int
val line_column : int -> t -> (int * int, string) result
val index_of_line_column : line:int -> column:int -> t -> (int, string) result
