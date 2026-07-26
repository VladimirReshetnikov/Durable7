(** Implementation of the immutable snapshot-plus-position cursor for a generic rope. *)

type 'element t = { snapshot : 'element Rope.t; cursor_position : int }

let create ?(position = 0) snapshot =
  if position < 0 || position > Rope.length snapshot then
    Error "rope cursor position is out of bounds"
  else Ok { snapshot; cursor_position = position }

let rope cursor = cursor.snapshot
let position cursor = cursor.cursor_position

let move_to cursor_position cursor =
  if cursor_position < 0 || cursor_position > Rope.length cursor.snapshot then
    Error "rope cursor position is out of bounds"
  else Ok { cursor with cursor_position }

let peek_before cursor = Rope.nth (cursor.cursor_position - 1) cursor.snapshot
let peek_after cursor = Rope.nth cursor.cursor_position cursor.snapshot

let insert_many values cursor =
  {
    snapshot = Result.get_ok (Rope.insert cursor.cursor_position values cursor.snapshot);
    cursor_position = cursor.cursor_position + List.length values;
  }

let insert value cursor = insert_many [ value ] cursor

let delete_before count cursor =
  if count < 0 || count > cursor.cursor_position then
    Error "rope cursor backward deletion is out of bounds"
  else
    let cursor_position = cursor.cursor_position - count in
    Result.map
      (fun snapshot -> { snapshot; cursor_position })
      (Rope.remove ~start:cursor_position ~length:count cursor.snapshot)

let delete_after count cursor =
  if count < 0 || count > Rope.length cursor.snapshot - cursor.cursor_position then
    Error "rope cursor forward deletion is out of bounds"
  else
    Result.map
      (fun snapshot -> { cursor with snapshot })
      (Rope.remove ~start:cursor.cursor_position ~length:count cursor.snapshot)
