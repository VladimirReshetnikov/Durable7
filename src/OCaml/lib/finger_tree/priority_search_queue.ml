type ('key, 'priority, 'value) entry = { key : 'key; priority : 'priority; value : 'value }

type ('key, 'priority, 'value) t = {
  key_order : 'key Common.Comparator.t;
  priority_order : 'priority Common.Comparator.t;
  entries : ('key, 'priority, 'value) entry array;
  winner : int option;
  recomputation_count : int;
}

type statistics = { count : int; estimated_height : int; winner_recomputations : int }

let empty ~key_comparator ~priority_comparator =
  {
    key_order = key_comparator;
    priority_order = priority_comparator;
    entries = [||];
    winner = None;
    recomputation_count = 0;
  }

let count queue = Array.length queue.entries
let is_empty queue = count queue = 0
let entry_key entry = entry.key
let entry_priority entry = entry.priority
let entry_value entry = entry.value
let compare_key queue = Common.Comparator.compare queue.key_order

let lower_bound key queue =
  let low = ref 0 in
  let high = ref (count queue) in
  while !low < !high do
    let middle = !low + ((!high - !low) / 2) in
    if compare_key queue queue.entries.(middle).key key < 0 then low := middle + 1
    else high := middle
  done;
  !low

let upper_bound key queue =
  let low = ref 0 in
  let high = ref (count queue) in
  while !low < !high do
    let middle = !low + ((!high - !low) / 2) in
    if compare_key queue queue.entries.(middle).key key <= 0 then low := middle + 1
    else high := middle
  done;
  !low

let nth index queue = if index < 0 || index >= count queue then None else Some queue.entries.(index)

let find_index key queue =
  let index = lower_bound key queue in
  if index < count queue && compare_key queue queue.entries.(index).key key = 0 then Some index
  else None

let find key queue = Option.map (fun index -> queue.entries.(index)) (find_index key queue)
let mem key queue = Option.is_some (find_index key queue)

let before queue left right =
  let by_priority = Common.Comparator.compare queue.priority_order left.priority right.priority in
  by_priority < 0 || (by_priority = 0 && compare_key queue left.key right.key < 0)

let find_winner queue entries =
  if Array.length entries = 0 then None
  else
    let winner = ref 0 in
    for index = 1 to Array.length entries - 1 do
      if before queue entries.(index) entries.(!winner) then winner := index
    done;
    Some !winner

let publish queue entries =
  {
    queue with
    entries;
    winner = find_winner queue entries;
    recomputation_count = queue.recomputation_count + 1;
  }

let add key priority value queue =
  let index = lower_bound key queue in
  if index < count queue && compare_key queue queue.entries.(index).key key = 0 then
    Error "priority-search queue already contains an equivalent key"
  else Ok (publish queue (Sorted_helpers.insert index { key; priority; value } queue.entries))

let set key priority value queue =
  let index = lower_bound key queue in
  if index < count queue && compare_key queue queue.entries.(index).key key = 0 then (
    let entries = Array.copy queue.entries in
    entries.(index) <- { key = entries.(index).key; priority; value };
    (false, publish queue entries))
  else (true, publish queue (Sorted_helpers.insert index { key; priority; value } queue.entries))

let remove key queue =
  match find_index key queue with
  | None -> None
  | Some index ->
      Some (queue.entries.(index), publish queue (Sorted_helpers.remove index queue.entries))

let minimum queue = Option.map (fun index -> queue.entries.(index)) queue.winner

let minimum_view queue =
  match minimum queue with
  | None -> None
  | Some entry -> Option.map (fun (_, successor) -> (entry, successor)) (remove entry.key queue)

let entries_by_key queue = Array.to_list queue.entries

let statistics queue =
  let rec height nodes result =
    if nodes <= 1 then result else height ((nodes + 1) / 2) (result + 1)
  in
  {
    count = count queue;
    estimated_height = height (count queue) 0;
    winner_recomputations = queue.recomputation_count;
  }
