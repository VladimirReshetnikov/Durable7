exception Duplicate_key
exception Transient_consumed

type ('key, 'value) entry = { key : 'key; value : 'value }
type difference_kind = Only_left | Only_right | Different

type ('key, 'value) difference = {
  kind : difference_kind;
  difference_key : 'key;
  left_value : 'value option;
  right_value : 'value option;
}

type ('key, 'value) node =
  | Empty
  | Leaf of int * ('key, 'value) entry list
  | Branch of int32 * ('key, 'value) node array

type ('key, 'value) map = {
  policy : 'key Common.Hash_policy.t;
  value_equal : 'value -> 'value -> bool;
  root : ('key, 'value) node;
  count : int;
}

type ('key, 'value) change =
  | Inserted
  | Unchanged of ('key, 'value) entry
  | Replaced of ('key, 'value) entry

let empty ?(value_equal = ( = )) policy = { policy; value_equal; root = Empty; count = 0 }
let policy map = map.policy
let entry_key entry = entry.key
let entry_value entry = entry.value
let count map = map.count
let is_empty map = map.count = 0
let normalized_hash policy key = Common.Hash_policy.hash policy key land max_int
let slot hash shift = (hash lsr shift) land 31
let bit slot = Int32.shift_left 1l slot

let rec popcount value count =
  if Int32.equal value 0l then count
  else popcount (Int32.logand value (Int32.pred value)) (count + 1)

let index bitmap bit = popcount (Int32.logand bitmap (Int32.pred bit)) 0
let has_bit bitmap bit = not (Int32.equal (Int32.logand bitmap bit) 0l)

let insert_at index value array =
  Array.init
    (Array.length array + 1)
    (fun destination ->
      if destination < index then array.(destination)
      else if destination = index then value
      else array.(destination - 1))

let remove_at index array =
  Array.init
    (Array.length array - 1)
    (fun destination ->
      if destination < index then array.(destination) else array.(destination + 1))

let replace_at index value array =
  Array.mapi (fun position existing -> if position = index then value else existing) array

let rec merge_leaves shift left_hash left right_hash right =
  let left_slot = slot left_hash shift in
  let right_slot = slot right_hash shift in
  if left_slot = right_slot then
    let child = merge_leaves (shift + 5) left_hash left right_hash right in
    Branch (bit left_slot, [| child |])
  else
    let left_bit = bit left_slot in
    let right_bit = bit right_slot in
    let bitmap = Int32.logor left_bit right_bit in
    if left_slot < right_slot then Branch (bitmap, [| left; right |])
    else Branch (bitmap, [| right; left |])

let rec update_bucket policy value_equal key value replace prefix = function
  | [] -> (List.rev_append prefix [ { key; value } ], Inserted)
  | ({ key = stored_key; value = stored_value } as stored) :: rest ->
      if Common.Hash_policy.equal policy stored_key key then
        if (not replace) || value_equal stored_value value then
          (List.rev_append prefix (stored :: rest), Unchanged stored)
        else (List.rev_append prefix ({ key = stored_key; value } :: rest), Replaced stored)
      else update_bucket policy value_equal key value replace (stored :: prefix) rest

let rec set_node policy value_equal key value hash shift replace = function
  | Empty -> (Leaf (hash, [ { key; value } ]), Inserted)
  | Leaf (stored_hash, entries) as leaf ->
      if stored_hash = hash then
        let updated, change = update_bucket policy value_equal key value replace [] entries in
        if match change with Unchanged _ -> true | Inserted | Replaced _ -> false then (leaf, change)
        else (Leaf (hash, updated), change)
      else (merge_leaves shift stored_hash leaf hash (Leaf (hash, [ { key; value } ])), Inserted)
  | Branch (bitmap, children) as branch ->
      let slot = slot hash shift in
      let bit = bit slot in
      let child_index = index bitmap bit in
      if not (has_bit bitmap bit) then
        ( Branch
            ( Int32.logor bitmap bit,
              insert_at child_index (Leaf (hash, [ { key; value } ])) children ),
          Inserted )
      else
        let child, change =
          set_node policy value_equal key value hash (shift + 5) replace children.(child_index)
        in
        if match change with Unchanged _ -> true | Inserted | Replaced _ -> false then
          (branch, change)
        else (Branch (bitmap, replace_at child_index child children), change)

let rec find_node policy key hash shift = function
  | Empty -> None
  | Leaf (stored_hash, entries) ->
      if stored_hash <> hash then None
      else List.find_opt (fun entry -> Common.Hash_policy.equal policy entry.key key) entries
  | Branch (bitmap, children) ->
      let bit = bit (slot hash shift) in
      if not (has_bit bitmap bit) then None
      else find_node policy key hash (shift + 5) children.(index bitmap bit)

let find_entry_opt key map = find_node map.policy key (normalized_hash map.policy key) 0 map.root
let find_opt key map = Option.map (fun entry -> entry.value) (find_entry_opt key map)
let mem key map = Option.is_some (find_entry_opt key map)

let try_add key value map =
  let root, change =
    set_node map.policy map.value_equal key value (normalized_hash map.policy key) 0 false map.root
  in
  match change with
  | Inserted -> ({ map with root; count = map.count + 1 }, true)
  | Unchanged _ | Replaced _ -> (map, false)

let add key value map =
  let successor, added = try_add key value map in
  if added then successor else raise Duplicate_key

let set key value map =
  let root, change =
    set_node map.policy map.value_equal key value (normalized_hash map.policy key) 0 true map.root
  in
  match change with
  | Inserted -> { map with root; count = map.count + 1 }
  | Replaced _ -> { map with root }
  | Unchanged _ -> map

let singleton ?value_equal policy key value = add key value (empty ?value_equal policy)

let rec remove_bucket policy key prefix = function
  | [] -> (List.rev prefix, None)
  | entry :: rest ->
      if Common.Hash_policy.equal policy entry.key key then (List.rev_append prefix rest, Some entry)
      else remove_bucket policy key (entry :: prefix) rest

let rec remove_node policy key hash shift = function
  | Empty as empty -> (empty, None)
  | Leaf (stored_hash, entries) as leaf -> (
      if stored_hash <> hash then (leaf, None)
      else
        let remaining, removed = remove_bucket policy key [] entries in
        match removed with
        | None -> (leaf, None)
        | Some _ ->
            let successor = if remaining = [] then Empty else Leaf (stored_hash, remaining) in
            (successor, removed))
  | Branch (bitmap, children) as branch -> (
      let bit = bit (slot hash shift) in
      if not (has_bit bitmap bit) then (branch, None)
      else
        let child_index = index bitmap bit in
        let child, removed = remove_node policy key hash (shift + 5) children.(child_index) in
        match removed with
        | None -> (branch, None)
        | Some _ -> (
            match child with
            | Empty ->
                let remaining = remove_at child_index children in
                let successor =
                  if Array.length remaining = 0 then Empty
                  else if Array.length remaining = 1 then
                    match remaining.(0) with
                    | Leaf _ as leaf -> leaf
                    | Empty | Branch _ -> Branch (Int32.logand bitmap (Int32.lognot bit), remaining)
                  else Branch (Int32.logand bitmap (Int32.lognot bit), remaining)
                in
                (successor, removed)
            | Leaf _ | Branch _ -> (Branch (bitmap, replace_at child_index child children), removed)
            ))

let remove_entry key map =
  let root, removed = remove_node map.policy key (normalized_hash map.policy key) 0 map.root in
  match removed with
  | None -> (map, None)
  | Some _ -> ({ map with root; count = map.count - 1 }, removed)

let remove key map = fst (remove_entry key map)

let get_or_add key factory map =
  match find_entry_opt key map with
  | Some entry -> (map, entry.value, false)
  | None ->
      let value = factory () in
      (add key value map, value, true)

let add_or_update key ~add:add_factory ~update map =
  match find_entry_opt key map with
  | None ->
      let value = add_factory () in
      (add key value map, value, true)
  | Some entry ->
      let value = update entry.value in
      let successor = set entry.key value map in
      (successor, value, successor != map)

let rec fold_node folder accumulator = function
  | Empty -> accumulator
  | Leaf (_, entries) ->
      List.fold_left (fun state entry -> folder state entry.key entry.value) accumulator entries
  | Branch (_, children) -> Array.fold_left (fold_node folder) accumulator children

let fold folder accumulator map = fold_node folder accumulator map.root
let to_list map = List.rev (fold (fun result key value -> (key, value) :: result) [] map)

let equal left right =
  left == right
  || left.count = right.count
     && fold
          (fun result key value ->
            result
            &&
            match find_entry_opt key right with
            | Some entry -> left.value_equal value entry.value
            | None -> false)
          true left

let diff left right =
  let left_differences =
    fold
      (fun result key value ->
        match find_entry_opt key right with
        | None ->
            { kind = Only_left; difference_key = key; left_value = Some value; right_value = None }
            :: result
        | Some entry when not (left.value_equal value entry.value) ->
            {
              kind = Different;
              difference_key = key;
              left_value = Some value;
              right_value = Some entry.value;
            }
            :: result
        | Some _ -> result)
      [] left
  in
  fold
    (fun result key value ->
      if mem key left then result
      else
        { kind = Only_right; difference_key = key; left_value = None; right_value = Some value }
        :: result)
    left_differences right
  |> List.rev

module Bulk_builder = struct
  type ('key, 'value) t = { mutable current : ('key, 'value) map }

  let create current = { current }
  let count builder = count builder.current
  let set key value builder = builder.current <- set key value builder.current
  let add key value builder = builder.current <- add key value builder.current
  let freeze builder = builder.current
end

module Transient = struct
  type ('key, 'value) t = { mutable current : ('key, 'value) map option }

  let create current = { current = Some current }

  let active transient =
    match transient.current with Some map -> map | None -> raise Transient_consumed

  let count transient = count (active transient)
  let find_opt key transient = find_opt key (active transient)
  let fold folder seed transient = fold folder seed (active transient)
  let set key value transient = transient.current <- Some (set key value (active transient))
  let remove key transient = transient.current <- Some (remove key (active transient))

  let persistent transient =
    let result = active transient in
    transient.current <- None;
    result
end
