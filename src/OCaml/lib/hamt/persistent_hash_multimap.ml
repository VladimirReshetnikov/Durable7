(** Implementation of the set-valued persistent hash multimap. *)

type ('key, 'value) entry = { key : 'key; value : 'value }

type ('key, 'value) t = {
  groups : ('key, 'value Persistent_hash_set.t) Persistent_hamt.map;
  value_policy : 'value Common.Hash_policy.t;
  pair_count : int;
}

let empty ~key_policy ~value_policy =
  { groups = Persistent_hamt.empty ~value_equal:( == ) key_policy; value_policy; pair_count = 0 }

let key_policy multimap = Persistent_hamt.policy multimap.groups
let value_policy multimap = multimap.value_policy
let key_count multimap = Persistent_hamt.count multimap.groups
let pair_count multimap = multimap.pair_count
let is_empty multimap = multimap.pair_count = 0
let contains_key key multimap = Persistent_hamt.mem key multimap.groups

let values key multimap =
  Option.value
    ~default:(Persistent_hash_set.empty multimap.value_policy)
    (Persistent_hamt.find_opt key multimap.groups)

let contains key value multimap = Persistent_hash_set.mem value (values key multimap)

let stored_key_opt key multimap =
  Option.map Persistent_hamt.entry_key (Persistent_hamt.find_entry_opt key multimap.groups)

let add key value multimap =
  match Persistent_hamt.find_entry_opt key multimap.groups with
  | None ->
      let group = Persistent_hash_set.singleton multimap.value_policy value in
      {
        multimap with
        groups = Persistent_hamt.add key group multimap.groups;
        pair_count = multimap.pair_count + 1;
      }
  | Some entry ->
      let group = Persistent_hamt.entry_value entry in
      let updated = Persistent_hash_set.add value group in
      if updated == group then multimap
      else
        {
          multimap with
          groups = Persistent_hamt.set (Persistent_hamt.entry_key entry) updated multimap.groups;
          pair_count = multimap.pair_count + 1;
        }

let remove key value multimap =
  match Persistent_hamt.find_entry_opt key multimap.groups with
  | None -> multimap
  | Some entry ->
      let group = Persistent_hamt.entry_value entry in
      let updated = Persistent_hash_set.remove value group in
      if updated == group then multimap
      else
        let groups =
          if Persistent_hash_set.is_empty updated then
            Persistent_hamt.remove (Persistent_hamt.entry_key entry) multimap.groups
          else Persistent_hamt.set (Persistent_hamt.entry_key entry) updated multimap.groups
        in
        { multimap with groups; pair_count = multimap.pair_count - 1 }

let remove_key key multimap =
  let groups, removed = Persistent_hamt.remove_entry key multimap.groups in
  match removed with
  | None -> multimap
  | Some entry ->
      {
        multimap with
        groups;
        pair_count =
          multimap.pair_count - Persistent_hash_set.count (Persistent_hamt.entry_value entry);
      }

let to_list multimap =
  Persistent_hamt.fold
    (fun result key group ->
      List.fold_left
        (fun entries value -> { key; value } :: entries)
        result
        (Persistent_hash_set.to_list group))
    [] multimap.groups
  |> List.rev
