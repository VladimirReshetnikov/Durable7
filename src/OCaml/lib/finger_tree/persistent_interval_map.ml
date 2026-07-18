type ('endpoint, 'value) entry = { low : 'endpoint; high : 'endpoint; value : 'value }

type ('endpoint, 'value) t = {
  order : 'endpoint Common.Comparator.t;
  entries : ('endpoint, 'value) entry array;
}

let empty order = { order; entries = [||] }
let comparator map = map.order
let count map = Array.length map.entries
let entry_low entry = entry.low
let entry_high entry = entry.high
let entry_value entry = entry.value
let compare map = Common.Comparator.compare map.order

let compare_interval map left_low left_high right_low right_high =
  let by_low = compare map left_low right_low in
  if by_low <> 0 then by_low else compare map left_high right_high

let lower_bound_index low high map =
  let left = ref 0 in
  let right = ref (count map) in
  while !left < !right do
    let middle = !left + ((!right - !left) / 2) in
    let entry = map.entries.(middle) in
    if compare_interval map entry.low entry.high low high < 0 then left := middle + 1
    else right := middle
  done;
  !left

let upper_bound ~low ~high map =
  let index = lower_bound_index low high map in
  if
    index < count map
    && compare_interval map map.entries.(index).low map.entries.(index).high low high = 0
  then index + 1
  else index

let lower_bound ~low ~high map = lower_bound_index low high map
let nth index map = if index < 0 || index >= count map then None else Some map.entries.(index)

let validate_interval ~low ~high map =
  if compare map low high > 0 then Error "interval low endpoint exceeds its high endpoint"
  else Ok ()

let find_index ~low ~high map =
  let index = lower_bound_index low high map in
  if
    index < count map
    && compare_interval map map.entries.(index).low map.entries.(index).high low high = 0
  then Some index
  else None

let find_exact ~low ~high map =
  Option.map (fun index -> map.entries.(index)) (find_index ~low ~high map)

let add ~low ~high value map =
  Result.bind (validate_interval ~low ~high map) (fun () ->
      let index = lower_bound_index low high map in
      if
        index < count map
        && compare_interval map map.entries.(index).low map.entries.(index).high low high = 0
      then Error "interval map already contains the exact interval"
      else Ok { map with entries = Sorted_helpers.insert index { low; high; value } map.entries })

let set ~low ~high value map =
  Result.bind (validate_interval ~low ~high map) (fun () ->
      match find_index ~low ~high map with
      | None -> Result.map (fun successor -> (true, successor)) (add ~low ~high value map)
      | Some index ->
          let entries = Array.copy map.entries in
          entries.(index) <- { (entries.(index)) with value };
          Ok (false, { map with entries }))

let remove ~low ~high map =
  match find_index ~low ~high map with
  | None -> None
  | Some index ->
      Some
        (map.entries.(index).value, { map with entries = Sorted_helpers.remove index map.entries })

let query_point point map =
  Array.fold_right
    (fun entry result ->
      if compare map entry.low point <= 0 && compare map point entry.high <= 0 then entry :: result
      else result)
    map.entries []

let query_overlap ~low ~high map =
  Result.map
    (fun () ->
      Array.fold_right
        (fun entry result ->
          if compare map entry.low high <= 0 && compare map low entry.high <= 0 then entry :: result
          else result)
        map.entries [])
    (validate_interval ~low ~high map)

let to_list map = Array.to_list map.entries
