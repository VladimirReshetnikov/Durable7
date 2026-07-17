module Bit_set = Set.Make (Int)

type t = Bit_set.t
type statistics = { bit_count : int; chunk_count : int; highest_bit : int option }

let empty = Bit_set.empty
let count = Bit_set.cardinal
let is_empty = Bit_set.is_empty
let mem index set = index >= 0 && Bit_set.mem index set

let add index set =
  if index < 0 then Error "bit index must be non-negative"
  else
    let added = not (Bit_set.mem index set) in
    Ok (added, Bit_set.add index set)

let remove index set =
  let removed = index >= 0 && Bit_set.mem index set in
  (removed, if removed then Bit_set.remove index set else set)

let toggle index set =
  if index < 0 then Error "bit index must be non-negative"
  else if Bit_set.mem index set then Ok (false, Bit_set.remove index set)
  else Ok (true, Bit_set.add index set)

let of_list indexes =
  List.fold_left
    (fun state index -> Result.bind state (fun set -> Result.map snd (add index set)))
    (Ok empty) indexes

let union = Bit_set.union
let intersect = Bit_set.inter
let difference = Bit_set.diff

let symmetric_difference left right =
  Bit_set.union (Bit_set.diff left right) (Bit_set.diff right left)

let rank index set =
  Bit_set.fold (fun value result -> if value < index then result + 1 else result) set 0

let select rank set =
  if rank < 0 || rank >= count set then None
  else
    let current = ref 0 in
    let found = ref None in
    Bit_set.iter
      (fun value ->
        if !current = rank then found := Some value;
        incr current)
      set;
    !found

let next_set_bit index set =
  if Bit_set.is_empty set then None
  else
    let _, present, greater = Bit_set.split index set in
    if present then Some index
    else if Bit_set.is_empty greater then None
    else Some (Bit_set.min_elt greater)

let previous_set_bit index set =
  if Bit_set.is_empty set then None
  else
    let less, present, _ = Bit_set.split index set in
    if present then Some index
    else if Bit_set.is_empty less then None
    else Some (Bit_set.max_elt less)

let to_list = Bit_set.elements

let statistics set =
  let chunks = Hashtbl.create 16 in
  Bit_set.iter (fun index -> Hashtbl.replace chunks (index lsr 6) ()) set;
  {
    bit_count = count set;
    chunk_count = Hashtbl.length chunks;
    highest_bit = (if Bit_set.is_empty set then None else Some (Bit_set.max_elt set));
  }
