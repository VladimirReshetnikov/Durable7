type ('element, 'measure) t = ('element, 'measure) Measured_sequence.t
type ('element, 'measure) cursor = { snapshot : ('element, 'measure) t; position : int }

let empty = Measured_sequence.empty
let of_list = Measured_sequence.of_list
let length = Measured_sequence.length
let measure = Measured_sequence.measure
let nth = Measured_sequence.nth
let to_list = Measured_sequence.to_list
let concat = Measured_sequence.concat
let split_at = Measured_sequence.split_at
let locate = Measured_sequence.locate

let create_cursor ?(position = 0) snapshot =
  if position < 0 || position > length snapshot then Error "measured-rope cursor is out of bounds"
  else Ok { snapshot; position }

let cursor_rope cursor = cursor.snapshot
let cursor_position cursor = cursor.position

let cursor_move_to position cursor =
  if position < 0 || position > length cursor.snapshot then
    Error "measured-rope cursor is out of bounds"
  else Ok { cursor with position }

let cursor_measure_before cursor =
  Result.get_ok (Measured_sequence.measure_range 0 cursor.position cursor.snapshot)

let cursor_insert value cursor =
  {
    snapshot = Result.get_ok (Measured_sequence.insert_at cursor.position value cursor.snapshot);
    position = cursor.position + 1;
  }

let cursor_delete_after count cursor =
  if count < 0 || count > length cursor.snapshot - cursor.position then
    Error "measured-rope cursor deletion is out of bounds"
  else
    let left, tail = split_at cursor.position cursor.snapshot in
    let _, right = split_at count tail in
    Result.map (fun snapshot -> { cursor with snapshot }) (concat left right)
