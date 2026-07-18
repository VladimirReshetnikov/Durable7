type ('element, 'measure, 'tag) algebra = {
  id : string;
  identity : 'measure;
  combine : 'measure -> 'measure -> 'measure;
  measure_element : 'element -> 'measure;
  apply_element : 'tag -> 'element -> 'element;
  apply_measure : 'tag -> length:int -> 'measure -> 'measure;
  compose : 'tag -> 'tag -> 'tag;
}

type ('element, 'measure, 'tag) t = {
  algebra : ('element, 'measure, 'tag) algebra;
  values : 'element array;
  cached_measure : 'measure;
}

type ('element, 'measure, 'tag) cursor = {
  cursor_snapshot : ('element, 'measure, 'tag) t;
  cursor_position : int;
}

let create_algebra ~id ~identity ~combine ~measure ~apply_element ~apply_measure ~compose
    ~laws_verified () =
  if String.trim id = "" then Error "range-update algebra id must not be empty"
  else if not laws_verified then Error "range-update algebra laws must be explicitly verified"
  else
    Ok { id; identity; combine; measure_element = measure; apply_element; apply_measure; compose }

let algebra_id algebra = algebra.id
let compose_tags algebra = algebra.compose

let fold_measure algebra values =
  Array.fold_left
    (fun total value -> algebra.combine total (algebra.measure_element value))
    algebra.identity values

let make algebra values = { algebra; values; cached_measure = fold_measure algebra values }
let empty algebra = make algebra [||]
let of_list algebra values = make algebra (Array.of_list values)
let length sequence = Array.length sequence.values

let nth index sequence =
  if index < 0 || index >= length sequence then None else Some sequence.values.(index)

let to_list sequence = Array.to_list sequence.values
let measure sequence = sequence.cached_measure
let valid_range start count sequence = start >= 0 && count >= 0 && start <= length sequence - count

let measure_range ~start ~length:count sequence =
  if not (valid_range start count sequence) then Error "range measure is out of bounds"
  else Ok (fold_measure sequence.algebra (Array.sub sequence.values start count))

let update_range ~start ~length:count tag sequence =
  if not (valid_range start count sequence) then Error "range update is out of bounds"
  else
    let values = Array.copy sequence.values in
    for index = start to start + count - 1 do
      values.(index) <- sequence.algebra.apply_element tag values.(index)
    done;
    (* Exercise the algebra's aggregate law at the publication boundary. The rebuilt element measure
       remains the source of truth for this initial OCaml checkpoint. *)
    ignore
      (sequence.algebra.apply_measure tag ~length:count
         (fold_measure sequence.algebra (Array.sub sequence.values start count)));
    Ok (make sequence.algebra values)

let insert index value sequence =
  if index < 0 || index > length sequence then
    Error "range sequence insertion index is out of bounds"
  else Ok (make sequence.algebra (Sorted_helpers.insert index value sequence.values))

let remove index sequence =
  if index < 0 || index >= length sequence then
    Error "range sequence removal index is out of bounds"
  else
    let value = sequence.values.(index) in
    Ok (value, make sequence.algebra (Sorted_helpers.remove index sequence.values))

let split_at index sequence =
  let index = Int.max 0 (Int.min index (length sequence)) in
  ( make sequence.algebra (Array.sub sequence.values 0 index),
    make sequence.algebra (Array.sub sequence.values index (length sequence - index)) )

let cursor sequence = { cursor_snapshot = sequence; cursor_position = 0 }

let cursor_at position sequence =
  if position < 0 || position > length sequence then None
  else Some { cursor_snapshot = sequence; cursor_position = position }

let cursor_position value = value.cursor_position
let cursor_length value = length value.cursor_snapshot
let cursor_is_at_start value = value.cursor_position = 0
let cursor_is_at_end value = value.cursor_position = cursor_length value

let cursor_measure_before value =
  Result.get_ok (measure_range ~start:0 ~length:value.cursor_position value.cursor_snapshot)

let cursor_measure_after value =
  Result.get_ok
    (measure_range ~start:value.cursor_position
       ~length:(cursor_length value - value.cursor_position)
       value.cursor_snapshot)

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
    let values = Array.copy value.cursor_snapshot.values in
    values.(value.cursor_position) <- element;
    Some
      {
        cursor_snapshot = make value.cursor_snapshot.algebra values;
        cursor_position = value.cursor_position;
      }

let cursor_measure_previous count value =
  if count < 0 || count > value.cursor_position then Error "cursor previous range is out of bounds"
  else measure_range ~start:(value.cursor_position - count) ~length:count value.cursor_snapshot

let cursor_measure_next count value =
  if count < 0 || count > cursor_length value - value.cursor_position then
    Error "cursor next range is out of bounds"
  else measure_range ~start:value.cursor_position ~length:count value.cursor_snapshot

let cursor_update_previous count tag value =
  if count < 0 || count > value.cursor_position then Error "cursor previous range is out of bounds"
  else
    Result.map
      (fun snapshot -> { cursor_snapshot = snapshot; cursor_position = value.cursor_position })
      (update_range ~start:(value.cursor_position - count) ~length:count tag value.cursor_snapshot)

let cursor_update_next count tag value =
  if count < 0 || count > cursor_length value - value.cursor_position then
    Error "cursor next range is out of bounds"
  else
    Result.map
      (fun snapshot -> { cursor_snapshot = snapshot; cursor_position = value.cursor_position })
      (update_range ~start:value.cursor_position ~length:count tag value.cursor_snapshot)

let cursor_snapshot value = value.cursor_snapshot
