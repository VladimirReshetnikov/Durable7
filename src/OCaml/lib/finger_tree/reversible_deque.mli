(** Persistent deque with an O(1) logical reversal bit.

    Reversal flips an orientation bit and shares the tree. Concatenation is orientation-aware and
    structural: operands stored in opposite orientations are joined through the underlying tree's
    O(1) lazy reversed view, at O(log(min(n, m))) amortized, never by re-materializing either
    operand. Cursor edits are O(log n) tree splices at the mirrored physical index. *)

type 'element t
type 'element cursor
(** An immutable gap cursor over one version. It holds that exact version, so it stays readable
    however the collection is edited afterwards. *)

val empty : 'element t
(** The empty deque. *)

val of_list : 'element list -> 'element t
(** A deque holding a list's elements, built in bulk rather than by repeated insertion. *)

val length : 'element t -> int
(** Number of elements in the deque. *)

val is_empty : 'element t -> bool
(** Whether the deque holds no elements. *)

val reverse : 'element t -> 'element t
(** The deque in the opposite order. *)

val cons : 'element -> 'element t -> 'element t
(** A deque with the element added at the front. *)

val snoc : 'element t -> 'element -> 'element t
(** A deque with the element added at the back. *)

val first : 'element t -> 'element option
(** The first element. *)

val last : 'element t -> 'element option
(** The last element. *)

val pop_front : 'element t -> ('element * 'element t) option
(** The first element together with the deque remaining. *)

val pop_back : 'element t -> ('element t * 'element) option
(** The last element together with the deque remaining. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. *)

val to_list : 'element t -> 'element list
(** The elements, in the deque's own order. *)

val concat : 'element t -> 'element t -> 'element t
(** The concatenation of two deques, sharing both operands' unchanged structure. O(log(min(n, m)))
    amortized whatever the two operands' orientations: a mismatched pair goes through the O(1)
    lazy reversed view rather than a rebuild. *)

val cursor : 'element t -> 'element cursor
(** A cursor before the first element. *)

val cursor_at : int -> 'element t -> 'element cursor option
(** A cursor at the given gap of the deque. *)

val cursor_position : 'element cursor -> int
(** The cursor's gap position. *)

val cursor_length : 'element cursor -> int
(** Number of elements in the deque version the cursor is positioned in. *)

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
(** A cursor at the given position within the same deque version. *)

val cursor_insert : 'element -> 'element cursor -> 'element cursor
(** A deque containing the given element. O(log n): a tree splice at the mirrored physical
    index. *)

val cursor_insert_many : 'element list -> 'element cursor -> 'element cursor
(** A deque with several elements inserted at the position. O(log n + k) for k elements. *)

val cursor_delete_previous : 'element cursor -> 'element cursor option
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_delete_next : 'element cursor -> 'element cursor option
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_replace_next : 'element -> 'element cursor -> 'element cursor option
(** Replaces the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_reverse : 'element cursor -> 'element cursor
(** The deque in the opposite order. *)

val cursor_snapshot : 'element cursor -> 'element t
(** The deque version this cursor is positioned in. *)
