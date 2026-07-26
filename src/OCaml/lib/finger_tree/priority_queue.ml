(** Implementation of the stable persistent priority queue retaining insertion order among equal
    priorities. *)

type ('element, 'priority) entry = { value : 'element; priority : 'priority }

type ('element, 'priority) t = {
  order : 'priority Common.Comparator.t;
  entries : ('element, 'priority) entry array;
  best : int option;
}

let empty order = { order; entries = [||]; best = None }
let comparator queue = queue.order
let count queue = Array.length queue.entries
let is_empty queue = count queue = 0
let entry_value entry = entry.value
let entry_priority entry = entry.priority
let compare queue = Common.Comparator.compare queue.order

let find_best order entries =
  if Array.length entries = 0 then None
  else
    let best = ref 0 in
    for index = 1 to Array.length entries - 1 do
      if Common.Comparator.compare order entries.(index).priority entries.(!best).priority < 0 then
        best := index
    done;
    Some !best

let enqueue value priority queue =
  let index = count queue in
  let entries = Array.append queue.entries [| { value; priority } |] in
  let best =
    match queue.best with
    | None -> Some index
    | Some current ->
        if compare queue priority queue.entries.(current).priority < 0 then Some index
        else queue.best
  in
  { queue with entries; best }

let meld left right =
  if not (Common.Comparator.same left.order right.order) then
    invalid_arg "cannot meld priority queues with different comparators";
  let entries = Array.append left.entries right.entries in
  { order = left.order; entries; best = find_best left.order entries }

let peek queue = Option.map (fun index -> queue.entries.(index)) queue.best

let dequeue queue =
  match queue.best with
  | None -> None
  | Some index ->
      let entry = queue.entries.(index) in
      let entries = Sorted_helpers.remove index queue.entries in
      Some (entry, { queue with entries; best = find_best queue.order entries })

let to_list queue = Array.to_list queue.entries
