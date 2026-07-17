type ('key, 'value) entry = { key : 'key; value : 'value }

type ('key, 'value) t = {
  equality : 'key Common.Hash_policy.t;
  value_equal : 'value -> 'value -> bool;
  entries : ('key, 'value) entry array;
}

let empty ?(value_equal = ( = )) equality = { equality; value_equal; entries = [||] }
let key_policy map = map.equality
let count map = Array.length map.entries
let is_empty map = count map = 0
let entry_key entry = entry.key
let entry_value entry = entry.value
let equal map = Common.Hash_policy.equal map.equality

let index_of key map =
  let rec search index =
    if index = count map then None
    else if equal map map.entries.(index).key key then Some index
    else search (index + 1)
  in
  search 0

let mem key map = Option.is_some (index_of key map)
let find_entry key map = Option.map (fun index -> map.entries.(index)) (index_of key map)
let find_opt key map = Option.map entry_value (find_entry key map)
let nth index map = if index < 0 || index >= count map then None else Some map.entries.(index)
let first map = nth 0 map
let last map = nth (count map - 1) map

let insert index key value map =
  if index < 0 || index > count map then Error "ordered-map insertion index is out of bounds"
  else if mem key map then Error "ordered map already contains an equivalent key"
  else Ok { map with entries = Finger_tree.Sorted_helpers.insert index { key; value } map.entries }

let add key value map = insert (count map) key value map

let set key value map =
  match index_of key map with
  | None -> (true, Result.get_ok (add key value map))
  | Some index when map.value_equal map.entries.(index).value value -> (false, map)
  | Some index ->
      let entries = Array.copy map.entries in
      entries.(index) <- { key = entries.(index).key; value };
      (false, { map with entries })

let of_list ?(value_equal = ( = )) equality entries =
  List.fold_left
    (fun map (key, value) -> snd (set key value map))
    (empty ~value_equal equality) entries

let remove_at index map =
  if index < 0 || index >= count map then Error "ordered-map removal index is out of bounds"
  else
    Ok
      ( map.entries.(index),
        { map with entries = Finger_tree.Sorted_helpers.remove index map.entries } )

let remove key map =
  Option.map (fun index -> Result.get_ok (remove_at index map)) (index_of key map)

let move_to index key map =
  if index < 0 || index >= count map then Error "ordered-map movement index is out of bounds"
  else
    match index_of key map with
    | None -> Ok map
    | Some current when current = index -> Ok map
    | Some current ->
        let entry = map.entries.(current) in
        let entries = Finger_tree.Sorted_helpers.remove current map.entries in
        Ok { map with entries = Finger_tree.Sorted_helpers.insert index entry entries }

let move_to_first key map = if is_empty map then map else Result.get_ok (move_to 0 key map)

let move_to_last key map =
  if is_empty map then map else Result.get_ok (move_to (count map - 1) key map)

let range ~start ~count:range_count map =
  if start < 0 || range_count < 0 || start > count map - range_count then
    Error "ordered-map range is out of bounds"
  else Ok { map with entries = Finger_tree.Sorted_helpers.slice start range_count map.entries }

let reverse map = { map with entries = Array.of_list (List.rev (Array.to_list map.entries)) }

let sort_by_key comparator map =
  let entries = Array.copy map.entries in
  Array.stable_sort
    (fun left right -> Common.Comparator.compare comparator left.key right.key)
    entries;
  { map with entries }

let to_list map = Array.to_list (Array.map (fun entry -> (entry.key, entry.value)) map.entries)
