type 'element t = 'element Finger_tree.Persistent_deque.t

let empty = Finger_tree.Persistent_deque.empty
let of_list = Finger_tree.Persistent_deque.of_list
let length = Finger_tree.Persistent_deque.length
let is_empty = Finger_tree.Persistent_deque.is_empty
let first = Finger_tree.Persistent_deque.first
let last = Finger_tree.Persistent_deque.last
let nth = Finger_tree.Persistent_deque.nth
let append value list = Finger_tree.Persistent_deque.snoc list value
let prepend = Finger_tree.Persistent_deque.cons
let join = Finger_tree.Persistent_deque.concat
let insert = Finger_tree.Persistent_deque.insert_at
let valid_range start count list = start >= 0 && count >= 0 && start <= length list - count

let insert_range index values list =
  if index < 0 || index > length list then Error "Tungsten List insertion index is out of bounds"
  else
    let left, right = Finger_tree.Persistent_deque.split_at index list in
    Ok (join (join left (of_list values)) right)

let remove_at index list = Result.map snd (Finger_tree.Persistent_deque.remove_at index list)

let remove_range ~start ~count list =
  if not (valid_range start count list) then Error "Tungsten List removal range is out of bounds"
  else
    let left, tail = Finger_tree.Persistent_deque.split_at start list in
    let _, right = Finger_tree.Persistent_deque.split_at count tail in
    Ok (join left right)

let set index value list = Finger_tree.Persistent_deque.update_at index (fun _ -> value) list
let update = Finger_tree.Persistent_deque.update_at

let range ~start ~count list =
  if not (valid_range start count list) then Error "Tungsten List range is out of bounds"
  else Finger_tree.Persistent_deque.slice ~start ~length:count list

let take count list = range ~start:0 ~count list

let take_last count list =
  if count < 0 || count > length list then Error "Tungsten List count is out of bounds"
  else range ~start:(length list - count) ~count list

let drop count list =
  if count < 0 || count > length list then Error "Tungsten List count is out of bounds"
  else range ~start:count ~count:(length list - count) list

let drop_last count list =
  if count < 0 || count > length list then Error "Tungsten List count is out of bounds"
  else range ~start:0 ~count:(length list - count) list

let split_at index list =
  if index < 0 || index > length list then Error "Tungsten List split index is out of bounds"
  else Ok (Finger_tree.Persistent_deque.split_at index list)

let to_list = Finger_tree.Persistent_deque.to_list
let reverse list = if length list <= 1 then list else of_list (List.rev (to_list list))

let map transform list =
  let _, reversed =
    Finger_tree.Persistent_deque.fold_left
      (fun (index, result) value -> (index + 1, transform value index :: result))
      (0, []) list
  in
  of_list (List.rev reversed)

let index_of ?(equal = ( = )) value list =
  let rec search index = function
    | [] -> None
    | candidate :: rest -> if equal candidate value then Some index else search (index + 1) rest
  in
  search 0 (to_list list)

let mem ?(equal = ( = )) value list = Option.is_some (index_of ~equal value list)
