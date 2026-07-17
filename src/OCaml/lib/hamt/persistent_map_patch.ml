type 'value state = Absent | Present of 'value
type ('key, 'value) entry = { patch_key : 'key; before : 'value state; after : 'value state }

type ('key, 'value) conflict = {
  conflict_key : 'key;
  expected : 'value state;
  actual : 'value state;
}

type ('key, 'value) composition_conflict = {
  composition_key : 'key;
  left_after : 'value state;
  right_before : 'value state;
}

type 'value change = { change_before : 'value state; change_after : 'value state }

type ('key, 'value) t = {
  changes : ('key, 'value change) Persistent_hamt.map;
  value_equal : 'value -> 'value -> bool;
}

let states_equal value_equal left right =
  match (left, right) with
  | Absent, Absent -> true
  | Present left_value, Present right_value -> value_equal left_value right_value
  | Absent, Present _ | Present _, Absent -> false

let empty ?(value_equal = ( = )) policy =
  { changes = Persistent_hamt.empty ~value_equal:( == ) policy; value_equal }

let of_list ?(value_equal = ( = )) policy entries =
  let changes =
    List.fold_left
      (fun result entry ->
        let { patch_key; before; after } = entry in
        if states_equal value_equal before after then result
        else Persistent_hamt.add patch_key { change_before = before; change_after = after } result)
      (Persistent_hamt.empty ~value_equal:( == ) policy)
      entries
  in
  { changes; value_equal }

let state_of_entry = function
  | None -> Absent
  | Some entry -> Present (Persistent_hamt.entry_value entry)

let between ?(value_equal = ( = )) before after =
  if not (Common.Hash_policy.same (Persistent_hamt.policy before) (Persistent_hamt.policy after))
  then invalid_arg "maps must retain the same key policy";
  let entries =
    List.map
      (fun difference ->
        {
          patch_key = difference.Persistent_hamt.difference_key;
          before =
            (match difference.Persistent_hamt.left_value with
            | None -> Absent
            | Some value -> Present value);
          after =
            (match difference.Persistent_hamt.right_value with
            | None -> Absent
            | Some value -> Present value);
        })
      (Persistent_hamt.diff before after)
  in
  of_list ~value_equal (Persistent_hamt.policy before) entries

let count patch = Persistent_hamt.count patch.changes

let to_list patch =
  List.map
    (fun (patch_key, change) ->
      { patch_key; before = change.change_before; after = change.change_after })
    (Persistent_hamt.to_list patch.changes)

let apply patch source =
  if
    not
      (Common.Hash_policy.same
         (Persistent_hamt.policy patch.changes)
         (Persistent_hamt.policy source))
  then invalid_arg "map and patch must retain the same key policy";
  let rec preflight = function
    | [] -> Ok ()
    | (key, change) :: rest ->
        let actual = state_of_entry (Persistent_hamt.find_entry_opt key source) in
        if states_equal patch.value_equal change.change_before actual then preflight rest
        else Error { conflict_key = key; expected = change.change_before; actual }
  in
  match preflight (Persistent_hamt.to_list patch.changes) with
  | Error conflict -> Error conflict
  | Ok () ->
      Ok
        (Persistent_hamt.fold
           (fun result key change ->
             match change.change_after with
             | Absent -> Persistent_hamt.remove key result
             | Present value -> Persistent_hamt.set key value result)
           source patch.changes)

let invert patch =
  if count patch = 0 then patch
  else
    {
      patch with
      changes =
        Persistent_hamt.fold
          (fun result key change ->
            Persistent_hamt.add key
              { change_before = change.change_after; change_after = change.change_before }
              result)
          (Persistent_hamt.empty ~value_equal:( == ) (Persistent_hamt.policy patch.changes))
          patch.changes;
    }

let compose left right =
  if
    not
      (Common.Hash_policy.same
         (Persistent_hamt.policy left.changes)
         (Persistent_hamt.policy right.changes))
  then invalid_arg "patches must retain the same key policy";
  let rec merge changes = function
    | [] -> Ok changes
    | (key, right_change) :: rest -> (
        match Persistent_hamt.find_entry_opt key left.changes with
        | None -> merge (Persistent_hamt.add key right_change changes) rest
        | Some left_entry ->
            let left_change = Persistent_hamt.entry_value left_entry in
            if
              not
                (states_equal left.value_equal left_change.change_after right_change.change_before)
            then
              Error
                {
                  composition_key = key;
                  left_after = left_change.change_after;
                  right_before = right_change.change_before;
                }
            else
              let changes =
                if states_equal left.value_equal left_change.change_before right_change.change_after
                then changes
                else
                  Persistent_hamt.add key
                    {
                      change_before = left_change.change_before;
                      change_after = right_change.change_after;
                    }
                    changes
              in
              merge changes rest)
  in
  let initial =
    Persistent_hamt.fold
      (fun result key change ->
        if Persistent_hamt.mem key right.changes then result
        else Persistent_hamt.add key change result)
      (Persistent_hamt.empty ~value_equal:( == ) (Persistent_hamt.policy left.changes))
      left.changes
  in
  match merge initial (Persistent_hamt.to_list right.changes) with
  | Error conflict -> Error conflict
  | Ok changes -> Ok { changes; value_equal = left.value_equal }
