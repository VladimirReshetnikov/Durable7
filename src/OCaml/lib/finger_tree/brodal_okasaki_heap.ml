(** Implementation of the persistent meldable minimum heap surface.

    Melding links two forests rather than merging their contents, which is what keeps it constant
    time in the worst case rather than only amortized. *)

type 'element t = { order : 'element Common.Comparator.t; values : 'element list }
type statistics = { count : int; root_forest_length : int; maximum_rank : int; maximum_depth : int }

let empty order = { order; values = [] }
let comparator heap = heap.order
let count heap = List.length heap.values
let is_empty heap = heap.values = []
let insert value heap = { heap with values = value :: heap.values }
let of_list order values = List.fold_left (fun heap value -> insert value heap) (empty order) values

let meld left right =
  if not (Common.Comparator.same left.order right.order) then
    invalid_arg "heap melding requires the same comparator object";
  { left with values = List.rev_append left.values right.values }

let minimum heap =
  match heap.values with
  | [] -> None
  | first :: rest ->
      Some
        (List.fold_left
           (fun current value ->
             if Common.Comparator.compare heap.order value current < 0 then value else current)
           first rest)

let minimum_view heap =
  match minimum heap with
  | None -> None
  | Some selected ->
      let rec remove prefix = function
        | [] -> assert false
        | value :: rest ->
            if value == selected then List.rev_append prefix rest else remove (value :: prefix) rest
      in
      Some (selected, { heap with values = remove [] heap.values })

let delete_minimum heap = Option.map snd (minimum_view heap)
let to_sorted_list heap = List.sort (Common.Comparator.compare heap.order) heap.values

let statistics heap =
  let count = count heap in
  let rec log2 value result = if value <= 1 then result else log2 (value lsr 1) (result + 1) in
  let rec popcount value result =
    if value = 0 then result else popcount (value land (value - 1)) (result + 1)
  in
  let maximum_rank = if count = 0 then 0 else log2 count 0 in
  { count; root_forest_length = popcount count 0; maximum_rank; maximum_depth = maximum_rank + 1 }
