(** Persistent indexed vector facade over the shared balanced sequence. *)

type 'element t
type 'element cursor
(** An immutable gap cursor over one version. It holds that exact version, so it stays readable
    however the collection is edited afterwards. *)

type statistics = { count : int; estimated_leaves : int; depth : int }
(** Shape measurements returned by a structural audit. *)

val empty : 'element t
(** The empty vector. *)

val singleton : 'element -> 'element t
(** A vector holding one element. *)

val of_list : 'element list -> 'element t
(** A vector holding a list's elements, built in bulk rather than by repeated insertion. *)

val length : 'element t -> int
(** Number of elements in the vector. *)

val is_empty : 'element t -> bool
(** Whether the vector holds no elements. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. *)

val set : int -> 'element -> 'element t -> ('element t, string) result
(** A vector with the key bound to the value, adding or replacing as needed. *)

val append : 'element -> 'element t -> 'element t
(** The concatenation of two vectors, sharing both operands' unchanged structure. *)

val prepend : 'element -> 'element t -> 'element t
(** A vector with the element added at the front. *)

val concat : 'element t -> 'element t -> 'element t
(** The concatenation of two vectors, sharing both operands' unchanged structure. *)

val insert : int -> 'element -> 'element t -> ('element t, string) result
(** A vector containing the given element. *)

val remove : int -> 'element t -> ('element * 'element t, string) result
(** A vector without that element. *)

val pop : 'element t -> ('element t * 'element) option
(** The first element together with the vector remaining. *)

val split_at : int -> 'element t -> 'element t * 'element t
(** Splits into the elements before the position and those from it onward. *)

val slice : start:int -> length:int -> 'element t -> ('element t, string) result
(** The elements in the given range. *)

val to_list : 'element t -> 'element list
(** The elements, in the vector's own order. *)

val statistics : 'element t -> statistics
(** Shape measurements from a structural audit. *)

val cursor : 'element t -> 'element cursor
(** A cursor before the first element. *)

val cursor_at : int -> 'element t -> 'element cursor option
(** A cursor at the given gap of the vector. *)

val cursor_position : 'element cursor -> int
(** The cursor's gap position. *)

val cursor_length : 'element cursor -> int
(** Number of elements in the vector version the cursor is positioned in. *)

val cursor_is_at_start : 'element cursor -> bool
(** Whether the gap precedes the first element. *)

val cursor_is_at_end : 'element cursor -> bool
(** Whether the gap follows the last element. *)

val cursor_peek_previous : 'element cursor -> 'element option
(** The element immediately before the gap, or [None] at the start. *)

val cursor_peek_next : 'element cursor -> 'element option
(** The element immediately after the gap, or [None] at the end. *)

val cursor_move_previous : 'element cursor -> 'element cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val cursor_move_next : 'element cursor -> 'element cursor option
(** A cursor one position later. The receiver is unchanged. *)

val cursor_seek : int -> 'element cursor -> 'element cursor option
(** A cursor at the given position within the same vector version. *)

val cursor_insert : 'element -> 'element cursor -> 'element cursor
(** A vector containing the given element. *)

val cursor_insert_many : 'element list -> 'element cursor -> 'element cursor
(** A vector with several elements inserted at the position. *)

val cursor_insert_vector : 'element t -> 'element cursor -> 'element cursor
(** Inserts a vector's elements at the gap, producing a new version the returned cursor is
    positioned in. *)

val cursor_delete_previous : 'element cursor -> 'element cursor option
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_delete_next : 'element cursor -> 'element cursor option
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_replace_next : 'element -> 'element cursor -> 'element cursor option
(** Replaces the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_snapshot : 'element cursor -> 'element t
(** The vector version this cursor is positioned in. *)

module Builder : sig
  type 'element vector = 'element t
  type 'element t
(** A mutable accumulator for building a collection in bulk. Deliberately not a snapshot: it fills a
    buffer and produces a persistent value only on demand, which is cheaper than one version per
    element. *)

  val create : 'element vector -> 'element t
  val length : 'element t -> int
  val append : 'element -> 'element t -> unit
  val set : int -> 'element -> 'element t -> (unit, string) result
  val remove : int -> 'element t -> ('element, string) result
  val freeze : 'element t -> 'element vector
end
