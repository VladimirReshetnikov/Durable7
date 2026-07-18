(** Immutable snapshot-plus-gap cursors for neutral insertion-ordered collections. *)

type 'cursor search = { found : bool; search_cursor : 'cursor }
type 'cursor insertion = { added : bool; insertion_cursor : 'cursor }

val search_found : 'cursor search -> bool
val search_value : 'cursor search -> 'cursor
val insertion_added : 'cursor insertion -> bool
val insertion_value : 'cursor insertion -> 'cursor

type 'element ordered_set_cursor

val ordered_set_at : int -> 'element Persistent_ordered_set.t -> 'element ordered_set_cursor option

val ordered_set_find :
  'element -> 'element Persistent_ordered_set.t -> 'element ordered_set_cursor search

val ordered_set_position : 'element ordered_set_cursor -> int
val ordered_set_count : 'element ordered_set_cursor -> int
val ordered_set_at_start : 'element ordered_set_cursor -> bool
val ordered_set_at_end : 'element ordered_set_cursor -> bool
val ordered_set_peek_previous : 'element ordered_set_cursor -> 'element option
val ordered_set_peek_next : 'element ordered_set_cursor -> 'element option
val ordered_set_move_previous : 'element ordered_set_cursor -> 'element ordered_set_cursor option
val ordered_set_move_next : 'element ordered_set_cursor -> 'element ordered_set_cursor option
val ordered_set_seek : int -> 'element ordered_set_cursor -> 'element ordered_set_cursor option
val ordered_set_insert : 'element -> 'element ordered_set_cursor -> 'element ordered_set_cursor

val ordered_set_try_insert :
  'element -> 'element ordered_set_cursor -> 'element ordered_set_cursor insertion

val ordered_set_delete_previous : 'element ordered_set_cursor -> 'element ordered_set_cursor option
val ordered_set_delete_next : 'element ordered_set_cursor -> 'element ordered_set_cursor option
val ordered_set_snapshot : 'element ordered_set_cursor -> 'element Persistent_ordered_set.t

type ('key, 'value) ordered_map_cursor

val ordered_map_at :
  int -> ('key, 'value) Persistent_ordered_map.t -> ('key, 'value) ordered_map_cursor option

val ordered_map_find :
  'key -> ('key, 'value) Persistent_ordered_map.t -> ('key, 'value) ordered_map_cursor search

val ordered_map_position : ('key, 'value) ordered_map_cursor -> int
val ordered_map_count : ('key, 'value) ordered_map_cursor -> int
val ordered_map_at_start : ('key, 'value) ordered_map_cursor -> bool
val ordered_map_at_end : ('key, 'value) ordered_map_cursor -> bool
val ordered_map_peek_previous : ('key, 'value) ordered_map_cursor -> ('key * 'value) option
val ordered_map_peek_next : ('key, 'value) ordered_map_cursor -> ('key * 'value) option

val ordered_map_move_previous :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option

val ordered_map_move_next :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option

val ordered_map_seek :
  int -> ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option

val ordered_map_insert :
  'key ->
  'value ->
  ('key, 'value) ordered_map_cursor ->
  (('key, 'value) ordered_map_cursor, string) result

val ordered_map_try_insert :
  'key -> 'value -> ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor insertion

val ordered_map_set_next_value :
  'value -> ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option

val ordered_map_delete_previous :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option

val ordered_map_delete_next :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) ordered_map_cursor option

val ordered_map_snapshot :
  ('key, 'value) ordered_map_cursor -> ('key, 'value) Persistent_ordered_map.t

type ('key, 'value) ordered_multimap_cursor

val ordered_multimap_at :
  int ->
  ('key, 'value) Persistent_ordered_multimap.t ->
  ('key, 'value) ordered_multimap_cursor option

val ordered_multimap_find :
  'key ->
  'value ->
  ('key, 'value) Persistent_ordered_multimap.t ->
  ('key, 'value) ordered_multimap_cursor search

val ordered_multimap_find_group :
  'key ->
  ('key, 'value) Persistent_ordered_multimap.t ->
  ('key, 'value) ordered_multimap_cursor search

val ordered_multimap_position : ('key, 'value) ordered_multimap_cursor -> int
val ordered_multimap_pair_count : ('key, 'value) ordered_multimap_cursor -> int
val ordered_multimap_at_start : ('key, 'value) ordered_multimap_cursor -> bool
val ordered_multimap_at_end : ('key, 'value) ordered_multimap_cursor -> bool

val ordered_multimap_peek_previous :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) Persistent_ordered_multimap.entry option

val ordered_multimap_peek_next :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) Persistent_ordered_multimap.entry option

val ordered_multimap_move_previous :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option

val ordered_multimap_move_next :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option

val ordered_multimap_seek :
  int -> ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option

val ordered_multimap_insert :
  'key -> 'value -> ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor

val ordered_multimap_try_insert :
  'key ->
  'value ->
  ('key, 'value) ordered_multimap_cursor ->
  ('key, 'value) ordered_multimap_cursor insertion

val ordered_multimap_delete_previous :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option

val ordered_multimap_delete_next :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) ordered_multimap_cursor option

val ordered_multimap_snapshot :
  ('key, 'value) ordered_multimap_cursor -> ('key, 'value) Persistent_ordered_multimap.t
