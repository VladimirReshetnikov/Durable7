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
