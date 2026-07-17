type ('key, 'value) entry = { key : 'key; value : 'value }
type ('value, 'index) indexed_value = { row_value : 'value; index_key : 'index }

type ('key, 'value, 'index) t = {
  primary : ('key, ('value, 'index) indexed_value) Persistent_hamt.map;
  index : ('index, 'key) Persistent_hash_multimap.t;
  index_selector : 'key -> 'value -> 'index;
  value_equal : 'value -> 'value -> bool;
}

let empty ?(value_equal = ( = )) ~key_policy ~index_policy ~index_selector () =
  {
    primary = Persistent_hamt.empty ~value_equal:( == ) key_policy;
    index = Persistent_hash_multimap.empty ~key_policy:index_policy ~value_policy:key_policy;
    index_selector;
    value_equal;
  }

let count map = Persistent_hamt.count map.primary
let index_count map = Persistent_hash_multimap.key_count map.index

let find_opt key map =
  Option.map (fun indexed -> indexed.row_value) (Persistent_hamt.find_opt key map.primary)

let find_index_opt key map =
  Option.map (fun indexed -> indexed.index_key) (Persistent_hamt.find_opt key map.primary)

let keys_for_index index map = Persistent_hash_multimap.values index map.index

let add key value map =
  if Persistent_hamt.mem key map.primary then raise Persistent_hamt.Duplicate_key;
  let selected = map.index_selector key value in
  let index = Persistent_hash_multimap.add selected key map.index in
  let stored_index = Option.get (Persistent_hash_multimap.stored_key_opt selected index) in
  {
    map with
    primary = Persistent_hamt.add key { row_value = value; index_key = stored_index } map.primary;
    index;
  }

let try_add key value map =
  if Persistent_hamt.mem key map.primary then (map, false) else (add key value map, true)

let set key value map =
  match Persistent_hamt.find_entry_opt key map.primary with
  | None -> add key value map
  | Some entry ->
      let stored_key = Persistent_hamt.entry_key entry in
      let current = Persistent_hamt.entry_value entry in
      if map.value_equal current.row_value value then map
      else
        let selected = map.index_selector stored_key value in
        let index =
          if
            Common.Hash_policy.equal
              (Persistent_hash_multimap.key_policy map.index)
              current.index_key selected
          then map.index
          else
            map.index
            |> Persistent_hash_multimap.remove current.index_key stored_key
            |> Persistent_hash_multimap.add selected stored_key
        in
        let stored_index = Option.get (Persistent_hash_multimap.stored_key_opt selected index) in
        {
          map with
          primary =
            Persistent_hamt.set stored_key
              { row_value = value; index_key = stored_index }
              map.primary;
          index;
        }

let remove key map =
  match Persistent_hamt.find_entry_opt key map.primary with
  | None -> map
  | Some entry ->
      let stored_key = Persistent_hamt.entry_key entry in
      let current = Persistent_hamt.entry_value entry in
      {
        map with
        primary = Persistent_hamt.remove stored_key map.primary;
        index = Persistent_hash_multimap.remove current.index_key stored_key map.index;
      }

let to_list map =
  List.map
    (fun (key, indexed) -> { key; value = indexed.row_value })
    (Persistent_hamt.to_list map.primary)
