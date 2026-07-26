(** Immutable root-plus-rank gap cursors for ordered persistent collections. *)

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

type 'element sorted_bag_cursor
(** A gap cursor over one sorted bag version. *)

val sorted_bag_at : int -> 'element Sorted_bag.t -> 'element sorted_bag_cursor option
(** A cursor at the given gap of the bag. *)

val sorted_bag_lower_bound : 'element -> 'element Sorted_bag.t -> 'element sorted_bag_cursor
(** The rank of the first key not less than the probe. *)

val sorted_bag_upper_bound : 'element -> 'element Sorted_bag.t -> 'element sorted_bag_cursor
(** The rank after any key equal to the probe. *)

val sorted_bag_find : 'element -> 'element Sorted_bag.t -> 'element sorted_bag_cursor search
(** The element matching the probe. Raises when absent. *)

val sorted_bag_position : 'element sorted_bag_cursor -> int
(** The cursor's gap position. *)

val sorted_bag_peek_previous : 'element sorted_bag_cursor -> 'element option
(** The element immediately before the gap, or [None] at the start. *)

val sorted_bag_peek_next : 'element sorted_bag_cursor -> 'element option
(** The element immediately after the gap, or [None] at the end. *)

val sorted_bag_move_previous : 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val sorted_bag_move_next : 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val sorted_bag_seek_rank : int -> 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
(** A cursor at the given rank within the same version. *)

val sorted_bag_add : 'element -> 'element sorted_bag_cursor -> 'element sorted_bag_cursor
(** A bag containing the given element. *)

val sorted_bag_delete_previous : 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val sorted_bag_delete_next : 'element sorted_bag_cursor -> 'element sorted_bag_cursor option
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val sorted_bag_snapshot : 'element sorted_bag_cursor -> 'element Sorted_bag.t
(** The bag version this cursor is positioned in. *)

type 'element sorted_set_cursor
(** A gap cursor over one sorted set version. *)

val sorted_set_at : int -> 'element Sorted_set.t -> 'element sorted_set_cursor option
(** A cursor at the given gap of the set. *)

val sorted_set_lower_bound : 'element -> 'element Sorted_set.t -> 'element sorted_set_cursor
(** The rank of the first key not less than the probe. *)

val sorted_set_upper_bound : 'element -> 'element Sorted_set.t -> 'element sorted_set_cursor
(** The rank after any key equal to the probe. *)

val sorted_set_find : 'element -> 'element Sorted_set.t -> 'element sorted_set_cursor search
(** The element matching the probe. Raises when absent. *)

val sorted_set_position : 'element sorted_set_cursor -> int
(** The cursor's gap position. *)

val sorted_set_peek_previous : 'element sorted_set_cursor -> 'element option
(** The element immediately before the gap, or [None] at the start. *)

val sorted_set_peek_next : 'element sorted_set_cursor -> 'element option
(** The element immediately after the gap, or [None] at the end. *)

val sorted_set_move_previous : 'element sorted_set_cursor -> 'element sorted_set_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val sorted_set_move_next : 'element sorted_set_cursor -> 'element sorted_set_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val sorted_set_seek_rank : int -> 'element sorted_set_cursor -> 'element sorted_set_cursor option
(** A cursor at the given rank within the same version. *)

val sorted_set_add : 'element -> 'element sorted_set_cursor -> 'element sorted_set_cursor
(** A set containing the given element. *)

val sorted_set_delete_previous : 'element sorted_set_cursor -> 'element sorted_set_cursor option
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val sorted_set_delete_next : 'element sorted_set_cursor -> 'element sorted_set_cursor option
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val sorted_set_snapshot : 'element sorted_set_cursor -> 'element Sorted_set.t
(** The set version this cursor is positioned in. *)

type ('key, 'value) sorted_map_cursor
(** A gap cursor over one sorted map version. *)

val sorted_map_at : int -> ('key, 'value) Sorted_map.t -> ('key, 'value) sorted_map_cursor option
(** A cursor at the given gap of the map. *)

val sorted_map_lower_bound : 'key -> ('key, 'value) Sorted_map.t -> ('key, 'value) sorted_map_cursor
(** The rank of the first key not less than the probe. *)

val sorted_map_upper_bound : 'key -> ('key, 'value) Sorted_map.t -> ('key, 'value) sorted_map_cursor
(** The rank after any key equal to the probe. *)

val sorted_map_find : 'key -> ('key, 'value) Sorted_map.t -> ('key, 'value) sorted_map_cursor search
(** The entry matching the probe. Raises when absent. *)

val sorted_map_position : ('key, 'value) sorted_map_cursor -> int
(** The cursor's gap position. *)

val sorted_map_peek_previous : ('key, 'value) sorted_map_cursor -> ('key * 'value) option
(** The entry immediately before the gap, or [None] at the start. *)

val sorted_map_peek_next : ('key, 'value) sorted_map_cursor -> ('key * 'value) option
(** The entry immediately after the gap, or [None] at the end. *)

val sorted_map_move_previous :
  ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val sorted_map_move_next :
  ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val sorted_map_seek_rank :
  int -> ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option
(** A cursor at the given rank within the same version. *)

val sorted_map_insert :
  'key ->
  'value ->
  ('key, 'value) sorted_map_cursor ->
  (('key, 'value) sorted_map_cursor, string) result
(** A map containing the given entry. *)

val sorted_map_try_insert :
  'key -> 'value -> ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor insertion
(** Inserts the entry unless an equivalent one is present. *)

val sorted_map_set :
  'key -> 'value -> ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor
(** A map with the key bound to the value, adding or replacing as needed. *)

val sorted_map_set_next_value :
  'value -> ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option
(** Replaces the value of the entry after the gap, producing a new version the returned cursor is
    positioned in. *)

val sorted_map_delete_previous :
  ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option
(** Removes the entry before the gap, producing a new version the returned cursor is positioned
    in. *)

val sorted_map_delete_next :
  ('key, 'value) sorted_map_cursor -> ('key, 'value) sorted_map_cursor option
(** Removes the entry after the gap, producing a new version the returned cursor is positioned
    in. *)

val sorted_map_snapshot : ('key, 'value) sorted_map_cursor -> ('key, 'value) Sorted_map.t
(** The map version this cursor is positioned in. *)

type 'element canonical_cursor
(** A gap cursor over one canonical sorted set version. *)

val canonical_at : int -> 'element Canonical_sorted_set.t -> 'element canonical_cursor option
(** A cursor at the given gap of the set. *)

val canonical_lower_bound : 'element -> 'element Canonical_sorted_set.t -> 'element canonical_cursor
(** The rank of the first key not less than the probe. *)

val canonical_upper_bound : 'element -> 'element Canonical_sorted_set.t -> 'element canonical_cursor
(** The rank after any key equal to the probe. *)

val canonical_find : 'element -> 'element Canonical_sorted_set.t -> 'element canonical_cursor search
(** The element matching the probe. Raises when absent. *)

val canonical_position : 'element canonical_cursor -> int
(** The cursor's gap position. *)

val canonical_peek_previous : 'element canonical_cursor -> 'element option
(** The element immediately before the gap, or [None] at the start. *)

val canonical_peek_next : 'element canonical_cursor -> 'element option
(** The element immediately after the gap, or [None] at the end. *)

val canonical_move_previous : 'element canonical_cursor -> 'element canonical_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val canonical_move_next : 'element canonical_cursor -> 'element canonical_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val canonical_seek_rank : int -> 'element canonical_cursor -> 'element canonical_cursor option
(** A cursor at the given rank within the same version. *)

val canonical_add : 'element -> 'element canonical_cursor -> 'element canonical_cursor
(** A set containing the given element. *)

val canonical_delete_previous : 'element canonical_cursor -> 'element canonical_cursor option
(** Removes the element before the gap, producing a new version the returned cursor is positioned
    in. *)

val canonical_delete_next : 'element canonical_cursor -> 'element canonical_cursor option
(** Removes the element after the gap, producing a new version the returned cursor is positioned
    in. *)

val canonical_snapshot : 'element canonical_cursor -> 'element Canonical_sorted_set.t
(** The set version this cursor is positioned in. *)

type ('key, 'priority, 'value) priority_search_cursor
(** A gap cursor over one priority search queue version, in key order. *)

val priority_search_at :
  int ->
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor option
(** A cursor at the given key-order gap of the queue. *)

val priority_search_lower_bound :
  'key ->
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor
(** The rank of the first key not less than the probe. *)

val priority_search_upper_bound :
  'key ->
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor
(** The rank after any key equal to the probe. *)

val priority_search_find :
  'key ->
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor search
(** The entry matching the probe. Raises when absent. *)

val priority_search_minimum :
  ('key, 'priority, 'value) Priority_search_queue.t ->
  ('key, 'priority, 'value) priority_search_cursor
(** The smallest element, or the minimum-priority entry where the structure is a queue. *)

val priority_search_position : ('key, 'priority, 'value) priority_search_cursor -> int
(** The cursor's gap position. *)

val priority_search_peek_previous :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) Priority_search_queue.entry option
(** The entry immediately before the gap, or [None] at the start. *)

val priority_search_peek_next :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) Priority_search_queue.entry option
(** The entry immediately after the gap, or [None] at the end. *)

val priority_search_move_previous :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val priority_search_move_next :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val priority_search_seek_rank :
  int ->
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option
(** A cursor at the given rank within the same version. *)

val priority_search_try_insert :
  'key ->
  'priority ->
  'value ->
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor insertion
(** Inserts the entry unless an equivalent one is present. *)

val priority_search_set :
  'key ->
  'priority ->
  'value ->
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor
(** A queue with the key bound to the value, adding or replacing as needed. *)

val priority_search_set_next :
  'priority ->
  'value ->
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option
(** Replaces the entry after the gap, producing a new version the returned cursor is positioned
    in. *)

val priority_search_delete_previous :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option
(** Removes the entry before the gap, producing a new version the returned cursor is positioned
    in. *)

val priority_search_delete_next :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) priority_search_cursor option
(** Removes the entry after the gap, producing a new version the returned cursor is positioned
    in. *)

val priority_search_snapshot :
  ('key, 'priority, 'value) priority_search_cursor ->
  ('key, 'priority, 'value) Priority_search_queue.t
(** The queue version this cursor is positioned in. *)

type 'endpoint interval_tree_cursor
(** A gap cursor over one interval tree version, in low-endpoint order. *)

val interval_tree_at : int -> 'endpoint Interval_tree.t -> 'endpoint interval_tree_cursor option
(** A cursor at the given gap of the tree. *)

val interval_tree_lower_bound :
  'endpoint -> 'endpoint Interval_tree.t -> 'endpoint interval_tree_cursor
(** The rank of the first key not less than the probe. *)

val interval_tree_upper_bound :
  'endpoint -> 'endpoint Interval_tree.t -> 'endpoint interval_tree_cursor
(** The rank after any key equal to the probe. *)

val interval_tree_find :
  'endpoint Interval_tree.interval ->
  'endpoint Interval_tree.t ->
  'endpoint interval_tree_cursor search
(** The interval matching the probe. Raises when absent. *)

val interval_tree_find_overlap :
  'endpoint Interval_tree.interval ->
  'endpoint Interval_tree.t ->
  'endpoint interval_tree_cursor search
(** A cursor at the first interval overlapping the probe. *)

val interval_tree_find_containing :
  'endpoint -> 'endpoint Interval_tree.t -> 'endpoint interval_tree_cursor search
(** A cursor at the first interval containing the point. *)

val interval_tree_position : 'endpoint interval_tree_cursor -> int
(** The cursor's gap position. *)

val interval_tree_peek_previous :
  'endpoint interval_tree_cursor -> 'endpoint Interval_tree.interval option
(** The interval immediately before the gap, or [None] at the start. *)

val interval_tree_peek_next :
  'endpoint interval_tree_cursor -> 'endpoint Interval_tree.interval option
(** The interval immediately after the gap, or [None] at the end. *)

val interval_tree_move_previous :
  'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val interval_tree_move_next :
  'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val interval_tree_seek_rank :
  int -> 'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option
(** A cursor at the given rank within the same version. *)

val interval_tree_seek_next_overlap :
  'endpoint Interval_tree.interval ->
  'endpoint interval_tree_cursor ->
  'endpoint interval_tree_cursor search
(** A cursor at the next interval overlapping the probe. Subtrees whose cached maximum endpoint
    falls short of the probe are skipped whole. *)

val interval_tree_insert :
  'endpoint Interval_tree.interval ->
  'endpoint interval_tree_cursor ->
  'endpoint interval_tree_cursor
(** A tree containing the given interval. *)

val interval_tree_delete_previous :
  'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option
(** Removes the interval before the gap, producing a new version the returned cursor is positioned
    in. *)

val interval_tree_delete_next :
  'endpoint interval_tree_cursor -> 'endpoint interval_tree_cursor option
(** Removes the interval after the gap, producing a new version the returned cursor is positioned
    in. *)

val interval_tree_snapshot : 'endpoint interval_tree_cursor -> 'endpoint Interval_tree.t
(** The tree version this cursor is positioned in. *)

type ('endpoint, 'value) interval_map_cursor
(** A gap cursor over one interval map version. *)

val interval_map_at :
  int ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor option
(** A cursor at the given gap of the map. *)

val interval_map_lower_bound :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor
(** The rank of the first key not less than the probe. *)

val interval_map_upper_bound :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor
(** The rank after any key equal to the probe. *)

val interval_map_find :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor search
(** The entry matching the probe. Raises when absent. *)

val interval_map_find_overlap :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  (('endpoint, 'value) interval_map_cursor search, string) result
(** A cursor at the first entry whose interval overlaps the probe. *)

val interval_map_find_containing :
  'endpoint ->
  ('endpoint, 'value) Persistent_interval_map.t ->
  ('endpoint, 'value) interval_map_cursor search
(** A cursor at the first entry whose interval contains the point. *)

val interval_map_position : ('endpoint, 'value) interval_map_cursor -> int
(** The cursor's gap position. *)

val interval_map_peek_previous :
  ('endpoint, 'value) interval_map_cursor ->
  ('endpoint, 'value) Persistent_interval_map.entry option
(** The entry immediately before the gap, or [None] at the start. *)

val interval_map_peek_next :
  ('endpoint, 'value) interval_map_cursor ->
  ('endpoint, 'value) Persistent_interval_map.entry option
(** The entry immediately after the gap, or [None] at the end. *)

val interval_map_move_previous :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val interval_map_move_next :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val interval_map_seek_rank :
  int -> ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option
(** A cursor at the given rank within the same version. *)

val interval_map_seek_next_overlap :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) interval_map_cursor ->
  (('endpoint, 'value) interval_map_cursor search, string) result
(** A cursor at the next interval overlapping the probe. Subtrees whose cached maximum endpoint
    falls short of the probe are skipped whole. *)

val interval_map_try_insert :
  low:'endpoint ->
  high:'endpoint ->
  'value ->
  ('endpoint, 'value) interval_map_cursor ->
  (('endpoint, 'value) interval_map_cursor insertion, string) result
(** Inserts the entry unless an equivalent one is present. *)

val interval_map_set_next_value :
  'value ->
  ('endpoint, 'value) interval_map_cursor ->
  (('endpoint, 'value) interval_map_cursor option, string) result
(** Replaces the value of the entry after the gap, producing a new version the returned cursor is
    positioned in. *)

val interval_map_delete_previous :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option
(** Removes the entry before the gap, producing a new version the returned cursor is positioned
    in. *)

val interval_map_delete_next :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) interval_map_cursor option
(** Removes the entry after the gap, producing a new version the returned cursor is positioned
    in. *)

val interval_map_snapshot :
  ('endpoint, 'value) interval_map_cursor -> ('endpoint, 'value) Persistent_interval_map.t
(** The map version this cursor is positioned in. *)

type chunked_bit_set_cursor
(** A gap cursor over one bit set version, in set-bit order. *)

val chunked_bit_set_at : int -> Persistent_chunked_bit_set.t -> chunked_bit_set_cursor option
(** A cursor at the given gap in set-bit order. *)

val chunked_bit_set_at_or_after : int -> Persistent_chunked_bit_set.t -> chunked_bit_set_cursor
(** A cursor before the first set bit at or after the index, located by rank rather than by
    scanning. *)

val chunked_bit_set_find : int -> Persistent_chunked_bit_set.t -> chunked_bit_set_cursor search
(** The set bit matching the probe. Raises when absent. *)

val chunked_bit_set_position : chunked_bit_set_cursor -> int
(** The cursor's gap position. *)

val chunked_bit_set_peek_previous : chunked_bit_set_cursor -> int option
(** The set bit immediately before the gap, or [None] at the start. *)

val chunked_bit_set_peek_next : chunked_bit_set_cursor -> int option
(** The set bit immediately after the gap, or [None] at the end. *)

val chunked_bit_set_move_previous : chunked_bit_set_cursor -> chunked_bit_set_cursor option
(** A cursor one position earlier. The receiver is unchanged. *)

val chunked_bit_set_move_next : chunked_bit_set_cursor -> chunked_bit_set_cursor option
(** A cursor one position later. The receiver is unchanged. *)

val chunked_bit_set_seek_rank : int -> chunked_bit_set_cursor -> chunked_bit_set_cursor option
(** A cursor at the given rank within the same version. *)

val chunked_bit_set_add : int -> chunked_bit_set_cursor -> (chunked_bit_set_cursor, string) result
(** A bit set containing the given set bit. *)

val chunked_bit_set_delete_previous : chunked_bit_set_cursor -> chunked_bit_set_cursor option
(** Removes the set bit before the gap, producing a new version the returned cursor is positioned
    in. *)

val chunked_bit_set_delete_next : chunked_bit_set_cursor -> chunked_bit_set_cursor option
(** Removes the set bit after the gap, producing a new version the returned cursor is positioned
    in. *)

val chunked_bit_set_snapshot : chunked_bit_set_cursor -> Persistent_chunked_bit_set.t
(** The bit set version this cursor is positioned in. *)
