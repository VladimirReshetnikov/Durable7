type ('key, 'value) entry = { key : 'key; value : 'value }

type ('key, 'value) t = {
  groups : ('key, 'value Persistent_ordered_set.t) Persistent_ordered_map.t;
  values_equality : 'value Common.Hash_policy.t;
  pairs : int;
}

let empty ~key_policy ~value_policy =
  {
    groups = Persistent_ordered_map.empty ~value_equal:( == ) key_policy;
    values_equality = value_policy;
    pairs = 0;
  }

let key_policy map = Persistent_ordered_map.key_policy map.groups
let value_policy map = map.values_equality
let key_count map = Persistent_ordered_map.count map.groups
let pair_count map = map.pairs
let is_empty map = map.pairs = 0
let entry_key entry = entry.key
let entry_value entry = entry.value

let checked_increment pairs =
  if pairs = max_int then invalid_arg "ordered multimap pair count overflow";
  pairs + 1

let mem_key key map = Persistent_ordered_map.mem key map.groups

let values key map =
  match Persistent_ordered_map.find_opt key map.groups with
  | None -> Persistent_ordered_set.empty map.values_equality
  | Some values -> values

let mem key value map = Persistent_ordered_set.mem value (values key map)

let find_key key map =
  Option.map Persistent_ordered_map.entry_key (Persistent_ordered_map.find_entry key map.groups)

let find_value key value map = Persistent_ordered_set.find value (values key map)

let add key value map =
  match Persistent_ordered_map.find_entry key map.groups with
  | None ->
      let added, group =
        Persistent_ordered_set.add value (Persistent_ordered_set.empty map.values_equality)
      in
      assert added;
      ( true,
        {
          map with
          groups = Result.get_ok (Persistent_ordered_map.add key group map.groups);
          pairs = checked_increment map.pairs;
        } )
  | Some entry ->
      let added, group =
        Persistent_ordered_set.add value (Persistent_ordered_map.entry_value entry)
      in
      if not added then (false, map)
      else
        let _, groups =
          Persistent_ordered_map.set (Persistent_ordered_map.entry_key entry) group map.groups
        in
        (true, { map with groups; pairs = checked_increment map.pairs })

let of_list ~key_policy ~value_policy pairs =
  List.fold_left
    (fun map (key, value) -> snd (add key value map))
    (empty ~key_policy ~value_policy) pairs

let remove key value map =
  match Persistent_ordered_map.find_entry key map.groups with
  | None -> (false, map)
  | Some entry ->
      let group = Persistent_ordered_map.entry_value entry in
      let removed, group = Persistent_ordered_set.remove value group in
      if not removed then (false, map)
      else if Persistent_ordered_set.is_empty group then (
        let removed_entry, groups = Option.get (Persistent_ordered_map.remove key map.groups) in
        ignore removed_entry;
        (true, { map with groups; pairs = map.pairs - 1 }))
      else
        let _, groups =
          Persistent_ordered_map.set (Persistent_ordered_map.entry_key entry) group map.groups
        in
        (true, { map with groups; pairs = map.pairs - 1 })

let remove_key key map =
  match Persistent_ordered_map.find_entry key map.groups with
  | None -> (0, map)
  | Some entry ->
      let removed_count = Persistent_ordered_set.count (Persistent_ordered_map.entry_value entry) in
      let _, groups = Option.get (Persistent_ordered_map.remove key map.groups) in
      (removed_count, { map with groups; pairs = map.pairs - removed_count })

let move_key_to index key map =
  Result.map
    (fun groups -> { map with groups })
    (Persistent_ordered_map.move_to index key map.groups)

let move_value_to index ~key ~value map =
  match Persistent_ordered_map.find_entry key map.groups with
  | None -> Ok map
  | Some entry ->
      let group = Persistent_ordered_map.entry_value entry in
      Result.map
        (fun group ->
          let _, groups =
            Persistent_ordered_map.set (Persistent_ordered_map.entry_key entry) group map.groups
          in
          { map with groups })
        (Persistent_ordered_set.move_to index value group)

let reverse_keys map = { map with groups = Persistent_ordered_map.reverse map.groups }

let reverse_values key map =
  match Persistent_ordered_map.find_entry key map.groups with
  | None -> map
  | Some entry ->
      let group = Persistent_ordered_set.reverse (Persistent_ordered_map.entry_value entry) in
      let _, groups =
        Persistent_ordered_map.set (Persistent_ordered_map.entry_key entry) group map.groups
      in
      { map with groups }

let entries map =
  List.concat_map
    (fun (key, group) ->
      List.map (fun value -> { key; value }) (Persistent_ordered_set.to_list group))
    (Persistent_ordered_map.to_list map.groups)
