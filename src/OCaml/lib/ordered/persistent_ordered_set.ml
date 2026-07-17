type 'element t = { equality : 'element Common.Hash_policy.t; values : 'element array }

let empty equality = { equality; values = [||] }
let policy set = set.equality
let count set = Array.length set.values
let is_empty set = count set = 0
let equal set = Common.Hash_policy.equal set.equality

let index_of value set =
  let rec search index =
    if index = count set then None
    else if equal set set.values.(index) value then Some index
    else search (index + 1)
  in
  search 0

let mem value set = Option.is_some (index_of value set)
let find value set = Option.map (fun index -> set.values.(index)) (index_of value set)
let nth index set = if index < 0 || index >= count set then None else Some set.values.(index)
let first set = nth 0 set
let last set = nth (count set - 1) set

let insert index value set =
  if index < 0 || index > count set then Error "ordered-set insertion index is out of bounds"
  else
    match index_of value set with
    | Some _ -> Ok (false, set)
    | None ->
        Ok (true, { set with values = Finger_tree.Sorted_helpers.insert index value set.values })

let add value set = Result.get_ok (insert (count set) value set)
let add_first value set = Result.get_ok (insert 0 value set)

let of_list equality values =
  List.fold_left (fun set value -> snd (add value set)) (empty equality) values

let remove_at index set =
  if index < 0 || index >= count set then Error "ordered-set removal index is out of bounds"
  else
    Ok (set.values.(index), { set with values = Finger_tree.Sorted_helpers.remove index set.values })

let remove value set =
  match index_of value set with
  | None -> (false, set)
  | Some index -> (true, snd (Result.get_ok (remove_at index set)))

let move_to index value set =
  if index < 0 || index >= count set then Error "ordered-set movement index is out of bounds"
  else
    match index_of value set with
    | None -> Ok set
    | Some current when current = index -> Ok set
    | Some current ->
        let representative = set.values.(current) in
        let removed = Finger_tree.Sorted_helpers.remove current set.values in
        Ok { set with values = Finger_tree.Sorted_helpers.insert index representative removed }

let move_to_first value set = if is_empty set then set else Result.get_ok (move_to 0 value set)

let move_to_last value set =
  if is_empty set then set else Result.get_ok (move_to (count set - 1) value set)

let range ~start ~count:range_count set =
  if start < 0 || range_count < 0 || start > count set - range_count then
    Error "ordered-set range is out of bounds"
  else Ok { set with values = Finger_tree.Sorted_helpers.slice start range_count set.values }

let take range_count set = range ~start:0 ~count:range_count set
let drop range_count set = range ~start:range_count ~count:(count set - range_count) set
let reverse set = { set with values = Array.of_list (List.rev (Array.to_list set.values)) }

let sort comparator set =
  let values = Array.copy set.values in
  Array.stable_sort (Common.Comparator.compare comparator) values;
  { set with values }

let to_list set = Array.to_list set.values
let normalize set values = of_list set.equality values

let union set values =
  List.fold_left (fun result value -> snd (add value result)) set (to_list (normalize set values))

let intersect set values =
  let argument = normalize set values in
  of_list set.equality (List.filter (fun value -> mem value argument) (to_list set))

let difference set values =
  let argument = normalize set values in
  of_list set.equality (List.filter (fun value -> not (mem value argument)) (to_list set))

let symmetric_difference set values =
  let argument = normalize set values in
  let receiver_only = List.filter (fun value -> not (mem value argument)) (to_list set) in
  let argument_only = List.filter (fun value -> not (mem value set)) (to_list argument) in
  of_list set.equality (receiver_only @ argument_only)
