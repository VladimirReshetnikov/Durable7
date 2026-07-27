(** Immutable snapshot-plus-position cursor for a generic rope. *)

type 'element t
(** A retained rope version together with a gap position in it. The cursor names a gap {e between}
    elements, so insertion and deletion have an unambiguous target and an empty rope still has one
    valid position. Cursors are values: every edit returns a new cursor over a new rope version and
    leaves the receiver, and the version it holds, untouched. *)

val create : ?position:int -> 'element Rope.t -> ('element t, string) result
(** A cursor over the given rope version at [position], defaulting to the gap before the first
    element. Reports an error when the position lies outside [0..length]. *)

val rope : 'element t -> 'element Rope.t
(** The rope version this cursor is positioned in. *)

val position : 'element t -> int
(** The cursor's gap position. *)

val move_to : int -> 'element t -> ('element t, string) result
(** A cursor at the given gap of the same rope version, reporting an error when the position lies
    outside [0..length]. Moves the cursor; the rope is unchanged. *)

val peek_before : 'element t -> 'element option
(** The element immediately before the gap, or [None] at the start. *)

val peek_after : 'element t -> 'element option
(** The element immediately after the gap, or [None] at the end. *)

val insert : 'element -> 'element t -> 'element t
(** Insert one element at the gap and return a cursor positioned after it, over the new rope
    version. Cursors retained beforehand keep their own version and never see the insertion. *)

val insert_many : 'element list -> 'element t -> 'element t
(** A collection with several elements inserted at the position. *)

val delete_before : int -> 'element t -> ('element t, string) result
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val delete_after : int -> 'element t -> ('element t, string) result
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)
