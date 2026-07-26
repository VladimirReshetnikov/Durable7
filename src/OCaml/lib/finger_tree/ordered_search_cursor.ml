(** Implementation of the immutable root-plus-rank gap cursors for ordered persistent
    collections. *)

type 'cursor search = { found : bool; search_cursor : 'cursor }
type 'cursor insertion = { added : bool; insertion_cursor : 'cursor }

let search_found result = result.found
let search_value result = result.search_cursor
let insertion_added result = result.added
let insertion_value result = result.insertion_cursor
let valid_position position count = position >= 0 && position <= count

type 'element sorted_bag_cursor = { bag : 'element Sorted_bag.t; bag_position : int }

let sorted_bag_at bag_position bag =
  if valid_position bag_position (Sorted_bag.count bag) then Some { bag; bag_position } else None

let sorted_bag_lower_bound element bag =
  { bag; bag_position = Sorted_bag.count_less_than element bag }

let sorted_bag_upper_bound element bag =
  { bag; bag_position = Sorted_bag.count_at_most element bag }

let sorted_bag_find element bag =
  { found = Sorted_bag.mem element bag; search_cursor = sorted_bag_lower_bound element bag }

let sorted_bag_position cursor = cursor.bag_position
let sorted_bag_peek_previous cursor = Sorted_bag.nth (cursor.bag_position - 1) cursor.bag
let sorted_bag_peek_next cursor = Sorted_bag.nth cursor.bag_position cursor.bag
let sorted_bag_move_previous cursor = sorted_bag_at (cursor.bag_position - 1) cursor.bag

let sorted_bag_move_next cursor =
  if cursor.bag_position = Sorted_bag.count cursor.bag then None
  else sorted_bag_at (cursor.bag_position + 1) cursor.bag

let sorted_bag_seek_rank bag_position cursor = sorted_bag_at bag_position cursor.bag

let sorted_bag_add element cursor =
  let bag_position = Sorted_bag.count_at_most element cursor.bag + 1 in
  { bag = Sorted_bag.add element cursor.bag; bag_position }

let sorted_bag_delete_previous cursor =
  Result.to_option
    (Result.map
       (fun bag -> { bag; bag_position = cursor.bag_position - 1 })
       (Sorted_bag.remove_at (cursor.bag_position - 1) cursor.bag))

let sorted_bag_delete_next cursor =
  Result.to_option
    (Result.map
       (fun bag -> { bag; bag_position = cursor.bag_position })
       (Sorted_bag.remove_at cursor.bag_position cursor.bag))

let sorted_bag_snapshot cursor = cursor.bag

type 'element sorted_set_cursor = { set : 'element Sorted_set.t; set_position : int }

let sorted_set_at set_position set =
  if valid_position set_position (Sorted_set.count set) then Some { set; set_position } else None

let sorted_set_lower_bound element set =
  { set; set_position = Sorted_set.count_less_than element set }

let sorted_set_upper_bound element set =
  { set; set_position = Sorted_set.count_at_most element set }

let sorted_set_find element set =
  { found = Sorted_set.mem element set; search_cursor = sorted_set_lower_bound element set }

let sorted_set_position cursor = cursor.set_position
let sorted_set_peek_previous cursor = Sorted_set.nth (cursor.set_position - 1) cursor.set
let sorted_set_peek_next cursor = Sorted_set.nth cursor.set_position cursor.set
let sorted_set_move_previous cursor = sorted_set_at (cursor.set_position - 1) cursor.set

let sorted_set_move_next cursor =
  if cursor.set_position = Sorted_set.count cursor.set then None
  else sorted_set_at (cursor.set_position + 1) cursor.set

let sorted_set_seek_rank set_position cursor = sorted_set_at set_position cursor.set

let sorted_set_add element cursor =
  let set_position = Sorted_set.count_less_than element cursor.set + 1 in
  let _, set = Sorted_set.add element cursor.set in
  { set; set_position }

let sorted_set_delete_previous cursor =
  Option.map
    (fun element ->
      let _, set = Sorted_set.remove element cursor.set in
      { set; set_position = cursor.set_position - 1 })
    (sorted_set_peek_previous cursor)

let sorted_set_delete_next cursor =
  Option.map
    (fun element ->
      let _, set = Sorted_set.remove element cursor.set in
      { set; set_position = cursor.set_position })
    (sorted_set_peek_next cursor)

let sorted_set_snapshot cursor = cursor.set

type ('key, 'value) sorted_map_cursor = { map : ('key, 'value) Sorted_map.t; map_position : int }

let sorted_map_at map_position map =
  if valid_position map_position (Sorted_map.count map) then Some { map; map_position } else None

let sorted_map_lower_bound key map = { map; map_position = Sorted_map.lower_bound key map }
let sorted_map_upper_bound key map = { map; map_position = Sorted_map.upper_bound key map }

let sorted_map_find key map =
  { found = Sorted_map.mem key map; search_cursor = sorted_map_lower_bound key map }

let sorted_map_position cursor = cursor.map_position
let map_entry_pair entry = (Sorted_map.entry_key entry, Sorted_map.entry_value entry)

let sorted_map_peek_previous cursor =
  Option.map map_entry_pair (Sorted_map.nth (cursor.map_position - 1) cursor.map)

let sorted_map_peek_next cursor =
  Option.map map_entry_pair (Sorted_map.nth cursor.map_position cursor.map)

let sorted_map_move_previous cursor = sorted_map_at (cursor.map_position - 1) cursor.map

let sorted_map_move_next cursor =
  if cursor.map_position = Sorted_map.count cursor.map then None
  else sorted_map_at (cursor.map_position + 1) cursor.map

let sorted_map_seek_rank map_position cursor = sorted_map_at map_position cursor.map

let sorted_map_insert key value cursor =
  let map_position = Sorted_map.lower_bound key cursor.map + 1 in
  Result.map (fun map -> { map; map_position }) (Sorted_map.add key value cursor.map)

let sorted_map_try_insert key value cursor =
  match sorted_map_insert key value cursor with
  | Ok insertion_cursor -> { added = true; insertion_cursor }
  | Error _ -> { added = false; insertion_cursor = sorted_map_lower_bound key cursor.map }

let sorted_map_set key value cursor =
  let lower = Sorted_map.lower_bound key cursor.map in
  let existed = Sorted_map.mem key cursor.map in
  let _, map = Sorted_map.set key value cursor.map in
  { map; map_position = (lower + if existed then 0 else 1) }

let sorted_map_set_next_value value cursor =
  Option.map
    (fun (key, _) ->
      let _, map = Sorted_map.set key value cursor.map in
      { map; map_position = cursor.map_position })
    (sorted_map_peek_next cursor)

let sorted_map_delete_previous cursor =
  Option.bind (sorted_map_peek_previous cursor) (fun (key, _) ->
      Option.map
        (fun (_, map) -> { map; map_position = cursor.map_position - 1 })
        (Sorted_map.remove key cursor.map))

let sorted_map_delete_next cursor =
  Option.bind (sorted_map_peek_next cursor) (fun (key, _) ->
      Option.map
        (fun (_, map) -> { map; map_position = cursor.map_position })
        (Sorted_map.remove key cursor.map))

let sorted_map_snapshot cursor = cursor.map

type 'element canonical_cursor = {
  canonical : 'element Canonical_sorted_set.t;
  canonical_position_value : int;
}

let canonical_at canonical_position_value canonical =
  if valid_position canonical_position_value (Canonical_sorted_set.count canonical) then
    Some { canonical; canonical_position_value }
  else None

let canonical_lower_bound element canonical =
  { canonical; canonical_position_value = Canonical_sorted_set.lower_bound element canonical }

let canonical_upper_bound element canonical =
  { canonical; canonical_position_value = Canonical_sorted_set.upper_bound element canonical }

let canonical_find element canonical =
  {
    found = Canonical_sorted_set.mem element canonical;
    search_cursor = canonical_lower_bound element canonical;
  }

let canonical_position cursor = cursor.canonical_position_value

let canonical_peek_previous cursor =
  Canonical_sorted_set.nth (cursor.canonical_position_value - 1) cursor.canonical

let canonical_peek_next cursor =
  Canonical_sorted_set.nth cursor.canonical_position_value cursor.canonical

let canonical_move_previous cursor =
  canonical_at (cursor.canonical_position_value - 1) cursor.canonical

let canonical_move_next cursor =
  if cursor.canonical_position_value = Canonical_sorted_set.count cursor.canonical then None
  else canonical_at (cursor.canonical_position_value + 1) cursor.canonical

let canonical_seek_rank canonical_position_value cursor =
  canonical_at canonical_position_value cursor.canonical

let canonical_add element cursor =
  let canonical_position_value = Canonical_sorted_set.lower_bound element cursor.canonical + 1 in
  match Canonical_sorted_set.add element cursor.canonical with
  | Canonical_sorted_set.Added canonical -> { canonical; canonical_position_value }
  | Canonical_sorted_set.Existing _ -> { cursor with canonical_position_value }

let canonical_delete_previous cursor =
  Option.map
    (fun element ->
      let _, canonical = Canonical_sorted_set.remove element cursor.canonical in
      { canonical; canonical_position_value = cursor.canonical_position_value - 1 })
    (canonical_peek_previous cursor)

let canonical_delete_next cursor =
  Option.map
    (fun element ->
      let _, canonical = Canonical_sorted_set.remove element cursor.canonical in
      { canonical; canonical_position_value = cursor.canonical_position_value })
    (canonical_peek_next cursor)

let canonical_snapshot cursor = cursor.canonical

type ('key, 'priority, 'value) priority_search_cursor = {
  queue : ('key, 'priority, 'value) Priority_search_queue.t;
  priority_position : int;
}

let priority_search_at priority_position queue =
  if valid_position priority_position (Priority_search_queue.count queue) then
    Some { queue; priority_position }
  else None

let priority_search_lower_bound key queue =
  { queue; priority_position = Priority_search_queue.lower_bound key queue }

let priority_search_upper_bound key queue =
  { queue; priority_position = Priority_search_queue.upper_bound key queue }

let priority_search_find key queue =
  {
    found = Priority_search_queue.mem key queue;
    search_cursor = priority_search_lower_bound key queue;
  }

let priority_search_minimum queue =
  match Priority_search_queue.minimum queue with
  | None -> { queue; priority_position = 0 }
  | Some entry -> priority_search_lower_bound (Priority_search_queue.entry_key entry) queue

let priority_search_position cursor = cursor.priority_position

let priority_search_peek_previous cursor =
  Priority_search_queue.nth (cursor.priority_position - 1) cursor.queue

let priority_search_peek_next cursor =
  Priority_search_queue.nth cursor.priority_position cursor.queue

let priority_search_move_previous cursor =
  priority_search_at (cursor.priority_position - 1) cursor.queue

let priority_search_move_next cursor =
  if cursor.priority_position = Priority_search_queue.count cursor.queue then None
  else priority_search_at (cursor.priority_position + 1) cursor.queue

let priority_search_seek_rank priority_position cursor =
  priority_search_at priority_position cursor.queue

let priority_search_try_insert key priority value cursor =
  let lower = Priority_search_queue.lower_bound key cursor.queue in
  match Priority_search_queue.add key priority value cursor.queue with
  | Ok queue -> { added = true; insertion_cursor = { queue; priority_position = lower + 1 } }
  | Error _ -> { added = false; insertion_cursor = { cursor with priority_position = lower } }

let priority_search_set key priority value cursor =
  let lower = Priority_search_queue.lower_bound key cursor.queue in
  let existed = Priority_search_queue.mem key cursor.queue in
  let _, queue = Priority_search_queue.set key priority value cursor.queue in
  { queue; priority_position = (lower + if existed then 0 else 1) }

let priority_search_set_next priority value cursor =
  Option.map
    (fun entry ->
      let key = Priority_search_queue.entry_key entry in
      let _, queue = Priority_search_queue.set key priority value cursor.queue in
      { queue; priority_position = cursor.priority_position })
    (priority_search_peek_next cursor)

let priority_search_delete_previous cursor =
  Option.bind (priority_search_peek_previous cursor) (fun entry ->
      Option.map
        (fun (_, queue) -> { queue; priority_position = cursor.priority_position - 1 })
        (Priority_search_queue.remove (Priority_search_queue.entry_key entry) cursor.queue))

let priority_search_delete_next cursor =
  Option.bind (priority_search_peek_next cursor) (fun entry ->
      Option.map
        (fun (_, queue) -> { queue; priority_position = cursor.priority_position })
        (Priority_search_queue.remove (Priority_search_queue.entry_key entry) cursor.queue))

let priority_search_snapshot cursor = cursor.queue

type 'endpoint interval_tree_cursor = { tree : 'endpoint Interval_tree.t; tree_position : int }

let interval_tree_at tree_position tree =
  if valid_position tree_position (Interval_tree.count tree) then Some { tree; tree_position }
  else None

let interval_tree_lower_bound low tree =
  { tree; tree_position = Interval_tree.lower_bound low tree }

let interval_tree_upper_bound low tree =
  { tree; tree_position = Interval_tree.upper_bound low tree }

let interval_equal tree left right =
  let compare = Common.Comparator.compare (Interval_tree.comparator tree) in
  compare (Interval_tree.low left) (Interval_tree.low right) = 0
  && compare (Interval_tree.high left) (Interval_tree.high right) = 0

let rec find_interval_rank predicate start tree =
  if start >= Interval_tree.count tree then None
  else
    match Interval_tree.nth start tree with
    | Some interval when predicate interval -> Some start
    | _ -> find_interval_rank predicate (start + 1) tree

(* Exact search only walks the contiguous equal-low run: intervals are ordered by low endpoint,
   so once the low endpoint stops matching no later interval can be an exact match. *)
let rec find_equal_low_rank tree low predicate start =
  if start >= Interval_tree.count tree then None
  else
    match Interval_tree.nth start tree with
    | None -> None
    | Some candidate ->
        if Common.Comparator.compare (Interval_tree.comparator tree) (Interval_tree.low candidate) low <> 0
        then None
        else if predicate candidate then Some start
        else find_equal_low_rank tree low predicate (start + 1)

let interval_tree_find interval tree =
  let lower = Interval_tree.lower_bound (Interval_tree.low interval) tree in
  match
    find_equal_low_rank tree (Interval_tree.low interval) (interval_equal tree interval) lower
  with
  | Some tree_position -> { found = true; search_cursor = { tree; tree_position } }
  | None -> { found = false; search_cursor = { tree; tree_position = lower } }

let overlap tree query candidate =
  Interval_tree.overlaps (Interval_tree.comparator tree) query candidate

let interval_tree_find_overlap_from start query tree =
  match find_interval_rank (overlap tree query) start tree with
  | Some tree_position -> { found = true; search_cursor = { tree; tree_position } }
  | None -> { found = false; search_cursor = { tree; tree_position = Interval_tree.count tree } }

let interval_tree_find_overlap query tree = interval_tree_find_overlap_from 0 query tree

let interval_tree_find_containing point tree =
  let query =
    Result.get_ok (Interval_tree.make_interval (Interval_tree.comparator tree) point point)
  in
  interval_tree_find_overlap query tree

let interval_tree_position cursor = cursor.tree_position
let interval_tree_peek_previous cursor = Interval_tree.nth (cursor.tree_position - 1) cursor.tree
let interval_tree_peek_next cursor = Interval_tree.nth cursor.tree_position cursor.tree
let interval_tree_move_previous cursor = interval_tree_at (cursor.tree_position - 1) cursor.tree

let interval_tree_move_next cursor =
  if cursor.tree_position = Interval_tree.count cursor.tree then None
  else interval_tree_at (cursor.tree_position + 1) cursor.tree

let interval_tree_seek_rank tree_position cursor = interval_tree_at tree_position cursor.tree

let interval_tree_seek_next_overlap query cursor =
  interval_tree_find_overlap_from
    (Int.min (Interval_tree.count cursor.tree) (cursor.tree_position + 1))
    query cursor.tree

let interval_tree_insert interval cursor =
  let tree_position = Interval_tree.lower_bound (Interval_tree.low interval) cursor.tree + 1 in
  { tree = Interval_tree.insert interval cursor.tree; tree_position }

let interval_tree_delete_previous cursor =
  Result.to_option
    (Result.map
       (fun tree -> { tree; tree_position = cursor.tree_position - 1 })
       (Interval_tree.remove_at (cursor.tree_position - 1) cursor.tree))

let interval_tree_delete_next cursor =
  Result.to_option
    (Result.map
       (fun tree -> { tree; tree_position = cursor.tree_position })
       (Interval_tree.remove_at cursor.tree_position cursor.tree))

let interval_tree_snapshot cursor = cursor.tree

type ('endpoint, 'value) interval_map_cursor = {
  interval_map : ('endpoint, 'value) Persistent_interval_map.t;
  interval_map_position_value : int;
}

let interval_map_at interval_map_position_value interval_map =
  if valid_position interval_map_position_value (Persistent_interval_map.count interval_map) then
    Some { interval_map; interval_map_position_value }
  else None

let interval_map_lower_bound ~low ~high interval_map =
  {
    interval_map;
    interval_map_position_value = Persistent_interval_map.lower_bound ~low ~high interval_map;
  }

let interval_map_upper_bound ~low ~high interval_map =
  {
    interval_map;
    interval_map_position_value = Persistent_interval_map.upper_bound ~low ~high interval_map;
  }

let interval_map_find ~low ~high interval_map =
  {
    found = Option.is_some (Persistent_interval_map.find_exact ~low ~high interval_map);
    search_cursor = interval_map_lower_bound ~low ~high interval_map;
  }

let interval_map_position cursor = cursor.interval_map_position_value

let interval_map_peek_previous cursor =
  Persistent_interval_map.nth (cursor.interval_map_position_value - 1) cursor.interval_map

let interval_map_peek_next cursor =
  Persistent_interval_map.nth cursor.interval_map_position_value cursor.interval_map

let interval_map_move_previous cursor =
  interval_map_at (cursor.interval_map_position_value - 1) cursor.interval_map

let interval_map_move_next cursor =
  if cursor.interval_map_position_value = Persistent_interval_map.count cursor.interval_map then
    None
  else interval_map_at (cursor.interval_map_position_value + 1) cursor.interval_map

let interval_map_seek_rank interval_map_position_value cursor =
  interval_map_at interval_map_position_value cursor.interval_map

let interval_map_entry_overlaps ~low ~high map entry =
  let compare = Common.Comparator.compare (Persistent_interval_map.comparator map) in
  compare (Persistent_interval_map.entry_low entry) high <= 0
  && compare low (Persistent_interval_map.entry_high entry) <= 0

let rec find_interval_map_rank predicate start map =
  if start >= Persistent_interval_map.count map then None
  else
    match Persistent_interval_map.nth start map with
    | Some entry when predicate entry -> Some start
    | _ -> find_interval_map_rank predicate (start + 1) map

let interval_map_find_overlap_from start ~low ~high map =
  Result.map
    (fun () ->
      match find_interval_map_rank (interval_map_entry_overlaps ~low ~high map) start map with
      | Some interval_map_position_value ->
          { found = true; search_cursor = { interval_map = map; interval_map_position_value } }
      | None ->
          {
            found = false;
            search_cursor =
              {
                interval_map = map;
                interval_map_position_value = Persistent_interval_map.count map;
              };
          })
    (Persistent_interval_map.validate_interval ~low ~high map)

let interval_map_find_overlap ~low ~high map = interval_map_find_overlap_from 0 ~low ~high map

let interval_map_find_containing point map =
  Result.get_ok (interval_map_find_overlap ~low:point ~high:point map)

let interval_map_seek_next_overlap ~low ~high cursor =
  interval_map_find_overlap_from
    (Int.min
       (Persistent_interval_map.count cursor.interval_map)
       (cursor.interval_map_position_value + 1))
    ~low ~high cursor.interval_map

let interval_map_try_insert ~low ~high value cursor =
  let interval_map_position_value =
    Persistent_interval_map.lower_bound ~low ~high cursor.interval_map
  in
  match Persistent_interval_map.add ~low ~high value cursor.interval_map with
  | Ok interval_map ->
      Ok
        {
          added = true;
          insertion_cursor =
            { interval_map; interval_map_position_value = interval_map_position_value + 1 };
        }
  | Error message when String.equal message "interval map already contains the exact interval" ->
      Ok { added = false; insertion_cursor = { cursor with interval_map_position_value } }
  | Error message -> Error message

let interval_map_set_next_value value cursor =
  match interval_map_peek_next cursor with
  | None -> Ok None
  | Some entry ->
      Result.map
        (fun (_, interval_map) ->
          Some { interval_map; interval_map_position_value = cursor.interval_map_position_value })
        (Persistent_interval_map.set
           ~low:(Persistent_interval_map.entry_low entry)
           ~high:(Persistent_interval_map.entry_high entry)
           value cursor.interval_map)

let interval_map_delete_previous cursor =
  Option.bind (interval_map_peek_previous cursor) (fun entry ->
      Option.map
        (fun (_, interval_map) ->
          { interval_map; interval_map_position_value = cursor.interval_map_position_value - 1 })
        (Persistent_interval_map.remove
           ~low:(Persistent_interval_map.entry_low entry)
           ~high:(Persistent_interval_map.entry_high entry)
           cursor.interval_map))

let interval_map_delete_next cursor =
  Option.bind (interval_map_peek_next cursor) (fun entry ->
      Option.map
        (fun (_, interval_map) ->
          { interval_map; interval_map_position_value = cursor.interval_map_position_value })
        (Persistent_interval_map.remove
           ~low:(Persistent_interval_map.entry_low entry)
           ~high:(Persistent_interval_map.entry_high entry)
           cursor.interval_map))

let interval_map_snapshot cursor = cursor.interval_map

type chunked_bit_set_cursor = { bit_set : Persistent_chunked_bit_set.t; bit_position : int }

let chunked_bit_set_at bit_position bit_set =
  if valid_position bit_position (Persistent_chunked_bit_set.count bit_set) then
    Some { bit_set; bit_position }
  else None

let chunked_bit_set_at_or_after bit_index bit_set =
  { bit_set; bit_position = Persistent_chunked_bit_set.rank bit_index bit_set }

let chunked_bit_set_find bit_index bit_set =
  {
    found = Persistent_chunked_bit_set.mem bit_index bit_set;
    search_cursor = chunked_bit_set_at_or_after bit_index bit_set;
  }

let chunked_bit_set_position cursor = cursor.bit_position

let chunked_bit_set_peek_previous cursor =
  Persistent_chunked_bit_set.select (cursor.bit_position - 1) cursor.bit_set

let chunked_bit_set_peek_next cursor =
  Persistent_chunked_bit_set.select cursor.bit_position cursor.bit_set

let chunked_bit_set_move_previous cursor =
  chunked_bit_set_at (cursor.bit_position - 1) cursor.bit_set

let chunked_bit_set_move_next cursor =
  if cursor.bit_position = Persistent_chunked_bit_set.count cursor.bit_set then None
  else chunked_bit_set_at (cursor.bit_position + 1) cursor.bit_set

let chunked_bit_set_seek_rank bit_position cursor = chunked_bit_set_at bit_position cursor.bit_set

let chunked_bit_set_add bit_index cursor =
  if Persistent_chunked_bit_set.mem bit_index cursor.bit_set then Ok cursor
  else
    Result.map
      (fun (_, bit_set) ->
        { bit_set; bit_position = Persistent_chunked_bit_set.rank bit_index cursor.bit_set + 1 })
      (Persistent_chunked_bit_set.add bit_index cursor.bit_set)

let chunked_bit_set_delete_previous cursor =
  Option.map
    (fun bit_index ->
      let _, bit_set = Persistent_chunked_bit_set.remove bit_index cursor.bit_set in
      { bit_set; bit_position = cursor.bit_position - 1 })
    (chunked_bit_set_peek_previous cursor)

let chunked_bit_set_delete_next cursor =
  Option.map
    (fun bit_index ->
      let _, bit_set = Persistent_chunked_bit_set.remove bit_index cursor.bit_set in
      { bit_set; bit_position = cursor.bit_position })
    (chunked_bit_set_peek_next cursor)

let chunked_bit_set_snapshot cursor = cursor.bit_set
