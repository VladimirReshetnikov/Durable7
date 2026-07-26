(** Immutable snapshot-plus-gap cursors for neutral insertion-ordered collections. *)

type 'cursor search = { found : bool; search_cursor : 'cursor }
type 'cursor insertion = { added : bool; insertion_cursor : 'cursor }
(** The outcome of a cursor insertion: whether anything was added, and the cursor positioned in the
    resulting version. *)

val search_found : 'cursor search -> bool
(** Whether the search found an exact match. *)

val search_value : 'cursor search -> 'cursor
(** The value the search landed on. *)

val insertion_added : 'cursor insertion -> bool
(** Whether the insertion added anything. *)

val insertion_value : 'cursor insertion -> 'cursor
(** The version the insertion produced. *)

type 'element ordered_set_cursor
(** A gap cursor over one insertion-ordered set version. *)

val ordered_set_at : int -> 'element Persistent_ordered_set.t -> 'element ordered_set_cursor option
(** A cursor at the given gap of the set. *)

val ordered_set_find :
  'element -> 'element Persistent_ordered_set.t -> 'element ordered_set_cursor search
(** The element matching the probe. Raises when absent. *)

val ordered_set_position : 'element ordered_set_cursor -> int
(** The cursor's gap position. *)

val ordered_set_count : 'element ordered_set_cursor -> int
(** Number of elements in the set version the cursor is positioned in. *)

val ordered_set_at_start : 'element ordered_set_cursor -> bool
(** Whether the gap precedes the first element. *)

val ordered_set_at_end : 'element ordered_set_cursor -> bool
(** Whether the gap follows the last element. *)

val ordered_set_peek_previous : 'element ordered_set_cursor -> 'element option
(** The element immediately before the gap, or [None] at the start. *)

val ordered_set_peek_next : 'element ordered_set_cursor -> 'element option
(** The element immediately after the gap, or [None] at the end. *)

val ordered_set_move_previous : 'element ordered_set_cursor -> 'element ordered_set_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val ordered_set_move_next : 'element ordered_set_cursor -> 'element ordered_set_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val ordered_set_seek : int -> 'element ordered_set_cursor -> 'element ordered_set_cursor option
(** A cursor at the given position within the same set version. *)

val ordered_set_insert : 'element -> 'element ordered_set_cursor -> 'element ordered_set_cursor
(** A set containing the given element. *)

val ordered_set_try_insert :
  'element -> 'element ordered_set_cursor -> 'element ordered_set_cursor insertion
(** Inserts the element unless an equivalent one is present. *)

val ordered_set_delete_previous : 'element ordered_set_cursor -> 'element ordered_set_cursor option
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val ordered_set_delete_next : 'element ordered_set_cursor -> 'element ordered_set_cursor option
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val ordered_set_snapshot : 'element ordered_set_cursor -> 'element Persistent_ordered_set.t
(** The set version this cursor is positioned in. *)

type ('key, 'value) ordered_map_cursor
(** A gap cursor over one insertion-ordered map version. *)

val ordered_map_at :
  int -> ('key, 'value) Persistent_ordered_map.t -> ('key, 'value) ordered_map_cursor option
(** A cursor at the given gap of the map. *)

val ordered_map_find :
  'key -> ('key, 'value) Persistent_ordered_map.t -> ('key, 'value) ordered_map_cursor search
(** The entry matching the probe. Raises when absent. *)

val ordered_map_position : ('key, 'value) ordered_map_cursor -> int
(** The cursor's gap position. *)

val ordered_map_count : ('key, 'value) ordered_map_cursor -> int
(** Number of entries in the map version the cursor is positioned in. *)

val ordered_map_at_start : ('key, 'value) ordered_map_cursor -> bool
(** Whether the gap precedes the first entry. *)

val ordered_map_at_end : ('key, 'value) ordered_map_cursor -> bool
(** Whether the gap follows the last entry. *)

val ordered_map_peek_previous : ('key, 'value) ordered_map_cursor -> ('key * 'value) option
(** The entry immediately before the gap, or [None] at the start. *)

val ordered_map_peek_next : ('key, 'value) ordered_map_cursor -> ('key * 'value) option
(** The entry immediately after the gap, or [None] at the end. *)

val ordered_map_move_previous :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val ordered_map_move_next :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val ordered_map_seek :
  int -> ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option
(** A cursor at the given position within the same map version. *)

val ordered_map_insert :
  'key ->
  'value ->
  ('key, 'value) ordered_map_cursor ->
  (('key, 'value) ordered_map_cursor, string) result
(** A map containing the given entry. *)

val ordered_map_try_insert :
  'key -> 'value -> ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor insertion
(** Inserts the entry unless an equivalent one is present. *)

val ordered_map_set_next_value :
  'value -> ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option
(** Replaces the value of the entry after the gap, producing a new version the returned cursor is
    positioned in. *)

val ordered_map_delete_previous :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option
(** Removes the entry before the gap, producing a new version the returned cursor is positioned
    in. *)

val ordered_map_delete_next :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option
(** Removes the entry after the gap, producing a new version the returned cursor is positioned
    in. *)

val ordered_map_snapshot :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) Persistent_ordered_map.t
(** The map version this cursor is positioned in. *)

type ('key, 'value) ordered_multimap_cursor
(** A gap cursor over one insertion-ordered multimap version. *)

val ordered_multimap_at :
  int ->
  ('key, 'value) Persistent_ordered_multimap.t ->
  ('key, 'value) ordered_multimap_cursor option
(** A cursor at the given gap of the multimap. *)

val ordered_multimap_find :
  'key ->
  'value ->
  ('key, 'value) Persistent_ordered_multimap.t ->
  ('key, 'value) ordered_multimap_cursor search
(** The pair matching the probe. Raises when absent. *)

val ordered_multimap_find_group :
  'key ->
  ('key, 'value) Persistent_ordered_multimap.t ->
  ('key, 'value) ordered_multimap_cursor search
(** A cursor at the start of the key's group of values. *)

val ordered_multimap_position : ('key, 'value) ordered_multimap_cursor -> int
(** The cursor's gap position. *)

val ordered_multimap_pair_count : ('key, 'value) ordered_multimap_cursor -> int
(** How many pairs are present in total. *)

val ordered_multimap_at_start : ('key, 'value) ordered_multimap_cursor -> bool
(** Whether the gap precedes the first pair. *)

val ordered_multimap_at_end : ('key, 'value) ordered_multimap_cursor -> bool
(** Whether the gap follows the last pair. *)

val ordered_multimap_peek_previous :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) Persistent_ordered_multimap.entry option
(** The pair immediately before the gap, or [None] at the start. *)

val ordered_multimap_peek_next :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) Persistent_ordered_multimap.entry option
(** The pair immediately after the gap, or [None] at the end. *)

val ordered_multimap_move_previous :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val ordered_multimap_move_next :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val ordered_multimap_seek :
  int -> ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option
(** A cursor at the given position within the same multimap version. *)

val ordered_multimap_insert :
  'key -> 'value -> ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor
(** A multimap containing the given pair. *)

val ordered_multimap_try_insert :
  'key ->
  'value ->
  ('key, 'value) ordered_multimap_cursor ->
  ('key, 'value) ordered_multimap_cursor insertion
(** Inserts the pair unless an equivalent one is present. *)

val ordered_multimap_delete_previous :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option
(** Removes the pair before the gap, producing a new version the returned cursor is positioned
    in. *)

val ordered_multimap_delete_next :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option
(** Removes the pair after the gap, producing a new version the returned cursor is positioned in. *)

val ordered_multimap_snapshot :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) Persistent_ordered_multimap.t
(** The multimap version this cursor is positioned in. *)
