(** Implementation of the persistent deque with an O(1) logical reversal bit.

    Reversal flips an orientation bit and shares the tree; every read consults that bit rather than
    the stored order. *)

type 'element t = { deque : 'element Persistent_deque.t; reversed : bool }
type 'element cursor = { cursor_snapshot : 'element t; cursor_position : int }

let empty = { deque = Persistent_deque.empty; reversed = false }
let of_list values = { deque = Persistent_deque.of_list values; reversed = false }
let length value = Persistent_deque.length value.deque
let is_empty value = Persistent_deque.is_empty value.deque
let reverse value = { value with reversed = not value.reversed }

let cons element value =
  if value.reversed then { value with deque = Persistent_deque.snoc value.deque element }
  else { value with deque = Persistent_deque.cons element value.deque }

let snoc value element =
  if value.reversed then { value with deque = Persistent_deque.cons element value.deque }
  else { value with deque = Persistent_deque.snoc value.deque element }

let first value =
  if value.reversed then Persistent_deque.last value.deque else Persistent_deque.first value.deque

let last value =
  if value.reversed then Persistent_deque.first value.deque else Persistent_deque.last value.deque

let pop_front value =
  if value.reversed then
    Option.map
      (fun (successor, element) -> (element, { value with deque = successor }))
      (Persistent_deque.pop_back value.deque)
  else
    Option.map
      (fun (element, successor) -> (element, { value with deque = successor }))
      (Persistent_deque.pop_front value.deque)

let pop_back value =
  if value.reversed then
    Option.map
      (fun (element, successor) -> ({ value with deque = successor }, element))
      (Persistent_deque.pop_front value.deque)
  else
    Option.map
      (fun (successor, element) -> ({ value with deque = successor }, element))
      (Persistent_deque.pop_back value.deque)

let nth index value =
  if value.reversed then Persistent_deque.nth (length value - index - 1) value.deque
  else Persistent_deque.nth index value.deque

let to_list value =
  let values = Persistent_deque.to_list value.deque in
  if value.reversed then List.rev values else values

let concat left right = of_list (to_list left @ to_list right)
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

let split_list position elements =
  let rec loop remaining reversed_before rest =
    if remaining = 0 then (List.rev reversed_before, rest)
    else
      match rest with
      | [] -> (List.rev reversed_before, [])
      | head :: tail -> loop (remaining - 1) (head :: reversed_before) tail
  in
  loop position [] elements

let cursor_insert_many elements value =
  match elements with
  | [] -> value
  | _ ->
      let before, after = split_list value.cursor_position (to_list value.cursor_snapshot) in
      {
        cursor_snapshot = of_list (before @ elements @ after);
        cursor_position = value.cursor_position + List.length elements;
      }

let cursor_insert element value = cursor_insert_many [ element ] value

let cursor_delete_previous value =
  if cursor_is_at_start value then None
  else
    let elements = to_list value.cursor_snapshot in
    let snapshot =
      of_list (List.filteri (fun index _ -> index <> value.cursor_position - 1) elements)
    in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position - 1 }

let cursor_delete_next value =
  if cursor_is_at_end value then None
  else
    let elements = to_list value.cursor_snapshot in
    let snapshot =
      of_list (List.filteri (fun index _ -> index <> value.cursor_position) elements)
    in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position }

let cursor_replace_next element value =
  if cursor_is_at_end value then None
  else
    let elements =
      List.mapi
        (fun index stored -> if index = value.cursor_position then element else stored)
        (to_list value.cursor_snapshot)
    in
    Some { cursor_snapshot = of_list elements; cursor_position = value.cursor_position }

let cursor_reverse value =
  {
    cursor_snapshot = reverse value.cursor_snapshot;
    cursor_position = cursor_length value - value.cursor_position;
  }

let cursor_snapshot value = value.cursor_snapshot
