(** Immutable root-plus-rank gap cursors for ordered persistent collections. *)

type 'cursor search = { found : bool; search_cursor : 'cursor }
type 'cursor insertion = { added : bool; insertion_cursor : 'cursor }

val search_found : 'cursor search -> bool
val search_value : 'cursor search -> 'cursor
val insertion_added : 'cursor insertion -> bool
val insertion_value : 'cursor insertion -> 'cursor

type 'element sorted_bag_cursor

val sorted_bag_at : int -> 'element Sorted_bag.t -> 'element sorted_bag_cursor option
val sorted_bag_lower_bound : 'element -> 'element Sorted_bag.t -> 'element sorted_bag_cursor
val sorted_bag_upper_bound : 'element -> 'element Sorted_bag.t -> 'element sorted_bag_cursor
val sorted_bag_find : 'element -> 'element Sorted_bag.t -> 'element sorted_bag_cursor search
val sorted_bag_position : 'element sorted_bag_cursor -> int
val sorted_bag_peek_previous : 'element sorted_bag_cursor -> 'element option
val sorted_bag_peek_next : 'element sorted_bag_cursor -> 'element option
val sorted_bag_move_previous : 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
val sorted_bag_move_next : 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
val sorted_bag_seek_rank : int -> 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
val sorted_bag_add : 'element -> 'element sorted_bag_cursor -> 'element sorted_bag_cursor
val sorted_bag_delete_previous : 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
val sorted_bag_delete_next : 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
val sorted_bag_snapshot : 'element sorted_bag_cursor -> 'element Sorted_bag.t

type 'element sorted_set_cursor

val sorted_set_at : int -> 'element Sorted_set.t -> 'element sorted_set_cursor option
val sorted_set_lower_bound : 'element -> 'element Sorted_set.t -> 'element sorted_set_cursor
val sorted_set_upper_bound : 'element -> 'element Sorted_set.t -> 'element sorted_set_cursor
val sorted_set_find : 'element -> 'element Sorted_set.t -> 'element sorted_set_cursor search
val sorted_set_position : 'element sorted_set_cursor -> int
val sorted_set_peek_previous : 'element sorted_set_cursor -> 'element option
val sorted_set_peek_next : 'element sorted_set_cursor -> 'element option
val sorted_set_move_previous : 'element sorted_set_cursor -> 'element sorted_set_cursor option
val sorted_set_move_next : 'element sorted_set_cursor -> 'element sorted_set_cursor option
val sorted_set_seek_rank : int -> 'element sorted_set_cursor -> 'element sorted_set_cursor option
val sorted_set_add : 'element -> 'element sorted_set_cursor -> 'element sorted_set_cursor
val sorted_set_delete_previous : 'element sorted_set_cursor -> 'element sorted_set_cursor option
val sorted_set_delete_next : 'element sorted_set_cursor -> 'element sorted_set_cursor option
val sorted_set_snapshot : 'element sorted_set_cursor -> 'element Sorted_set.t

type ('key, 'value) sorted_map_cursor

val sorted_map_at : int -> ('key, 'value) Sorted_map.t -> ('key, 'value) sorted_map_cursor option
val sorted_map_lower_bound : 'key -> ('key, 'value) Sorted_map.t -> ('key, 'value) sorted_map_cursor
val sorted_map_upper_bound : 'key -> ('key, 'value) Sorted_map.t -> ('key, 'value) sorted_map_cursor
val sorted_map_find : 'key -> ('key, 'value) Sorted_map.t -> ('key, 'value) sorted_map_cursor search
val sorted_map_position : ('key, 'value) sorted_map_cursor -> int
val sorted_map_peek_previous : ('key, 'value) sorted_map_cursor -> ('key * 'value) option
val sorted_map_peek_next : ('key, 'value) sorted_map_cursor -> ('key * 'value) option

val sorted_map_move_previous :
  ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option

val sorted_map_move_next :
  ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option

val sorted_map_seek_rank :
  int -> ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option

val sorted_map_insert :
  'key ->
  'value ->
  ('key, 'value) sorted_map_cursor ->
  (('key, 'value) sorted_map_cursor, string) result

val sorted_map_try_insert :
  'key -> 'value -> ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor insertion

val sorted_map_set :
  'key -> 'value -> ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor

val sorted_map_set_next_value :
  'value -> ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option

val sorted_map_delete_previous :
  ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option

val sorted_map_delete_next :
  ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option

val sorted_map_snapshot : ('key, 'value) sorted_map_cursor -> ('key, 'value) Sorted_map.t

type 'element canonical_cursor

val canonical_at : int -> 'element Canonical_sorted_set.t -> 'element canonical_cursor option
val canonical_lower_bound : 'element -> 'element Canonical_sorted_set.t -> 'element canonical_cursor
val canonical_upper_bound : 'element -> 'element Canonical_sorted_set.t -> 'element canonical_cursor
val canonical_find : 'element -> 'element Canonical_sorted_set.t -> 'element canonical_cursor search
val canonical_position : 'element canonical_cursor -> int
val canonical_peek_previous : 'element canonical_cursor -> 'element option
val canonical_peek_next : 'element canonical_cursor -> 'element option
val canonical_move_previous : 'element canonical_cursor -> 'element canonical_cursor option
val canonical_move_next : 'element canonical_cursor -> 'element canonical_cursor option
val canonical_seek_rank : int -> 'element canonical_cursor -> 'element canonical_cursor option
val canonical_add : 'element -> 'element canonical_cursor -> 'element canonical_cursor
val canonical_delete_previous : 'element canonical_cursor -> 'element canonical_cursor option
val canonical_delete_next : 'element canonical_cursor -> 'element canonical_cursor option
val canonical_snapshot : 'element canonical_cursor -> 'element Canonical_sorted_set.t

type ('key, 'priority, 'value) priority_search_cursor

val priority_search_at :
  int ->
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor option

val priority_search_lower_bound :
  'key ->
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor

val priority_search_upper_bound :
  'key ->
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor

val priority_search_find :
  'key ->
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor search

val priority_search_minimum :
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor

val priority_search_position : ('key, 'priority, 'value) priority_search_cursor -> int

val priority_search_peek_previous :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) Priority_search_queue.entry option

val priority_search_peek_next :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) Priority_search_queue.entry option

val priority_search_move_previous :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option

val priority_search_move_next :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option

val priority_search_seek_rank :
  int ->
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option

val priority_search_try_insert :
  'key ->
  'priority ->
  'value ->
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor insertion

val priority_search_set :
  'key ->
  'priority ->
  'value ->
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor

val priority_search_set_next :
  'priority ->
  'value ->
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option

val priority_search_delete_previous :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option

val priority_search_delete_next :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option

val priority_search_snapshot :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) Priority_search_queue.t

type 'endpoint interval_tree_cursor

val interval_tree_at : int -> 'endpoint Interval_tree.t -> 'endpoint interval_tree_cursor option

val interval_tree_lower_bound :
  'endpoint -> 'endpoint Interval_tree.t -> 'endpoint interval_tree_cursor

val interval_tree_upper_bound :
  'endpoint -> 'endpoint Interval_tree.t -> 'endpoint interval_tree_cursor

val interval_tree_find :
  'endpoint Interval_tree.interval ->
  'endpoint Interval_tree.t ->
  'endpoint interval_tree_cursor search

val interval_tree_find_overlap :
  'endpoint Interval_tree.interval ->
  'endpoint Interval_tree.t ->
  'endpoint interval_tree_cursor search

val interval_tree_find_containing :
  'endpoint -> 'endpoint Interval_tree.t -> 'endpoint interval_tree_cursor search

val interval_tree_position : 'endpoint interval_tree_cursor -> int

val interval_tree_peek_previous :
  'endpoint interval_tree_cursor -> 'endpoint Interval_tree.interval option

val interval_tree_peek_next :
  'endpoint interval_tree_cursor -> 'endpoint Interval_tree.interval option

val interval_tree_move_previous :
  'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option

val interval_tree_move_next :
  'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option

val interval_tree_seek_rank :
  int -> 'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option

val interval_tree_seek_next_overlap :
  'endpoint Interval_tree.interval ->
  'endpoint interval_tree_cursor ->
  'endpoint interval_tree_cursor search

val interval_tree_insert :
  'endpoint Interval_tree.interval ->
  'endpoint interval_tree_cursor ->
  'endpoint interval_tree_cursor

val interval_tree_delete_previous :
  'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option

val interval_tree_delete_next :
  'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option

val interval_tree_snapshot : 'endpoint interval_tree_cursor -> 'endpoint Interval_tree.t

type ('endpoint, 'value) interval_map_cursor

val interval_map_at :
  int ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor option

val interval_map_lower_bound :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor

val interval_map_upper_bound :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor

val interval_map_find :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor search

val interval_map_find_overlap :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  (('endpoint, 'value) interval_map_cursor search, string) result

val interval_map_find_containing :
  'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor search

val interval_map_position : ('endpoint, 'value) interval_map_cursor -> int

val interval_map_peek_previous :
  ('endpoint, 'value) interval_map_cursor ->
  ('endpoint, 'value) Persistent_interval_map.entry option

val interval_map_peek_next :
  ('endpoint, 'value) interval_map_cursor ->
  ('endpoint, 'value) Persistent_interval_map.entry option

val interval_map_move_previous :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option

val interval_map_move_next :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option

val interval_map_seek_rank :
  int -> ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option

val interval_map_seek_next_overlap :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) interval_map_cursor ->
  (('endpoint, 'value) interval_map_cursor search, string) result

val interval_map_try_insert :
  low:'endpoint ->
  high:'endpoint ->
  'value ->
  ('endpoint, 'value) interval_map_cursor ->
  (('endpoint, 'value) interval_map_cursor insertion, string) result

val interval_map_set_next_value :
  'value ->
  ('endpoint, 'value) interval_map_cursor ->
  (('endpoint, 'value) interval_map_cursor option, string) result

val interval_map_delete_previous :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option

val interval_map_delete_next :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option

val interval_map_snapshot :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) Persistent_interval_map.t

type chunked_bit_set_cursor

val chunked_bit_set_at : int -> Persistent_chunked_bit_set.t -> chunked_bit_set_cursor option
val chunked_bit_set_at_or_after : int -> Persistent_chunked_bit_set.t -> chunked_bit_set_cursor
val chunked_bit_set_find : int -> Persistent_chunked_bit_set.t -> chunked_bit_set_cursor search
val chunked_bit_set_position : chunked_bit_set_cursor -> int
val chunked_bit_set_peek_previous : chunked_bit_set_cursor -> int option
val chunked_bit_set_peek_next : chunked_bit_set_cursor -> int option
val chunked_bit_set_move_previous : chunked_bit_set_cursor -> chunked_bit_set_cursor option
val chunked_bit_set_move_next : chunked_bit_set_cursor -> chunked_bit_set_cursor option
val chunked_bit_set_seek_rank : int -> chunked_bit_set_cursor -> chunked_bit_set_cursor option
val chunked_bit_set_add : int -> chunked_bit_set_cursor -> (chunked_bit_set_cursor, string) result
val chunked_bit_set_delete_previous : chunked_bit_set_cursor -> chunked_bit_set_cursor option
val chunked_bit_set_delete_next : chunked_bit_set_cursor -> chunked_bit_set_cursor option
val chunked_bit_set_snapshot : chunked_bit_set_cursor -> Persistent_chunked_bit_set.t
