(** Immutable snapshot-bound cursor for UTF-8 text ropes. *)

type t

val create : ?position:int -> Text_rope.t -> (t, string) result
(** An empty collection using the supplied policies, which it retains. *)

val text : t -> Text_rope.t
(** The text rope version this cursor is positioned in. *)

val position : t -> int
(** The cursor's gap position. *)

val line_column : t -> int * int
(** The cursor's position expressed as a line and column. *)

val move_to : int -> t -> (t, string) result
(** A collection with the element moved to the given position in the insertion order. *)

val move_to_line_column : line:int -> column:int -> t -> (t, string) result
(** A cursor at the given line and column within the same version. *)

val peek_before : t -> Uchar.t option
(** The element immediately before the gap, or [None] at the start. *)

val peek_after : t -> Uchar.t option
(** The element immediately after the gap, or [None] at the end. *)

val insert_utf8 : string -> t -> (t, string) result
(** A rope with the UTF-8 text inserted at the position. *)

val delete_before : int -> t -> (t, string) result
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val delete_after : int -> t -> (t, string) result
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val find_forward : string -> t -> (int option, string) result
(** The next match at or after the position. *)
