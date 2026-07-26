(** Immutable snapshot-plus-position cursor for a generic rope. *)

type 'element t

val create : ?position:int -> 'element Rope.t -> ('element t, string) result
(** An empty collection using the supplied policies, which it retains. *)

val rope : 'element t -> 'element Rope.t
(** The rope version this cursor is positioned in. *)

val position : 'element t -> int
(** The cursor's gap position. *)

val move_to : int -> 'element t -> ('element t, string) result
(** A collection with the element moved to the given position in the insertion order. *)

val peek_before : 'element t -> 'element option
(** The element immediately before the gap, or [None] at the start. *)

val peek_after : 'element t -> 'element option
(** The element immediately after the gap, or [None] at the end. *)

val insert : 'element -> 'element t -> 'element t
(** A collection containing the given element. *)

val insert_many : 'element list -> 'element t -> 'element t
(** A collection with several elements inserted at the position. *)

val delete_before : int -> 'element t -> ('element t, string) result
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val delete_after : int -> 'element t -> ('element t, string) result
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)
