(** Implementation of the stamp-measured insertion-order sequence shared by the ordered
    collections.

    A thin facade over the lazy measured finger tree whose cached measure is each subtree's
    maximum stamp, so a stamp resolves to its ordinal position in one measure-directed [locate]
    descent. The maximum monoid's identity is [min_int]; {!pick_stamp} keeps every issued stamp
    strictly above it. *)

type 'element entry = { stamp : int; item : 'element }
type 'element t = ('element entry, int) Finger_tree.Measured_tree.t

let stride = 1 lsl 20

let make_policy () =
  Finger_tree.Measures.create_policy ~id:"ordered-stamp-max-v1"
    ~monoid:
      (Finger_tree.Measures.create_monoid ~empty:min_int
         ~append:(fun left right -> if left >= right then left else right))
    ~measure:(fun entry -> entry.stamp)
    ()

let empty policy = Finger_tree.Measured_tree.empty policy
let of_entries policy entries = Finger_tree.Measured_tree.of_list policy entries
let length order = Finger_tree.Measured_tree.length order
let is_empty order = Finger_tree.Measured_tree.is_empty order
let first order = Finger_tree.Measured_tree.first order
let last order = Finger_tree.Measured_tree.last order
let nth index order = Finger_tree.Measured_tree.nth index order

let index_of_stamp stamp order =
  match Finger_tree.Measured_tree.locate (fun prefix -> prefix >= stamp) order with
  | Some (index, _, entry) when entry.stamp = stamp -> Some index
  | Some _ | None -> None

let pick_stamp index order =
  if is_empty order then Some 0
  else if index = 0 then
    match first order with
    | None -> Some 0
    | Some head -> if head.stamp < min_int + stride + 1 then None else Some (head.stamp - stride)
  else if index = length order then
    match last order with
    | None -> Some 0
    | Some tail -> if tail.stamp > max_int - stride then None else Some (tail.stamp + stride)
  else
    match (nth (index - 1) order, nth index order) with
    | Some left, Some right ->
        let gap = right.stamp - left.stamp in
        if gap < 2 then None else Some (left.stamp + (gap / 2))
    | _ -> None

let insert_at index entry order =
  if index = 0 then Ok (Finger_tree.Measured_tree.cons entry order)
  else if index = length order then Ok (Finger_tree.Measured_tree.snoc order entry)
  else Finger_tree.Measured_tree.insert_at index entry order

let remove_at index order = Finger_tree.Measured_tree.remove_at index order
let update_at index entry order = Finger_tree.Measured_tree.update_at index (fun _ -> entry) order

let sub ~start ~count order =
  Finger_tree.Measured_tree.take count (Finger_tree.Measured_tree.drop start order)

let to_list order = Finger_tree.Measured_tree.to_list order
let fold_left folder state order = Finger_tree.Measured_tree.fold_left folder state order
