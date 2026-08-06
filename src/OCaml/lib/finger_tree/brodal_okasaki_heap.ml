(** Implementation of the persistent meldable minimum heap surface.

    This is deliberately simple storage, not the bootstrapped skew-binomial structure the name refers
    to: elements live in a plain list, so melding appends the two lists, the minimum is a linear
    fold, and the count is a walk. Persistence and comparator identity are exact, but none of the
    namesake worst-case bounds are delivered or claimed — the workspace API notes record that
    divergence, and {!statistics} reports only what this representation can actually measure.

    If the namesake bounds are ever wanted here, {!Persistent_monotone_action_heap} already carries a
    full bootstrapped skew-binomial kernel ported from the managed and Rust sources, and is the
    in-repo model to follow. *)

type 'element t = { order : 'element Common.Comparator.t; values : 'element list }
type statistics = { count : int }

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

(* Only the count is reported. The shape fields the sibling ports carry were previously synthesized
   here as [popcount count] and [log2 count] — the shape a real skew-binomial forest would have had —
   which made the audit describe a structure this storage does not build. *)
let statistics heap = { count = count heap }
