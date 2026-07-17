(** Immutable snapshot-bound cursor for UTF-8 text ropes. *)

type t

val create : ?position:int -> Text_rope.t -> (t, string) result
val text : t -> Text_rope.t
val position : t -> int
val line_column : t -> int * int
val move_to : int -> t -> (t, string) result
val move_to_line_column : line:int -> column:int -> t -> (t, string) result
val peek_before : t -> Uchar.t option
val peek_after : t -> Uchar.t option
val insert_utf8 : string -> t -> (t, string) result
val delete_before : int -> t -> (t, string) result
val delete_after : int -> t -> (t, string) result
val find_forward : string -> t -> (int option, string) result
