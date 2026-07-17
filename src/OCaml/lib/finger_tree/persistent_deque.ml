type 'element t = Empty | Tree of ('element, int) Measured_tree.t

let empty = Empty
let singleton value = Tree (Measured_tree.singleton Measures.size value)

let of_list values =
  match values with [] -> Empty | _ -> Tree (Measured_tree.of_list Measures.size values)

let tree = function Empty -> Measured_tree.empty Measures.size | Tree value -> value
let wrap value = if Measured_tree.is_empty value then Empty else Tree value
let is_empty = function Empty -> true | Tree _ -> false
let length = function Empty -> 0 | Tree value -> Measured_tree.length value
let to_list = function Empty -> [] | Tree value -> Measured_tree.to_list value
let cons value deque = wrap (Measured_tree.cons value (tree deque))
let snoc deque value = wrap (Measured_tree.snoc (tree deque) value)
let concat left right = wrap (Result.get_ok (Measured_tree.concat (tree left) (tree right)))
let first = function Empty -> None | Tree value -> Measured_tree.first value
let last = function Empty -> None | Tree value -> Measured_tree.last value

let pop_front deque =
  match Measured_tree.remove_at 0 (tree deque) with
  | Error _ -> None
  | Ok (value, successor) -> Some (value, wrap successor)

let pop_back deque =
  match Measured_tree.remove_at (length deque - 1) (tree deque) with
  | Error _ -> None
  | Ok (value, successor) -> Some (wrap successor, value)

let nth index = function Empty -> None | Tree value -> Measured_tree.nth index value

let split_at index deque =
  let left, right = Measured_tree.split_at index (tree deque) in
  (wrap left, wrap right)

let take count deque = fst (split_at count deque)
let drop count deque = snd (split_at count deque)

let slice ~start ~length:count deque =
  if start < 0 || count < 0 || start > length deque - count then Error "deque slice is out of range"
  else Ok (take count (drop start deque))

let insert_at index value deque = Result.map wrap (Measured_tree.insert_at index value (tree deque))

let update_at index update deque =
  Result.map wrap (Measured_tree.update_at index update (tree deque))

let remove_at index deque =
  Result.map
    (fun (value, successor) -> (value, wrap successor))
    (Measured_tree.remove_at index (tree deque))

let iter action = function Empty -> () | Tree value -> Measured_tree.iter action value

let fold_left folder state = function
  | Empty -> state
  | Tree value -> Measured_tree.fold_left folder state value

let validate = function Empty -> Ok () | Tree value -> Measured_tree.validate value
