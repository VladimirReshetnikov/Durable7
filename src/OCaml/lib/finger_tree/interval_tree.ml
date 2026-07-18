type 'endpoint interval = { interval_low : 'endpoint; interval_high : 'endpoint }

type 'endpoint t = {
  order : 'endpoint Common.Comparator.t;
  intervals : 'endpoint interval array;
  cached_maximum_high : 'endpoint option;
}

let make_interval order interval_low interval_high =
  if Common.Comparator.compare order interval_low interval_high > 0 then
    Error "interval low endpoint exceeds its high endpoint"
  else Ok { interval_low; interval_high }

let low interval = interval.interval_low
let high interval = interval.interval_high

let overlaps order left right =
  Common.Comparator.compare order left.interval_low right.interval_high <= 0
  && Common.Comparator.compare order right.interval_low left.interval_high <= 0

let contains_point order point interval =
  Common.Comparator.compare order interval.interval_low point <= 0
  && Common.Comparator.compare order point interval.interval_high <= 0

let empty order = { order; intervals = [||]; cached_maximum_high = None }
let comparator tree = tree.order
let count tree = Array.length tree.intervals
let is_empty tree = count tree = 0
let maximum_high tree = tree.cached_maximum_high
let nth index tree = if index < 0 || index >= count tree then None else Some tree.intervals.(index)

let lower_bound low tree =
  let low_index = ref 0 in
  let high_index = ref (count tree) in
  while !low_index < !high_index do
    let middle = !low_index + ((!high_index - !low_index) / 2) in
    if Common.Comparator.compare tree.order tree.intervals.(middle).interval_low low < 0 then
      low_index := middle + 1
    else high_index := middle
  done;
  !low_index

let find_maximum order intervals =
  Array.fold_left
    (fun current interval ->
      match current with
      | None -> Some interval.interval_high
      | Some value ->
          if Common.Comparator.compare order interval.interval_high value > 0 then
            Some interval.interval_high
          else current)
    None intervals

let upper_bound_low low tree =
  let low_index = ref 0 in
  let high_index = ref (count tree) in
  while !low_index < !high_index do
    let middle = !low_index + ((!high_index - !low_index) / 2) in
    if Common.Comparator.compare tree.order tree.intervals.(middle).interval_low low <= 0 then
      low_index := middle + 1
    else high_index := middle
  done;
  !low_index

let upper_bound = upper_bound_low

let insert interval tree =
  let intervals =
    Sorted_helpers.insert (upper_bound_low interval.interval_low tree) interval tree.intervals
  in
  let cached_maximum_high =
    match tree.cached_maximum_high with
    | None -> Some interval.interval_high
    | Some current ->
        if Common.Comparator.compare tree.order interval.interval_high current > 0 then
          Some interval.interval_high
        else tree.cached_maximum_high
  in
  { tree with intervals; cached_maximum_high }

let of_list order intervals =
  List.fold_left (fun tree interval -> insert interval tree) (empty order) intervals

let equal_interval tree left right =
  Common.Comparator.compare tree.order left.interval_low right.interval_low = 0
  && Common.Comparator.compare tree.order left.interval_high right.interval_high = 0

let remove interval tree =
  let rec find index =
    if index = count tree then None
    else if equal_interval tree interval tree.intervals.(index) then Some index
    else find (index + 1)
  in
  match find 0 with
  | None -> (false, tree)
  | Some index ->
      let intervals = Sorted_helpers.remove index tree.intervals in
      (true, { tree with intervals; cached_maximum_high = find_maximum tree.order intervals })

let remove_at index tree =
  if index < 0 || index >= count tree then Error "interval-tree index is out of bounds"
  else
    let intervals = Sorted_helpers.remove index tree.intervals in
    Ok { tree with intervals; cached_maximum_high = find_maximum tree.order intervals }

let find_all_overlaps interval tree =
  Array.fold_right
    (fun candidate result ->
      if overlaps tree.order interval candidate then candidate :: result else result)
    tree.intervals []

let find_overlap interval tree =
  match find_all_overlaps interval tree with [] -> None | candidate :: _ -> Some candidate

let query_point point tree =
  Array.fold_right
    (fun interval result ->
      if contains_point tree.order point interval then interval :: result else result)
    tree.intervals []

let to_list tree = Array.to_list tree.intervals
