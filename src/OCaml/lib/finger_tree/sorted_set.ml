type 'element t = { order : 'element Common.Comparator.t; values : 'element array }

let compare set = Common.Comparator.compare set.order
let empty order = { order; values = [||] }
let comparator set = set.order
let count set = Array.length set.values
let is_empty set = count set = 0
let minimum set = if is_empty set then None else Some set.values.(0)
let maximum set = if is_empty set then None else Some set.values.(count set - 1)
let nth index set = if index < 0 || index >= count set then None else Some set.values.(index)

let index_of value set =
  let index = Sorted_helpers.lower_bound (compare set) value set.values in
  if index < count set && compare set set.values.(index) value = 0 then Some index else None

let find value set = Option.map (fun index -> set.values.(index)) (index_of value set)
let mem value set = Option.is_some (index_of value set)

let add value set =
  let index = Sorted_helpers.lower_bound (compare set) value set.values in
  if index < count set && compare set set.values.(index) value = 0 then (false, set)
  else (true, { set with values = Sorted_helpers.insert index value set.values })

let remove value set =
  match index_of value set with
  | None -> (false, set)
  | Some index -> (true, { set with values = Sorted_helpers.remove index set.values })

let of_list order values =
  List.fold_left (fun set value -> snd (add value set)) (empty order) values

let floor value set = nth (Sorted_helpers.upper_bound (compare set) value set.values - 1) set
let ceiling value set = nth (Sorted_helpers.lower_bound (compare set) value set.values) set
let lower value set = nth (Sorted_helpers.lower_bound (compare set) value set.values - 1) set
let higher value set = nth (Sorted_helpers.upper_bound (compare set) value set.values) set

let require_compatible left right =
  if not (Common.Comparator.same left.order right.order) then
    invalid_arg "sorted-set comparators differ"

let union left right =
  require_compatible left right;
  Array.fold_left (fun result value -> snd (add value result)) left right.values

let intersect left right =
  require_compatible left right;
  Array.fold_left
    (fun result value -> if mem value right then snd (add value result) else result)
    (empty left.order) left.values

let difference left right =
  require_compatible left right;
  Array.fold_left
    (fun result value -> if mem value right then result else snd (add value result))
    (empty left.order) left.values

let range ~start ~count:range_count set =
  if start < 0 || range_count < 0 || start > count set - range_count then
    Error "sorted-set range is out of bounds"
  else Ok { set with values = Sorted_helpers.slice start range_count set.values }

let value_range ~minimum:low ~maximum:high set =
  if compare set low high > 0 then empty set.order
  else
    let first = Sorted_helpers.lower_bound (compare set) low set.values in
    let last = Sorted_helpers.upper_bound (compare set) high set.values in
    { set with values = Sorted_helpers.slice first (last - first) set.values }

let to_list set = Array.to_list set.values

module Builder = struct
  type 'element set = 'element t
  type 'element t = { mutable current : 'element set }

  let create current = { current }

  let add value builder =
    let added, successor = add value builder.current in
    builder.current <- successor;
    added

  let remove value builder =
    let removed, successor = remove value builder.current in
    builder.current <- successor;
    removed

  let freeze builder = builder.current
end
