(** UTF-8 text rope indexed by Unicode scalar value. *)

type t

val empty : t
(** The empty rope. *)

val of_utf8 : string -> (t, string) result
(** A text rope holding the given UTF-8 text. *)

val to_utf8 : t -> string
(** The text as UTF-8. *)

val to_uchars : t -> Uchar.t list
(** The text as Unicode scalar values. *)

val length : t -> int
(** Number of characters in the rope. *)

val is_empty : t -> bool
(** Whether the rope holds no characters. *)

val concat : t -> t -> t
(** The concatenation of two ropes, sharing both operands' unchanged structure. *)

val nth : int -> t -> Uchar.t option
(** The character at the given rank. *)

val split_at : int -> t -> t * t
(** Splits into the characters before the position and those from it onward. *)

val substring : start:int -> length:int -> t -> (t, string) result
(** The sub-range of the text. *)

val insert_utf8 : int -> string -> t -> (t, string) result
(** A rope with the UTF-8 text inserted at the position. *)

val remove : start:int -> length:int -> t -> (t, string) result
(** A rope without that character. *)

val line_count : t -> int
(** How many lines the text holds. Newline counts are cached in the measure, so this is a cached
    read. *)

val line_column : int -> t -> (int * int, string) result
(** The line and column of a character offset. *)

val index_of_line_column : line:int -> column:int -> t -> (int, string) result
(** The character offset of a line and column. *)
