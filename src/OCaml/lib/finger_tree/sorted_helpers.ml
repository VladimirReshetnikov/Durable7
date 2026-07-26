(** Implementation of the internal immutable-array operations shared by sorted facades. *)

let bound move_right compare value values =
  let low = ref 0 in
  let high = ref (Array.length values) in
  while !low < !high do
    let middle = !low + ((!high - !low) / 2) in
    let relation = compare values.(middle) value in
    if relation < 0 || (move_right && relation = 0) then low := middle + 1 else high := middle
  done;
  !low

let lower_bound compare = bound false compare
let upper_bound compare = bound true compare

let insert index value values =
  let length = Array.length values in
  if index < 0 || index > length then invalid_arg "array insertion index is out of range";
  Array.init (length + 1) (fun current ->
      if current < index then values.(current)
      else if current = index then value
      else values.(current - 1))

let remove index values =
  let length = Array.length values in
  if index < 0 || index >= length then invalid_arg "array removal index is out of range";
  Array.init (length - 1) (fun current ->
      if current < index then values.(current) else values.(current + 1))

let slice start count values =
  if start < 0 || count < 0 || start > Array.length values - count then
    invalid_arg "array slice is out of range";
  Array.sub values start count
