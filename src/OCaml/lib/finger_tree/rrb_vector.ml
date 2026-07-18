type 'element t = 'element Persistent_deque.t
type 'element cursor = { cursor_snapshot : 'element t; cursor_position : int }
type statistics = { count : int; estimated_leaves : int; depth : int }

let empty = Persistent_deque.empty
let singleton = Persistent_deque.singleton
let of_list = Persistent_deque.of_list
let length = Persistent_deque.length
let is_empty = Persistent_deque.is_empty
let nth = Persistent_deque.nth
let set index value vector = Persistent_deque.update_at index (fun _ -> value) vector
let append value vector = Persistent_deque.snoc vector value
let prepend = Persistent_deque.cons
let concat = Persistent_deque.concat
let insert = Persistent_deque.insert_at
let remove = Persistent_deque.remove_at
let pop = Persistent_deque.pop_back
let split_at = Persistent_deque.split_at
let slice = Persistent_deque.slice
let to_list = Persistent_deque.to_list

let statistics vector =
  let count = length vector in
  let estimated_leaves = if count = 0 then 0 else (count + 31) / 32 in
  let rec depth nodes result =
    if nodes <= 1 then result else depth ((nodes + 31) / 32) (result + 1)
  in
  { count; estimated_leaves; depth = depth estimated_leaves (if count = 0 then 0 else 1) }

let cursor vector = { cursor_snapshot = vector; cursor_position = 0 }

let cursor_at position vector =
  if position < 0 || position > length vector then None
  else Some { cursor_snapshot = vector; cursor_position = position }

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
    cursor_snapshot = Result.get_ok (insert value.cursor_position element value.cursor_snapshot);
    cursor_position = value.cursor_position + 1;
  }

let cursor_insert_vector inserted value =
  if is_empty inserted then value
  else
    let left, right = split_at value.cursor_position value.cursor_snapshot in
    {
      cursor_snapshot = concat (concat left inserted) right;
      cursor_position = value.cursor_position + length inserted;
    }

let cursor_insert_many elements value = cursor_insert_vector (of_list elements) value

let cursor_delete_previous value =
  if cursor_is_at_start value then None
  else
    let _, snapshot = Result.get_ok (remove (value.cursor_position - 1) value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position - 1 }

let cursor_delete_next value =
  if cursor_is_at_end value then None
  else
    let _, snapshot = Result.get_ok (remove value.cursor_position value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position }

let cursor_replace_next element value =
  if cursor_is_at_end value then None
  else
    let snapshot = Result.get_ok (set value.cursor_position element value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position }

let cursor_snapshot value = value.cursor_snapshot

module Builder = struct
  type 'element vector = 'element t
  type 'element t = { mutable current : 'element vector }

  let create current = { current }
  let length builder = length builder.current
  let append value builder = builder.current <- append value builder.current

  let set index value builder =
    Result.map (fun successor -> builder.current <- successor) (set index value builder.current)

  let remove index builder =
    Result.map
      (fun (value, successor) ->
        builder.current <- successor;
        value)
      (remove index builder.current)

  let freeze builder = builder.current
end
