type ('key, 'value) entry = { key : 'key; value : 'value }

type ('key, 'value) t = {
  equality : 'key Common.Hash_policy.t;
  value_equal : 'value -> 'value -> bool;
  entries : ('key, 'value) entry array;
}

let empty ?(value_equal = ( = )) equality = { equality; value_equal; entries = [||] }
let policy association = association.equality
let count association = Array.length association.entries
let is_empty association = count association = 0
let entry_key entry = entry.key
let entry_value entry = entry.value
let equal association = Common.Hash_policy.equal association.equality

let index_of key association =
  let rec search index =
    if index = count association then None
    else if equal association association.entries.(index).key key then Some index
    else search (index + 1)
  in
  search 0

let mem key association = Option.is_some (index_of key association)

let find_entry key association =
  Option.map (fun index -> association.entries.(index)) (index_of key association)

let find_opt key association = Option.map entry_value (find_entry key association)

let nth index association =
  if index < 0 || index >= count association then None else Some association.entries.(index)

let first association = nth 0 association
let last association = nth (count association - 1) association

let set key value association =
  match index_of key association with
  | None ->
      {
        association with
        entries =
          Finger_tree.Sorted_helpers.insert (count association) { key; value } association.entries;
      }
  | Some index when association.value_equal association.entries.(index).value value -> association
  | Some index ->
      let entries = Array.copy association.entries in
      entries.(index) <- { key = entries.(index).key; value };
      { association with entries }

let set_items pairs association =
  List.fold_left (fun result (key, value) -> set key value result) association pairs

let of_list ?(value_equal = ( = )) equality pairs = set_items pairs (empty ~value_equal equality)

let to_list association =
  Array.to_list (Array.map (fun entry -> (entry.key, entry.value)) association.entries)

let join left right = set_items (to_list right) left

let insert position key value association =
  if position < 0 || position > count association then
    Error "Tungsten Association insertion position is out of bounds"
  else
    let entries, target =
      match index_of key association with
      | None -> (association.entries, position)
      | Some previous ->
          ( Finger_tree.Sorted_helpers.remove previous association.entries,
            if previous < position then position - 1 else position )
    in
    Ok
      { association with entries = Finger_tree.Sorted_helpers.insert target { key; value } entries }

let append key value association = Result.get_ok (insert (count association) key value association)
let prepend key value association = Result.get_ok (insert 0 key value association)

let remove key association =
  Option.map
    (fun index ->
      ( association.entries.(index).value,
        { association with entries = Finger_tree.Sorted_helpers.remove index association.entries }
      ))
    (index_of key association)

let remove_at index association =
  if index < 0 || index >= count association then
    Error "Tungsten Association removal position is out of bounds"
  else Ok { association with entries = Finger_tree.Sorted_helpers.remove index association.entries }

let key_take requested association =
  List.fold_left
    (fun result key ->
      match (find_entry key association, find_entry key result) with
      | Some entry, None -> append entry.key entry.value result
      | Some _, Some _ | None, _ -> result)
    (empty ~value_equal:association.value_equal association.equality)
    requested

let range ~start ~count:range_count association =
  if start < 0 || range_count < 0 || start > count association - range_count then
    Error "Tungsten Association range is out of bounds"
  else
    Ok
      {
        association with
        entries = Finger_tree.Sorted_helpers.slice start range_count association.entries;
      }

let reverse association =
  { association with entries = Array.of_list (List.rev (Array.to_list association.entries)) }

let sort_by project comparator association =
  let entries = Array.copy association.entries in
  Array.stable_sort
    (fun left right -> Common.Comparator.compare comparator (project left) (project right))
    entries;
  { association with entries }

let key_sort comparator = sort_by (fun entry -> entry.key) comparator
let value_sort comparator = sort_by (fun entry -> entry.value) comparator
let keys association = List.map fst (to_list association)
let values association = List.map snd (to_list association)
