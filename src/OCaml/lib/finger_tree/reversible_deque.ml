(** Implementation of the persistent deque with an O(1) logical reversal bit.

    Reversal flips an orientation bit and shares the tree; every read consults that bit rather
    than the stored order. Concatenation is orientation-aware and structural: operands stored in
    opposite orientations are joined by taking the lazy O(1) reversed view of one side and
    concatenating the trees in O(log(min(n, m))), never by re-materializing either operand. Cursor
    edits map the logical position to a physical index and go through the underlying tree's
    O(log n) splice operations. *)

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

(* Orientation-aware structural concatenation. With [rev] for a stored order read backwards,
   [rev a ++ rev b = rev (b ++ a)], so equal orientations concatenate the stored trees directly
   (swapped when both are reversed) and mismatched orientations take the O(1) lazy reversed view
   of the right operand's tree first — O(log(min(n, m))) overall, sharing both operands. *)
let concat left right =
  if left.reversed = right.reversed then
    if left.reversed then
      { deque = Persistent_deque.concat right.deque left.deque; reversed = true }
    else { deque = Persistent_deque.concat left.deque right.deque; reversed = false }
  else if left.reversed then
    { deque = Persistent_deque.concat (Persistent_deque.reverse right.deque) left.deque;
      reversed = true }
  else
    { deque = Persistent_deque.concat left.deque (Persistent_deque.reverse right.deque);
      reversed = false }

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

(* The logical gap [position] of a snapshot corresponds to the stored gap counted from the other
   end when the snapshot is reversed; a logical element index mirrors to [length - index - 1]. *)
let stored_gap snapshot position =
  if snapshot.reversed then length snapshot - position else position

let stored_index snapshot index = if snapshot.reversed then length snapshot - index - 1 else index

let cursor_insert_many elements value =
  match elements with
  | [] -> value
  | _ ->
      let snapshot = value.cursor_snapshot in
      let gap = stored_gap snapshot value.cursor_position in
      let block =
        Persistent_deque.of_list (if snapshot.reversed then List.rev elements else elements)
      in
      let left, right = Persistent_deque.split_at gap snapshot.deque in
      let deque = Persistent_deque.concat (Persistent_deque.concat left block) right in
      {
        cursor_snapshot = { snapshot with deque };
        cursor_position = value.cursor_position + List.length elements;
      }

let cursor_insert element value = cursor_insert_many [ element ] value

let cursor_delete_previous value =
  if cursor_is_at_start value then None
  else
    let snapshot = value.cursor_snapshot in
    let stored = stored_index snapshot (value.cursor_position - 1) in
    let _, deque = Result.get_ok (Persistent_deque.remove_at stored snapshot.deque) in
    Some
      {
        cursor_snapshot = { snapshot with deque };
        cursor_position = value.cursor_position - 1;
      }

let cursor_delete_next value =
  if cursor_is_at_end value then None
  else
    let snapshot = value.cursor_snapshot in
    let stored = stored_index snapshot value.cursor_position in
    let _, deque = Result.get_ok (Persistent_deque.remove_at stored snapshot.deque) in
    Some { cursor_snapshot = { snapshot with deque }; cursor_position = value.cursor_position }

let cursor_replace_next element value =
  if cursor_is_at_end value then None
  else
    let snapshot = value.cursor_snapshot in
    let stored = stored_index snapshot value.cursor_position in
    let deque =
      Result.get_ok (Persistent_deque.update_at stored (fun _ -> element) snapshot.deque)
    in
    Some { cursor_snapshot = { snapshot with deque }; cursor_position = value.cursor_position }

let cursor_reverse value =
  {
    cursor_snapshot = reverse value.cursor_snapshot;
    cursor_position = cursor_length value - value.cursor_position;
  }

let cursor_snapshot value = value.cursor_snapshot
