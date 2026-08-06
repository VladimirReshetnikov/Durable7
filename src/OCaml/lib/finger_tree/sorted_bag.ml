(** Implementation of the persistent order-statistic sorted multiset.

    Built on the lazy Hinze–Paterson measured core with the same [(count, last element)] measure as
    the sorted set: O(log n) point writes by split-and-join, O(1) worst-case comparator-free
    extremes from the digits, O(log n) rank select and counting queries, and O(n log n + n) bulk
    construction. Duplicates are kept; an insertion lands after every equal element. *)

type 'element t = {
  order : 'element Common.Comparator.t;
  tree : ('element, int * 'element option) Measured_tree.t;
}

let compare bag = Common.Comparator.compare bag.order

let make_policy () =
  Measures.create_policy ~id:"sorted-order-statistic-v1"
    ~monoid:
      (Measures.create_monoid ~empty:(0, None)
         ~append:(fun (left_count, left_last) (right_count, right_last) ->
           ( left_count + right_count,
             match right_last with None -> left_last | Some _ -> right_last )))
    ~measure:(fun value -> (1, Some value))
    ()

let empty order = { order; tree = Measured_tree.empty (make_policy ()) }

(* A stable sort keeps equal elements in arrival order, then one eager O(n) bottom-up build. *)
let of_list order values =
  let sorted = List.stable_sort (Common.Comparator.compare order) values in
  { order; tree = Measured_tree.of_list (make_policy ()) sorted }

let comparator bag = bag.order
let count bag = Measured_tree.length bag.tree
let is_empty bag = count bag = 0
let minimum bag = Measured_tree.first bag.tree
let maximum bag = Measured_tree.last bag.tree
let nth index bag = Measured_tree.nth index bag.tree

(* The first element ordering at-or-after the probe, with its rank: one measure-directed descent
   costing O(log n) comparator calls. *)
let at_least value bag =
  Measured_tree.locate
    (fun (_, last) ->
      match last with None -> false | Some candidate -> compare bag candidate value >= 0)
    bag.tree

(* The first element ordering strictly after the probe, with its rank. *)
let above value bag =
  Measured_tree.locate
    (fun (_, last) ->
      match last with None -> false | Some candidate -> compare bag candidate value > 0)
    bag.tree

let count_less_than value bag =
  match at_least value bag with Some (index, _, _) -> index | None -> count bag

let count_at_most value bag =
  match above value bag with Some (index, _, _) -> index | None -> count bag

let count_of value bag = count_at_most value bag - count_less_than value bag
let mem value bag = count_of value bag <> 0

let add value bag =
  match above value bag with
  | Some (index, _, _) ->
      { bag with tree = Result.get_ok (Measured_tree.insert_at index value bag.tree) }
  | None -> { bag with tree = Measured_tree.snoc bag.tree value }

let remove value bag =
  match at_least value bag with
  | Some (index, _, found) when compare bag found value = 0 ->
      let _, tree = Result.get_ok (Measured_tree.remove_at index bag.tree) in
      { bag with tree }
  | Some _ | None -> bag

let remove_at index bag =
  if index < 0 || index >= count bag then Error "sorted-bag index is out of bounds"
  else
    let _, tree = Result.get_ok (Measured_tree.remove_at index bag.tree) in
    Ok { bag with tree }

let remove_all value bag =
  let first = count_less_than value bag in
  let last = count_at_most value bag in
  if first = last then bag
  else
    {
      bag with
      tree =
        Result.get_ok
          (Measured_tree.concat
             (Measured_tree.take first bag.tree)
             (Measured_tree.drop last bag.tree));
    }

let range ~start ~count:range_count bag =
  if start < 0 || range_count < 0 || start > count bag - range_count then
    Error "sorted-bag range is out of bounds"
  else
    Ok { bag with tree = Measured_tree.take range_count (Measured_tree.drop start bag.tree) }

let value_range ~minimum:low ~maximum:high bag =
  if compare bag low high > 0 then empty bag.order
  else
    let first = count_less_than low bag in
    let last = count_at_most high bag in
    { bag with tree = Measured_tree.take (last - first) (Measured_tree.drop first bag.tree) }

let to_list bag = Measured_tree.to_list bag.tree
