(** Law-gated persistent sequence with range updates and cached range measures. *)

type ('element, 'measure, 'tag) algebra
type ('element, 'measure, 'tag) t
(** A persistent sequence with lazy range updates. A range update is recorded as a tag at the nodes
    covering the range rather than applied to each element, so the cost tracks the tree's height and
    not the range's length. *)

type ('element, 'measure, 'tag) cursor
(** An immutable gap cursor over one version. It holds that exact version, so it stays readable
    however the collection is edited afterwards. *)

val create_algebra :
  id:string ->
  identity:'measure ->
  combine:('measure -> 'measure -> 'measure) ->
  measure:('element -> 'measure) ->
  apply_element:('tag -> 'element -> 'element) ->
  apply_measure:('tag -> length:int -> 'measure -> 'measure) ->
  compose:('tag -> 'tag -> 'tag) ->
  laws_verified:bool ->
  unit ->
  (('element, 'measure, 'tag) algebra, string) result
(** A tag algebra from its identity, composition, and application. Composition must be associative
    and application must distribute over the measure; those laws are what let a pending tag be
    pushed down or folded into a measure on demand. *)

val algebra_id : ('element, 'measure, 'tag) algebra -> string
(** The algebra's identity tag, whose application is a no-op. *)

val compose_tags : ('element, 'measure, 'tag) algebra -> 'tag -> 'tag -> 'tag
(** Composes two tags, the outer applied after the inner. *)

val empty : ('element, 'measure, 'tag) algebra -> ('element, 'measure, 'tag) t
(** The empty sequence. *)

val of_list : ('element, 'measure, 'tag) algebra -> 'element list -> ('element, 'measure, 'tag) t
(** A sequence holding a list's elements, built in bulk rather than by repeated insertion. *)

val length : ('element, 'measure, 'tag) t -> int
(** Number of elements in the sequence. *)

val nth : int -> ('element, 'measure, 'tag) t -> 'element option
(** The element at the given rank. *)

val to_list : ('element, 'measure, 'tag) t -> 'element list
(** The elements, in the sequence's own order. *)

val measure : ('element, 'measure, 'tag) t -> 'measure
(** The combined measure of every element, read from the cached root measure. *)

val measure_range :
  start:int -> length:int -> ('element, 'measure, 'tag) t -> ('measure, string) result
(** The combined measure of the elements in the range. *)

val update_range :
  start:int ->
  length:int ->
  'tag ->
  ('element, 'measure, 'tag) t ->
  (('element, 'measure, 'tag) t, string) result
(** A sequence with the tag applied over the range, recorded at the covering nodes rather than at
    each element. *)

val insert :
  int -> 'element -> ('element, 'measure, 'tag) t -> (('element, 'measure, 'tag) t, string) result
(** A sequence containing the given element. *)

val remove :
  int -> ('element, 'measure, 'tag) t -> ('element * ('element, 'measure, 'tag) t, string) result
(** A sequence without that element. *)

val split_at :
  int -> ('element, 'measure, 'tag) t -> ('element, 'measure, 'tag) t * ('element, 'measure, 'tag) t
(** Splits into the elements before the position and those from it onward. *)

val cursor : ('element, 'measure, 'tag) t -> ('element, 'measure, 'tag) cursor
(** A cursor before the first element. *)

val cursor_at : int -> ('element, 'measure, 'tag) t -> ('element, 'measure, 'tag) cursor option
(** A cursor at the given gap of the sequence. *)

val cursor_position : ('element, 'measure, 'tag) cursor -> int
(** The cursor's gap position. *)

val cursor_length : ('element, 'measure, 'tag) cursor -> int
(** Number of elements in the sequence version the cursor is positioned in. *)

val cursor_is_at_start : ('element, 'measure, 'tag) cursor -> bool
(** Whether the gap precedes the first element. *)

val cursor_is_at_end : ('element, 'measure, 'tag) cursor -> bool
(** Whether the gap follows the last element. *)

val cursor_measure_before : ('element, 'measure, 'tag) cursor -> 'measure
(** The combined measure of everything before the gap. *)

val cursor_measure_after : ('element, 'measure, 'tag) cursor -> 'measure
(** The combined measure of everything after the gap. *)

val cursor_peek_previous : ('element, 'measure, 'tag) cursor -> 'element option
(** The element immediately before the gap, or [None] at the start. *)

val cursor_peek_next : ('element, 'measure, 'tag) cursor -> 'element option
(** The element immediately after the gap, or [None] at the end. *)

val cursor_move_previous :
  ('element, 'measure, 'tag) cursor -> ('element, 'measure, 'tag) cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val cursor_move_next : ('element, 'measure, 'tag) cursor -> ('element, 'measure, 'tag) cursor option
(** A cursor one position later. The receiver is unchanged. *)

val cursor_seek :
  int -> ('element, 'measure, 'tag) cursor -> ('element, 'measure, 'tag) cursor option
(** A cursor at the given position within the same sequence version. *)

val cursor_insert :
  'element -> ('element, 'measure, 'tag) cursor -> ('element, 'measure, 'tag) cursor
(** A sequence containing the given element. *)

val cursor_delete_previous :
  ('element, 'measure, 'tag) cursor -> ('element, 'measure, 'tag) cursor option
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_delete_next :
  ('element, 'measure, 'tag) cursor -> ('element, 'measure, 'tag) cursor option
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_replace_next :
  'element -> ('element, 'measure, 'tag) cursor -> ('element, 'measure, 'tag) cursor option
(** Replaces the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val cursor_measure_previous : int -> ('element, 'measure, 'tag) cursor -> ('measure, string) result
(** The measure of the element immediately before the gap. *)

val cursor_measure_next : int -> ('element, 'measure, 'tag) cursor -> ('measure, string) result
(** The measure of the element immediately after the gap. *)

val cursor_update_previous :
  int ->
  'tag ->
  ('element, 'measure, 'tag) cursor ->
  (('element, 'measure, 'tag) cursor, string) result
(** Applies the pending tags covering the element before the gap. *)

val cursor_update_next :
  int ->
  'tag ->
  ('element, 'measure, 'tag) cursor ->
  (('element, 'measure, 'tag) cursor, string) result
(** Applies the pending tags covering the element after the gap. *)

val cursor_snapshot : ('element, 'measure, 'tag) cursor -> ('element, 'measure, 'tag) t
(** The sequence version this cursor is positioned in. *)
