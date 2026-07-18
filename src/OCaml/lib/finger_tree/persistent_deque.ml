type 'element t = Empty | Tree of ('element, int) Measured_tree.t
type 'element cursor = { cursor_snapshot : 'element t; cursor_position : int }

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
let cursor deque = { cursor_snapshot = deque; cursor_position = 0 }

let cursor_at position deque =
  if position < 0 || position > length deque then None
  else Some { cursor_snapshot = deque; cursor_position = position }

let cursor_position value = value.cursor_position
let cursor_length value = length value.cursor_snapshot
let cursor_is_at_start value = value.cursor_position = 0
let cursor_is_at_end value = value.cursor_position = cursor_length value
let cursor_peek_previous value = nth (value.cursor_position - 1) value.cursor_snapshot
let cursor_peek_next value = nth value.cursor_position value.cursor_snapshot
let cursor_move_previous value = cursor_at (value.cursor_position - 1) value.cursor_snapshot

let cursor_move_next value =
  if cursor_is_at_end value then None
  else cursor_at (value.cursor_position + 1) value.cursor_snapshot

let cursor_seek position value = cursor_at position value.cursor_snapshot

let cursor_insert element value =
  {
    cursor_snapshot = Result.get_ok (insert_at value.cursor_position element value.cursor_snapshot);
    cursor_position = value.cursor_position + 1;
  }

let cursor_insert_many elements value =
  match elements with
  | [] -> value
  | _ ->
      let left, right = split_at value.cursor_position value.cursor_snapshot in
      {
        cursor_snapshot = concat (concat left (of_list elements)) right;
        cursor_position = value.cursor_position + List.length elements;
      }

let cursor_delete_previous value =
  if cursor_is_at_start value then None
  else
    let _, snapshot = Result.get_ok (remove_at (value.cursor_position - 1) value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position - 1 }

let cursor_delete_next value =
  if cursor_is_at_end value then None
  else
    let _, snapshot = Result.get_ok (remove_at value.cursor_position value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position }

let cursor_replace_next element value =
  if cursor_is_at_end value then None
  else
    let snapshot =
      Result.get_ok (update_at value.cursor_position (fun _ -> element) value.cursor_snapshot)
    in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position }

let cursor_snapshot value = value.cursor_snapshot
