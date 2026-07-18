type 'cursor search = { found : bool; search_cursor : 'cursor }
type 'cursor insertion = { added : bool; insertion_cursor : 'cursor }

let search_found result = result.found
let search_value result = result.search_cursor
let insertion_added result = result.added
let insertion_value result = result.insertion_cursor
let valid_position position count = position >= 0 && position <= count

let find_index predicate values =
  let rec search index = function
    | [] -> None
    | value :: rest -> if predicate value then Some index else search (index + 1) rest
  in
  search 0 values

type 'element ordered_set_cursor = {
  ordered_set : 'element Persistent_ordered_set.t;
  ordered_set_position_value : int;
}

let ordered_set_at ordered_set_position_value ordered_set =
  if valid_position ordered_set_position_value (Persistent_ordered_set.count ordered_set) then
    Some { ordered_set; ordered_set_position_value }
  else None

let ordered_set_find element ordered_set =
  match Persistent_ordered_set.index_of element ordered_set with
  | Some ordered_set_position_value ->
      { found = true; search_cursor = { ordered_set; ordered_set_position_value } }
  | None ->
      {
        found = false;
        search_cursor =
          { ordered_set; ordered_set_position_value = Persistent_ordered_set.count ordered_set };
      }

let ordered_set_position cursor = cursor.ordered_set_position_value
let ordered_set_count cursor = Persistent_ordered_set.count cursor.ordered_set
let ordered_set_at_start cursor = cursor.ordered_set_position_value = 0
let ordered_set_at_end cursor = cursor.ordered_set_position_value = ordered_set_count cursor

let ordered_set_peek_previous cursor =
  Persistent_ordered_set.nth (cursor.ordered_set_position_value - 1) cursor.ordered_set

let ordered_set_peek_next cursor =
  Persistent_ordered_set.nth cursor.ordered_set_position_value cursor.ordered_set

let ordered_set_move_previous cursor =
  ordered_set_at (cursor.ordered_set_position_value - 1) cursor.ordered_set

let ordered_set_move_next cursor =
  if ordered_set_at_end cursor then None
  else ordered_set_at (cursor.ordered_set_position_value + 1) cursor.ordered_set

let ordered_set_seek ordered_set_position_value cursor =
  ordered_set_at ordered_set_position_value cursor.ordered_set

let ordered_set_insert element cursor =
  let added, ordered_set =
    Result.get_ok
      (Persistent_ordered_set.insert cursor.ordered_set_position_value element cursor.ordered_set)
  in
  if not added then cursor
  else { ordered_set; ordered_set_position_value = cursor.ordered_set_position_value + 1 }

let ordered_set_try_insert element cursor =
  let insertion_cursor = ordered_set_insert element cursor in
  { added = insertion_cursor != cursor; insertion_cursor }

let ordered_set_delete_previous cursor =
  Result.to_option
    (Result.map
       (fun (_, ordered_set) ->
         { ordered_set; ordered_set_position_value = cursor.ordered_set_position_value - 1 })
       (Persistent_ordered_set.remove_at (cursor.ordered_set_position_value - 1) cursor.ordered_set))

let ordered_set_delete_next cursor =
  Result.to_option
    (Result.map
       (fun (_, ordered_set) ->
         { ordered_set; ordered_set_position_value = cursor.ordered_set_position_value })
       (Persistent_ordered_set.remove_at cursor.ordered_set_position_value cursor.ordered_set))

let ordered_set_snapshot cursor = cursor.ordered_set

type ('key, 'value) ordered_map_cursor = {
  ordered_map : ('key, 'value) Persistent_ordered_map.t;
  ordered_map_position_value : int;
}

let ordered_map_at ordered_map_position_value ordered_map =
  if valid_position ordered_map_position_value (Persistent_ordered_map.count ordered_map) then
    Some { ordered_map; ordered_map_position_value }
  else None

let ordered_map_find key ordered_map =
  match Persistent_ordered_map.index_of key ordered_map with
  | Some ordered_map_position_value ->
      { found = true; search_cursor = { ordered_map; ordered_map_position_value } }
  | None ->
      {
        found = false;
        search_cursor =
          { ordered_map; ordered_map_position_value = Persistent_ordered_map.count ordered_map };
      }

let ordered_map_position cursor = cursor.ordered_map_position_value
let ordered_map_count cursor = Persistent_ordered_map.count cursor.ordered_map
let ordered_map_at_start cursor = cursor.ordered_map_position_value = 0
let ordered_map_at_end cursor = cursor.ordered_map_position_value = ordered_map_count cursor

let ordered_map_entry_pair entry =
  (Persistent_ordered_map.entry_key entry, Persistent_ordered_map.entry_value entry)

let ordered_map_peek_previous cursor =
  Option.map ordered_map_entry_pair
    (Persistent_ordered_map.nth (cursor.ordered_map_position_value - 1) cursor.ordered_map)

let ordered_map_peek_next cursor =
  Option.map ordered_map_entry_pair
    (Persistent_ordered_map.nth cursor.ordered_map_position_value cursor.ordered_map)

let ordered_map_move_previous cursor =
  ordered_map_at (cursor.ordered_map_position_value - 1) cursor.ordered_map

let ordered_map_move_next cursor =
  if ordered_map_at_end cursor then None
  else ordered_map_at (cursor.ordered_map_position_value + 1) cursor.ordered_map

let ordered_map_seek ordered_map_position_value cursor =
  ordered_map_at ordered_map_position_value cursor.ordered_map

let ordered_map_insert key value cursor =
  Result.map
    (fun ordered_map ->
      { ordered_map; ordered_map_position_value = cursor.ordered_map_position_value + 1 })
    (Persistent_ordered_map.insert cursor.ordered_map_position_value key value cursor.ordered_map)

let ordered_map_try_insert key value cursor =
  match Persistent_ordered_map.index_of key cursor.ordered_map with
  | Some ordered_map_position_value ->
      {
        added = false;
        insertion_cursor = { ordered_map = cursor.ordered_map; ordered_map_position_value };
      }
  | None -> { added = true; insertion_cursor = Result.get_ok (ordered_map_insert key value cursor) }

let ordered_map_set_next_value value cursor =
  Option.map
    (fun (key, _) ->
      let _, ordered_map = Persistent_ordered_map.set key value cursor.ordered_map in
      { ordered_map; ordered_map_position_value = cursor.ordered_map_position_value })
    (ordered_map_peek_next cursor)

let ordered_map_delete_previous cursor =
  Result.to_option
    (Result.map
       (fun (_, ordered_map) ->
         { ordered_map; ordered_map_position_value = cursor.ordered_map_position_value - 1 })
       (Persistent_ordered_map.remove_at (cursor.ordered_map_position_value - 1) cursor.ordered_map))

let ordered_map_delete_next cursor =
  Result.to_option
    (Result.map
       (fun (_, ordered_map) ->
         { ordered_map; ordered_map_position_value = cursor.ordered_map_position_value })
       (Persistent_ordered_map.remove_at cursor.ordered_map_position_value cursor.ordered_map))

let ordered_map_snapshot cursor = cursor.ordered_map

type ('key, 'value) ordered_multimap_cursor = {
  ordered_multimap : ('key, 'value) Persistent_ordered_multimap.t;
  ordered_multimap_position_value : int;
}

let ordered_multimap_at ordered_multimap_position_value ordered_multimap =
  if
    valid_position ordered_multimap_position_value
      (Persistent_ordered_multimap.pair_count ordered_multimap)
  then Some { ordered_multimap; ordered_multimap_position_value }
  else None

let ordered_multimap_position cursor = cursor.ordered_multimap_position_value

let ordered_multimap_pair_count cursor =
  Persistent_ordered_multimap.pair_count cursor.ordered_multimap

let ordered_multimap_at_start cursor = cursor.ordered_multimap_position_value = 0

let ordered_multimap_at_end cursor =
  cursor.ordered_multimap_position_value = ordered_multimap_pair_count cursor

let ordered_multimap_entry_at index ordered_multimap =
  if index < 0 then None
  else List.nth_opt (Persistent_ordered_multimap.entries ordered_multimap) index

let ordered_multimap_peek_previous cursor =
  ordered_multimap_entry_at (cursor.ordered_multimap_position_value - 1) cursor.ordered_multimap

let ordered_multimap_peek_next cursor =
  ordered_multimap_entry_at cursor.ordered_multimap_position_value cursor.ordered_multimap

let ordered_multimap_move_previous cursor =
  ordered_multimap_at (cursor.ordered_multimap_position_value - 1) cursor.ordered_multimap

let ordered_multimap_move_next cursor =
  if ordered_multimap_at_end cursor then None
  else ordered_multimap_at (cursor.ordered_multimap_position_value + 1) cursor.ordered_multimap

let ordered_multimap_seek ordered_multimap_position_value cursor =
  ordered_multimap_at ordered_multimap_position_value cursor.ordered_multimap

let ordered_multimap_index_of key value ordered_multimap =
  let equal_key =
    Common.Hash_policy.equal (Persistent_ordered_multimap.key_policy ordered_multimap)
  in
  let equal_value =
    Common.Hash_policy.equal (Persistent_ordered_multimap.value_policy ordered_multimap)
  in
  find_index
    (fun entry ->
      equal_key (Persistent_ordered_multimap.entry_key entry) key
      && equal_value (Persistent_ordered_multimap.entry_value entry) value)
    (Persistent_ordered_multimap.entries ordered_multimap)

let ordered_multimap_find key value ordered_multimap =
  match ordered_multimap_index_of key value ordered_multimap with
  | Some ordered_multimap_position_value ->
      { found = true; search_cursor = { ordered_multimap; ordered_multimap_position_value } }
  | None ->
      {
        found = false;
        search_cursor =
          {
            ordered_multimap;
            ordered_multimap_position_value =
              Persistent_ordered_multimap.pair_count ordered_multimap;
          };
      }

let ordered_multimap_find_group key ordered_multimap =
  let equal_key =
    Common.Hash_policy.equal (Persistent_ordered_multimap.key_policy ordered_multimap)
  in
  match
    find_index
      (fun entry -> equal_key (Persistent_ordered_multimap.entry_key entry) key)
      (Persistent_ordered_multimap.entries ordered_multimap)
  with
  | Some ordered_multimap_position_value ->
      { found = true; search_cursor = { ordered_multimap; ordered_multimap_position_value } }
  | None ->
      {
        found = false;
        search_cursor =
          {
            ordered_multimap;
            ordered_multimap_position_value =
              Persistent_ordered_multimap.pair_count ordered_multimap;
          };
      }

let ordered_multimap_insert key value cursor =
  let added, ordered_multimap = Persistent_ordered_multimap.add key value cursor.ordered_multimap in
  if not added then cursor
  else
    let ordered_multimap_position_value =
      Option.get (ordered_multimap_index_of key value ordered_multimap) + 1
    in
    { ordered_multimap; ordered_multimap_position_value }

let ordered_multimap_try_insert key value cursor =
  let insertion_cursor = ordered_multimap_insert key value cursor in
  { added = insertion_cursor != cursor; insertion_cursor }

let ordered_multimap_delete_previous cursor =
  Option.map
    (fun entry ->
      let _, ordered_multimap =
        Persistent_ordered_multimap.remove
          (Persistent_ordered_multimap.entry_key entry)
          (Persistent_ordered_multimap.entry_value entry)
          cursor.ordered_multimap
      in
      {
        ordered_multimap;
        ordered_multimap_position_value = cursor.ordered_multimap_position_value - 1;
      })
    (ordered_multimap_peek_previous cursor)

let ordered_multimap_delete_next cursor =
  Option.map
    (fun entry ->
      let _, ordered_multimap =
        Persistent_ordered_multimap.remove
          (Persistent_ordered_multimap.entry_key entry)
          (Persistent_ordered_multimap.entry_value entry)
          cursor.ordered_multimap
      in
      { ordered_multimap; ordered_multimap_position_value = cursor.ordered_multimap_position_value })
    (ordered_multimap_peek_next cursor)

let ordered_multimap_snapshot cursor = cursor.ordered_multimap
