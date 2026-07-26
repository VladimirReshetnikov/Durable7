(** Implementation of the persistent order-statistic sorted map with first-key representative
    retention. *)

type ('key, 'value) entry = { key : 'key; value : 'value }
type ('key, 'value) t = { order : 'key Common.Comparator.t; entries : ('key, 'value) entry array }

let empty order = { order; entries = [||] }
let comparator map = map.order
let count map = Array.length map.entries
let is_empty map = count map = 0
let entry_key entry = entry.key
let entry_value entry = entry.value
let compare map left right = Common.Comparator.compare map.order left right

let lower_bound key map =
  let low = ref 0 in
  let high = ref (count map) in
  while !low < !high do
    let middle = !low + ((!high - !low) / 2) in
    if compare map map.entries.(middle).key key < 0 then low := middle + 1 else high := middle
  done;
  !low

let upper_bound key map =
  let low = ref 0 in
  let high = ref (count map) in
  while !low < !high do
    let middle = !low + ((!high - !low) / 2) in
    if compare map map.entries.(middle).key key <= 0 then low := middle + 1 else high := middle
  done;
  !low

let nth index map = if index < 0 || index >= count map then None else Some map.entries.(index)

let find_index key map =
  let index = lower_bound key map in
  if index < count map && compare map map.entries.(index).key key = 0 then Some index else None

let find_entry key map = Option.map (fun index -> map.entries.(index)) (find_index key map)
let find_opt key map = Option.map entry_value (find_entry key map)
let mem key map = Option.is_some (find_index key map)

let add key value map =
  let index = lower_bound key map in
  if index < count map && compare map map.entries.(index).key key = 0 then
    Error "sorted map already contains an equivalent key"
  else Ok { map with entries = Sorted_helpers.insert index { key; value } map.entries }

let set key value map =
  let index = lower_bound key map in
  if index < count map && compare map map.entries.(index).key key = 0 then (
    let entries = Array.copy map.entries in
    entries.(index) <- { key = entries.(index).key; value };
    (false, { map with entries }))
  else (true, { map with entries = Sorted_helpers.insert index { key; value } map.entries })

let remove key map =
  match find_index key map with
  | None -> None
  | Some index ->
      Some
        (map.entries.(index).value, { map with entries = Sorted_helpers.remove index map.entries })

let minimum map = nth 0 map
let maximum map = nth (count map - 1) map
let floor key map = nth (upper_bound key map - 1) map
let ceiling key map = nth (lower_bound key map) map
let lower key map = nth (lower_bound key map - 1) map
let higher key map = nth (upper_bound key map) map

let range ~start ~count:range_count map =
  if start < 0 || range_count < 0 || start > count map - range_count then
    Error "sorted-map range is out of bounds"
  else Ok { map with entries = Sorted_helpers.slice start range_count map.entries }

let key_range ~minimum:low ~maximum:high map =
  if compare map low high > 0 then empty map.order
  else
    let first = lower_bound low map in
    let last = upper_bound high map in
    { map with entries = Sorted_helpers.slice first (last - first) map.entries }

let to_list map = Array.to_list (Array.map (fun entry -> (entry.key, entry.value)) map.entries)

let of_list order entries =
  List.fold_left
    (fun state (key, value) -> Result.bind state (add key value))
    (Ok (empty order))
    entries

module Builder = struct
  type ('key, 'value) map = ('key, 'value) t
  type ('key, 'value) t = { mutable current : ('key, 'value) map }

  let create current = { current }

  let set key value builder =
    let added, successor = set key value builder.current in
    builder.current <- successor;
    added

  let remove key builder =
    match remove key builder.current with
    | None -> None
    | Some (value, successor) ->
        builder.current <- successor;
        Some value

  let freeze builder = builder.current
end
