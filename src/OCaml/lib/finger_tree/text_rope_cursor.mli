(** Immutable snapshot-bound cursor for UTF-8 text ropes. *)

type t
(** A retained text-rope version together with a gap position in it, counted in Unicode scalar
    values rather than bytes. The cursor names a gap {e between} code points, so insertion and
    deletion have an unambiguous target. Cursors are values: every edit returns a new cursor over a
    new text version and leaves the receiver untouched. *)

val create : ?position:int -> Text_rope.t -> (t, string) result
(** A cursor over the given text version at [position], defaulting to the gap before the first code
    point. Reports an error when the position lies outside [0..length]. *)

val text : t -> Text_rope.t
(** The text rope version this cursor is positioned in. *)

val position : t -> int
(** The cursor's gap position. *)

val line_column : t -> int * int
(** The cursor's position expressed as a line and column. *)

val move_to : int -> t -> (t, string) result
(** A cursor at the given code-point gap of the same text version, reporting an error when the
    position lies outside [0..length]. Moves the cursor; the text is unchanged. *)

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
