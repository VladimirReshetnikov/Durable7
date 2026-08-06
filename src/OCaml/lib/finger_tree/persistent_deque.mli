(** Size-measured persistent double-ended sequence. *)

type 'element t
type 'element cursor
(** An immutable gap cursor over one version. It holds that exact version, so it stays readable
    however the collection is edited afterwards. *)

val empty : 'element t
(** The empty deque. *)

val singleton : 'element -> 'element t
(** A deque holding one element. *)

val of_list : 'element list -> 'element t
(** A deque holding a list's elements, built in bulk rather than by repeated insertion. *)

val is_empty : 'element t -> bool
(** Whether the deque holds no elements. *)

val length : 'element t -> int
(** Number of elements in the deque. *)

val to_list : 'element t -> 'element list
(** The elements, in the deque's own order. *)

val cons : 'element -> 'element t -> 'element t
(** A deque with the element added at the front. *)

val snoc : 'element t -> 'element -> 'element t
(** A deque with the element added at the back. *)

val concat : 'element t -> 'element t -> 'element t
(** The concatenation of two deques, sharing both operands' unchanged structure. *)

val reverse : 'element t -> 'element t
(** The deque in the opposite order, sharing storage lazily. O(1): the underlying tree mirrors its
    outer digits and defers the middle's reversal behind a memoized suspension. *)

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

val split_at : int -> 'element t -> 'element t * 'element t
(** Splits into the elements before the position and those from it onward. *)

val take : int -> 'element t -> 'element t
(** The first n elements. *)

val drop : int -> 'element t -> 'element t
(** The elements after the first n. *)

val slice : start:int -> length:int -> 'element t -> ('element t, string) result
(** The elements in the given range. *)

val insert_at : int -> 'element -> 'element t -> ('element t, string) result
(** A deque with the element inserted at the position. *)

val update_at : int -> ('element -> 'element) -> 'element t -> ('element t, string) result
(** A deque with the element at the position replaced. *)

val remove_at : int -> 'element t -> ('element * 'element t, string) result
(** A deque without the element at the position. *)

val iter : ('element -> unit) -> 'element t -> unit
(** Applies the function to each element, in the deque's own order. *)

val fold_left : ('state -> 'element -> 'state) -> 'state -> 'element t -> 'state
(** Folds over the elements from the left. *)

val validate : 'element t -> (unit, string) result
(** Checks the deque's structural invariants. For tests and diagnostics. *)

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
(** A deque containing the given element. *)

val cursor_insert_many : 'element list -> 'element cursor -> 'element cursor
(** A deque with several elements inserted at the position. *)

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
(** The deque version this cursor is positioned in. *)
